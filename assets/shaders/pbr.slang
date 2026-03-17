/* Copyright (c) 2025 Holochip Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 the "License";
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
// Import shared utility modules
import common_types;
import pbr_utils;
import lighting_utils;
import tonemapping_utils;

// Input from vertex buffer
struct VSInput {
    [[vk::location(0)]] float3 Position;
    [[vk::location(1)]] float3 Normal;
    [[vk::location(2)]] float2 UV;
    [[vk::location(3)]] float4 Tangent;

    // Per-instance data. Model matrix is a true 4x4, while the normal
    // matrix is provided as three float4 columns that match the CPU
    // layout (glm::mat3x4: 3 columns of vec4, xyz used, w unused).
    [[vk::location(4)]] column_major float4x4 InstanceModelMatrix;  // binding 1 (uses 4 locations)
    [[vk::location(8)]] float4 InstanceNormal0;                     // normal matrix column 0
    [[vk::location(9)]] float4 InstanceNormal1;                     // normal matrix column 1
    [[vk::location(10)]] float4 InstanceNormal2;                    // normal matrix column 2
    [[vk::location(11)]] uint MaterialIndex;                        // material index for this instance
};

// Output from vertex shader / Input to fragment shader
struct VSOutput {
    float4 Position : SV_POSITION;
    float3 WorldPos;
    float3 Normal : NORMAL;
    float3 GeometricNormal : NORMAL1;
    float2 UV : TEXCOORD0;
    float4 Tangent : TANGENT;
    uint MaterialIndex : TEXCOORD1;
};

[[vk::binding(0, 0)]] ConstantBuffer<UniformBufferObject> ubo;
[[vk::binding(1, 0)]] Sampler2D baseColorMapStub; // Stub for validation, not used in bindless path
[[vk::binding(6, 0)]] StructuredBuffer<LightData> lightBuffer;
[[vk::binding(7, 0)]] StructuredBuffer<TileHeader> tileHeaders;
[[vk::binding(8, 0)]] StructuredBuffer<uint> tileLightIndices;
[[vk::binding(9, 0)]] StructuredBuffer<GeometryInfo> instanceBuffer; // instanceBuffer used as geometryInfoBuffer in some contexts
[[vk::binding(10, 0)]] Sampler2D reflectionMap;
[[vk::binding(11, 0)]] RaytracingAccelerationStructure tlas;
[[vk::binding(12, 0)]] StructuredBuffer<GeometryInfo> geometryInfoBuffer;
[[vk::binding(13, 0)]] StructuredBuffer<MaterialData> materialBuffer;

[[vk::binding(0, 1)]] Sampler2D globalTextures[];

[[vk::push_constant]] PushConstants material;

static const float RASTER_SHADOW_EPS = 0.001;

// Hard shadow query for raster fragment shading.
// NOTE: We intentionally treat NON_OPAQUE candidates as non-occluding here.
// To make glass/transmissive surfaces not block light, those instances should
// be flagged as FORCE_NO_OPAQUE in the TLAS build.
bool traceShadowOccluded(float3 origin, float3 direction, float tMin, float tMax)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = tMin;
    ray.TMax = tMax;

    RayQuery<RAY_FLAG_NONE> q;
    // Match TLAS instance masking convention from `renderer_ray_query.cpp`:
    // 0x01 = regular scene geometry, 0x02 = environment/sky.
    // For raster shadows, ignore the environment to avoid global false occlusion.
    uint mask = 0x01;
    q.TraceRayInline(
        tlas,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        mask,
        ray
    );

    int iter = 0;
    while (q.Proceed() && iter < 64)
    {
        iter++;
        // No special acceptance rules here; accept-first-hit will commit on the first opaque blocker.
    }
    return (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT);
}

// Vertex shader entry point
[[shader("vertex")]]
VSOutput VSMain(VSInput input)
{
    VSOutput output;
    float4x4 instanceModelMatrix = input.InstanceModelMatrix;
    float4 worldPos = mul(ubo.model, mul(instanceModelMatrix, float4(input.Position, 1.0)));
    output.Position = mul(ubo.proj, mul(ubo.view, worldPos));
    output.WorldPos = worldPos.xyz;

    // Transform normals correctly: first by the per-instance normal matrix,
    // then by the entity model 3x3 (avoid double-applying instance transform).
    float3x3 instNormal = float3x3(input.InstanceNormal0.xyz, input.InstanceNormal1.xyz, input.InstanceNormal2.xyz);
    float3x3 model3x3 = (float3x3)ubo.model;
    float3 worldNormal = normalize(mul(model3x3, mul(instNormal, input.Normal)));
    output.Normal = worldNormal;

    // Geometric normal (pre-normal-map) uses the same transform path.
    output.GeometricNormal = worldNormal;

    // Transform tangent similarly (approximate with same normal transform path).
    float3 worldTangent = normalize(mul(model3x3, mul(instNormal, input.Tangent.xyz)));
    output.UV = input.UV;
    output.Tangent = float4(worldTangent, input.Tangent.w);
    output.MaterialIndex = input.MaterialIndex;
    return output;
}

// Fragment shader entry point for generic PBR materials
[[shader("fragment")]]
float4 PSMain(VSOutput input) : SV_TARGET
{
    // --- 1. Material Properties ---
    float2 uv = float2(input.UV.x, 1.0 - input.UV.y);
    MaterialData mat = materialBuffer[NonUniformResourceIndex(input.MaterialIndex)];
    float4 baseColor = (mat.baseColorTexIndex < 0) ? float4(mat.albedo, mat.alpha) : globalTextures[NonUniformResourceIndex(mat.baseColorTexIndex)].Sample(uv) * float4(mat.albedo, mat.alpha);
    float4 mrOrSpecGloss = (mat.physicalTexIndex < 0) ? float4(1.0, 1.0, 1.0, 1.0) : globalTextures[NonUniformResourceIndex(mat.physicalTexIndex)].Sample(uv);
    float metallic = 0.0, roughness = 1.0;
    float3 F0, albedo;

    if (mat.useSpecGlossWorkflow != 0) {
        float3 specColorSG = mrOrSpecGloss.rgb * mat.specularFactor;
        float gloss = clamp(mrOrSpecGloss.a * mat.glossinessFactor, 0.0, 1.0);
        roughness = clamp(1.0 - gloss, 0.0, 1.0);
        F0 = specColorSG;
        albedo = baseColor.rgb * (1.0 - max(F0.r, max(F0.g, F0.b)));
    } else {
        // glTF metallic-roughness texture packs metallic in B, roughness in G (linear space)
        float metallicTex = mrOrSpecGloss.b;
        float roughnessTex = mrOrSpecGloss.g;
        metallic = clamp(metallicTex * mat.metallic, 0.0, 1.0);
        roughness = clamp(roughnessTex * mat.roughness, 0.0, 1.0);
        F0 = lerp(float3(0.04, 0.04, 0.04), baseColor.rgb, metallic);
        albedo = baseColor.rgb * (1.0 - metallic);
    }

    float ao = (mat.occlusionTexIndex < 0) ? 1.0 : globalTextures[NonUniformResourceIndex(mat.occlusionTexIndex)].Sample(uv).r;

    // Emissive: default to constant white when no emissive texture so authored emissiveFactor works per glTF spec.
    // If a texture is present but factor is zero, assume (1,1,1) to preserve emissive textures by default.
    float3 emissiveTex = (mat.emissiveTexIndex < 0) ? float3(1.0, 1.0, 1.0) : globalTextures[NonUniformResourceIndex(mat.emissiveTexIndex)].Sample(uv).rgb;
    float3 emissiveFactor = mat.emissive;
    float3 emissive = emissiveTex * emissiveFactor;
    if (mat.hasEmissiveStrengthExt != 0)
      emissive *= mat.emissiveStrength;

    if (mat.alphaMode == 1 && baseColor.a < mat.alphaCutoff) { discard; }

    // --- 2. Normal Calculation ---
    float3 N = normalize(input.Normal);
    if (mat.normalTexIndex >= 0) {
        float3 tangentNormal = globalTextures[NonUniformResourceIndex(mat.normalTexIndex)].Sample(uv).xyz * 2.0 - 1.0;
        float3 T = normalize(input.Tangent.xyz);
        // We flip the V coordinate for all textures (uv.y -> 1-uv.y). In
        // tangent space, this corresponds to inverting the bitangent.
        // glTF's tangent.w encodes the sign of the bitangent relative to
        // cross(N,T) in the *unflipped* UV space, so we must negate it here
        // to keep the normal map oriented correctly after the V flip.
        float handedness = -input.Tangent.w;
        float3 B = normalize(cross(N, T)) * handedness;
        float3x3 TBN = float3x3(T, B, N);
        N = normalize(mul(TBN, tangentNormal));
    }

    // Geometric (non-normal-mapped) normal for large-scale effects like Fresnel,
    // refraction and screen-space reflections. This stays stable across pixels
    // on a flat pane and helps avoid flickering when rotating the camera.
    float3 G = normalize(input.GeometricNormal);

    // --- 3. Opaque Lighting Calculation ---
    float3 V = normalize(ubo.camPos.xyz - input.WorldPos);

    float3 diffuseLighting  = float3(0.0, 0.0, 0.0);
    float3 specularLighting = float3(0.0, 0.0, 0.0);

    // Forward+: compute tile id and iterate culled light list
    const uint TILE = 16u; // must match engine configuration
    uint tilesX = (uint(ubo.screenDimensions.x) + TILE - 1u) / TILE;
    uint tilesY = (uint(ubo.screenDimensions.y) + TILE - 1u) / TILE;

    // SV_POSITION in the fragment stage is in window coordinates. Use robust integer index.
    uint px = (uint)max(0.0, input.Position.x);
    uint py = (uint)max(0.0, input.Position.y);
    uint tileX = (tilesX > 0u) ? min(px / TILE, tilesX - 1u) : 0u;
    uint tileY = (tilesY > 0u) ? min(py / TILE, tilesY - 1u) : 0u;
    uint totalTiles = max(tilesX * tilesY, 1u);

    // Clustered Z slice index from view-space depth (positive distance)
    float dVS = abs(mul(ubo.view, float4(input.WorldPos, 1.0)).z);
    float lnN = log(max(ubo.nearZ, 1e-4));
    float lnF = log(max(ubo.farZ,  lnN + 1e-4));
    float denom = max(lnF - lnN, 1e-6);
    float slices = max(ubo.slicesZ, 1.0);
    float lambda = saturate((log(max(dVS, 1e-4)) - lnN) / denom);
    uint slice = (uint)clamp(floor(lambda * slices), 0.0, slices - 1.0);

    uint tileId = (slice * tilesY + tileY) * tilesX + tileX;

    // Use Forward+ by default (fall back to global loop if lists are empty)
    bool useForwardPlus = true;

    uint base = 0u;
    uint count = 0u;
    if (useForwardPlus && tileId < totalTiles * (uint)slices) {
        TileHeader th = tileHeaders[tileId];
        base = th.offset;
        count = th.count;
    }

    bool forceGlobal = false;

    // Accumulate per-light diffuse and specular terms using GGX microfacet BRDF.
    if (useForwardPlus && count > 0) {
        // Use Forward+ culled list
        for (uint li = 0u; li < count; ++li) {
            uint lightIndex = tileLightIndices[base + li];
            LightData light = lightBuffer[lightIndex];
            
            LightEvaluation eval = evaluateLight(light, input.WorldPos, N);
            if (eval.valid) {
                float visibility = 1.0;
                // Raster ray-query shadows are expensive. Shadow only the directional light (sun) for now.
                if (ubo.padding2 != 0.0 && light.lightType == 1) {
                    float tMaxShadow = 10000.0;
                    float3 shadowOrigin = input.WorldPos + N * RASTER_SHADOW_EPS;
                    bool occluded = traceShadowOccluded(shadowOrigin, eval.L, RASTER_SHADOW_EPS, tMaxShadow);
                    visibility = occluded ? 0.0 : 1.0;
                }
                
                eval.radiance *= visibility;
                accumulateLighting(eval, N, V, albedo, metallic, roughness, F0, diffuseLighting, specularLighting);
            }
        }
    }
    // Global light loop (fallback)
    if (count == 0 && ubo.lightCount > 0) {
        for (uint li = 0u; li < (uint)ubo.lightCount; ++li) {
            LightData light = lightBuffer[li];
            LightEvaluation eval = evaluateLight(light, input.WorldPos, N);
            if (eval.valid) {
                 float visibility = 1.0;
                 if (ubo.padding2 != 0.0 && light.lightType == 1) {
                    float3 shadowOrigin = input.WorldPos + N * RASTER_SHADOW_EPS;
                    bool occluded = traceShadowOccluded(shadowOrigin, eval.L, RASTER_SHADOW_EPS, 10000.0);
                    visibility = occluded ? 0.0 : 1.0;
                 }
                 eval.radiance *= visibility;
                 accumulateLighting(eval, N, V, albedo, metallic, roughness, F0, diffuseLighting, specularLighting);
            }
        }
    }

    float3 ambient = albedo * ao * (0.1 * ubo.scaleIBLAmbient);
    float3 opaqueLit = diffuseLighting + specularLighting + ambient + emissive;

    // --- 4. Final Color Assembly (opaque only; transmission handled in GlassPSMain) ---
    float3 color    = opaqueLit;
    float  alphaOut = baseColor.a;

    // Clip-plane discard during reflection render pass (to remove behind-plane geometry)
    if (ubo.reflectionPass == 1) {
        float side = dot(ubo.clipPlaneWS, float4(input.WorldPos, 1.0));
        if (side > 0.0) discard; // discard geometry on the positive side of the plane
    }

    // Note: reflections are only applied in glass path (GlassPSMain). No planar reflection
    // sampling here to avoid banding/aliasing and ensure user-requested behavior.

    // --- 5. Post-Processing ---
    // Output linear color for intermediate buffers (composite pass will tonemap)
    return float4(color, alphaOut);
}

// Fragment shader entry point specialized for architectural glass.
// Shares the same inputs and bindings as PSMain, but uses a much simpler
// and more stable shading model: primarily refraction of the opaque scene
// with a small ambient/emissive surface term. Direct diffuse/specular
// lighting and screen-space reflections are omitted to avoid global
// bright/dark flashes across large glass surfaces.
[[shader("fragment")]]
float4 GlassPSMain(VSOutput input) : SV_TARGET
{
    // --- 1. Material / texture sampling (minimal subset) ---
    float2 uv = float2(input.UV.x, 1.0 - input.UV.y);

    MaterialData mat = materialBuffer[NonUniformResourceIndex(input.MaterialIndex)];
    float4 baseColor = (mat.baseColorTexIndex < 0)
        ? float4(mat.albedo, mat.alpha)
        : globalTextures[NonUniformResourceIndex(mat.baseColorTexIndex)].Sample(uv) * float4(mat.albedo, mat.alpha);

    // Emissive (same logic as PSMain)
    float3 emissiveTex = (mat.emissiveTexIndex < 0)
        ? float3(1.0, 1.0, 1.0)
        : globalTextures[NonUniformResourceIndex(mat.emissiveTexIndex)].Sample(uv).rgb;
    float3 emissiveFactor = mat.emissive;
    float3 emissive = emissiveTex * emissiveFactor;
    if (mat.hasEmissiveStrengthExt != 0)
        emissive *= mat.emissiveStrength;

    // Alpha mask discard as in PSMain
    if (mat.alphaMode == 1 && baseColor.a < mat.alphaCutoff) {
        discard;
    }

    // Geometric normal for view-angle dependence and refraction
    float3 G = normalize(input.GeometricNormal);
    float3 V = normalize(ubo.camPos.xyz - input.WorldPos);

    // Base albedo used for transmission tint
    float3 albedo = baseColor.rgb;

    // Ambient is intentionally disabled for the glass path.
    // Even small ambient terms can make large glass surfaces look "filled in"
    // (frosted/opaque) rather than primarily showing the background through refraction.

    // Transmission factor from push constants.
    // Some assets flag “glass” via engine-side heuristics but may not author
    // `KHR_materials_transmission`. Since this shader is only used for glass,
    // derive a robust effective transmission so glass never goes black.
    float T_auth = clamp(mat.transmissionFactor, 0.0, 1.0);
    float opacity = clamp(baseColor.a, 0.0, 1.0);
    float T_fromAlpha = 1.0 - opacity;
    float T_eff = max(T_auth, T_fromAlpha);
    if (T_eff < 0.01) {
        // Default to mostly transmissive for glass when no explicit transmission/alpha is authored.
        T_eff = 0.90;
    }

    float3 color;
    float alphaOut = baseColor.a;

    if (T_eff > 0.0) {
        // Transmission/background sample (refraction approximation): sample the opaque scene behind glass.
        float2 uvR = input.Position.xy / ubo.screenDimensions;
        uvR = clamp(uvR, float2(0.0, 0.0), float2(1.0, 1.0));
        float3 bg = float3(0,0,0); // opaqueSceneColor not bound in PBR pass
        // Tint the background by albedo to approximate colored glass.
        bg *= lerp(float3(1.0, 1.0, 1.0), max(albedo, 0.6), 0.8);

        // Planar reflection sample (optional)
        float3 refl = bg;
        if (ubo.reflectionEnabled == 1) {
            float4 pr = mul(ubo.reflectionVP, float4(input.WorldPos, 1.0));
            float2 uvP = pr.xy / max(pr.w, 1e-5);
            uvP = uvP * 0.5 + 0.5;
            if (uvP.x >= 0.0 && uvP.x <= 1.0 && uvP.y >= 0.0 && uvP.y <= 1.0) {
                refl = reflectionMap.Sample(uvP).rgb;
            }
        }

        // Stylized, stable glass: Use a tinted
        // glass body + rim highlight, then add planar reflection contribution.

        // Use symmetric |N·V| so that front/back views of thin glass walls
        // behave consistently (important when looking down into glasses).
        float NdotV = abs(dot(G, V));

        // Base clear color from albedo, slightly dimmed so glass does not
        // appear self-emissive.
        float3 clearColor = albedo * 0.6;

        // Rim term stronger at grazing angles (1 - NdotV)^3, but keep it subtle
        float edge = pow(1.0 - NdotV, 3.0);
        float3 rimColor = lerp(clearColor, float3(1.0, 1.0, 1.0), 0.25);

        // Surface term: keep subtle so glass does not appear frosted.
        float3 surfaceBase = emissive;
        float3 surfaceTerm = surfaceBase * (1.0 - T_eff) * 0.12;

        // Base surface appearance (slight body + rim) and transmitted background.
        float3 glassBody = clearColor * 0.08;
        float3 rim       = rimColor * (edge * 0.25);
        float3 surface   = glassBody + rim + surfaceTerm;

        // Primary transmission mix: this is what makes interior lighting visible through windows.
        color = lerp(surface, bg, T_eff);

        // Restore Fresnel-blended mixing with boosted visibility for debugging/tuning.
        float3 F_view2 = FresnelSchlick(NdotV, float3(0.06, 0.06, 0.06));
        float F_avg2 = (F_view2.r + F_view2.g + F_view2.b) / 3.0;
        float reflStrength = saturate(0.20 + (1.5 * F_avg2) * (1.0 - mat.roughness));
        // Scale by user-controlled intensity
        reflStrength *= max(0.0, ubo.reflectionIntensity);
        color = lerp(color, refl, reflStrength);

        // Fresnel influences alpha (how opaque the glass appears), not color here.
        // We already used F to modulate reflection strength above.

        // Opacity model for architectural glass: mostly transparent at
        // normal incidence, with a gentle Fresnel-driven increase in
        // opacity toward grazing angles. TransmissionFactor controls how
        // much of the underlying scene shows through overall.

        // Since we are sampling the background (opaqueSceneColor) and mixing it in the shader,
        // we should output an alpha of 1.0 to ensure our mixed color is shown correctly
        // in the swapchain, avoiding "double blending" with the hardware blender.
        alphaOut = 1.0;
    } else {
        // Non-transmissive fallback: just ambient + emissive.
        color = emissive;
    }

    // Simple Forward+ lighting for glass (additive), using per-tile lists.
    // This is a pragmatic lighting contribution so emissive bulbs can light glass-covered pixels.
    // It does not model full transmission; it simply adds local diffuse+spec highlights.
    {
        const uint TILE = 16u;
        uint tilesX = (uint(ubo.screenDimensions.x) + TILE - 1u) / TILE;
        uint tilesY = (uint(ubo.screenDimensions.y) + TILE - 1u) / TILE;
        uint px = (uint)max(0.0, input.Position.x);
        uint py = (uint)max(0.0, input.Position.y);
        uint tileX = (tilesX > 0u) ? min(px / TILE, tilesX - 1u) : 0u;
        uint tileY = (tilesY > 0u) ? min(py / TILE, tilesY - 1u) : 0u;
        uint totalTiles = max(tilesX * tilesY, 1u);
        uint tileId = tileY * tilesX + tileX;
        uint base = 0u;
        uint count = 0u;
        if (tileId < totalTiles) {
            TileHeader th = tileHeaders[tileId];
            base = th.offset;
            count = th.count;
        }
        if (count > 0u) {
            float3 Ng = normalize(input.GeometricNormal);
            float3 Vv = normalize(ubo.camPos.xyz - input.WorldPos);
            // Use a neutral albedo to avoid darkening glass; weight specular more
            float3 alb = float3(0.6, 0.6, 0.6);
            float  rough = 0.49;
            float  metal = 0.0;
            for (uint li = 0u; li < count; ++li) {
                uint lightIndex = tileLightIndices[base + li];
                LightData light = lightBuffer[lightIndex];
                float3 L, radiance;
                float distToLight = 10000.0;
                if (light.lightType == 1) {
                    L = normalize(-light.position.xyz);
                    radiance = light.color.rgb;
                } else {
                    float3 toLight = light.position.xyz - input.WorldPos;
                    float d = length(toLight);
                    L = (d > 1e-5) ? toLight / d : float3(0,0,1);
                    distToLight = d;
                    if (light.lightType == 3) {
                        float r = max(light.range, 0.001);
                        float att = 1.0 / (1.0 + (d / r) * (d / r));
                        radiance = light.color.rgb * att;
                    } else {
                        radiance = light.color.rgb / max(d * d, 0.0001);
                    }
                }
                float rawDot = dot(Ng, L);
                float NdotL = (light.lightType == 3) ? abs(rawDot) : max(rawDot, 0.0);
                if (NdotL > 0.0) {
                    float visibility = 1.0;
                    if (ubo.padding2 != 0.0) {
                        float tMaxShadow = (light.lightType == 1) ? 10000.0 : max(distToLight - RASTER_SHADOW_EPS, RASTER_SHADOW_EPS);
                        float3 shadowOrigin = input.WorldPos + Ng * RASTER_SHADOW_EPS;
                        bool occluded = traceShadowOccluded(shadowOrigin, L, RASTER_SHADOW_EPS, tMaxShadow);
                        visibility = occluded ? 0.0 : 1.0;
                    }

                    float3 H = normalize(Vv + L);
                    float NdotV = max(dot(Ng, Vv), 0.0);
                    float NdotH = max(dot(Ng, H), 0.0);
                    float HdotV = max(dot(H, Vv), 0.0);
                    float D      = DistributionGGX(NdotH, rough);
                    float G      = GeometrySmith(NdotV, NdotL, rough);
                    float3 F     = FresnelSchlick(HdotV, lerp(float3(0.04,0.04,0.04), alb, metal));
                    float3 spec  = (D * G * F) / max(4.0 * NdotV * NdotL, 0.0001);
                    float3 kD    = (1.0 - F) * (1.0 - metal);
                    // Add a modest contribution to the glass color
                    color += ((kD * alb / PI) * radiance * NdotL * 0.6 + spec * radiance * NdotL * 0.8) * visibility;
                }
            }
        }
    }


    // --- 3. Post-processing (same as PSMain) ---
    color *= ubo.exposure;

    // Uncharted2 / Hable filmic tonemap. Use the canonical form without
    // the extra 1.2 pre-scale so that midtones and shadows are not
    // over-compressed relative to highlights.
    float3 t = Hable_Filmic_Tonemapping::Uncharted2Tonemap(color);
    float3 w = Hable_Filmic_Tonemapping::Uncharted2Tonemap(float3(1,1,1) * Hable_Filmic_Tonemapping::W);
    color = t / max(w, float3(1e-6, 1e-6, 1e-6));

    if (ubo.padding0 == 0) {
        color = pow(saturate(color), float3(1.0 / ubo.gamma));
    } else {
        color = saturate(color);
    }

    return float4(color, alphaOut);
}