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
// Combined vertex and fragment shader for textured mesh rendering
// This shader provides basic textured rendering with simple lighting
import common_types;

// Input from vertex buffer
struct VSInput {
    [[vk::location(0)]] float3 Position;
    [[vk::location(1)]] float3 Normal;
    [[vk::location(2)]] float2 TexCoord;
    [[vk::location(3)]] float4 Tangent;

    // Per-instance data. Model matrix is a true 4x4, while the normal
    // matrix is provided as three float4 columns that match the CPU
    // layout (glm::mat3x4: 3 columns of vec4, xyz used, w unused).
    [[vk::location(4)]] column_major float4x4 InstanceModelMatrix;
    [[vk::location(8)]] float4 InstanceNormal0;                     // normal matrix column 0
    [[vk::location(9)]] float4 InstanceNormal1;                     // normal matrix column 1
    [[vk::location(10)]] float4 InstanceNormal2;                    // normal matrix column 2
    [[vk::location(11)]] uint MaterialIndex;                        // material index
};

// Output from vertex shader / Input to fragment shader
struct VSOutput {
    float4 Position : SV_POSITION;
    float3 WorldPos;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Tangent : TANGENT; 
    nointerpolation uint MaterialIndex : MATERIAL_INDEX;
};

// Bindings
[[vk::binding(0, 0)]] ConstantBuffer<UniformBufferObject> ubo;
[[vk::binding(1, 0)]] StructuredBuffer<MaterialData> materialBuffer;
[[vk::binding(0, 1)]] Sampler2D texTable[];

// Vertex shader entry point
[[shader("vertex")]]
VSOutput VSMain(VSInput input)
{
    VSOutput output;

    float4x4 model = input.InstanceModelMatrix;

    float4 worldPos = mul(model, float4(input.Position, 1.0));

    output.Position = mul(ubo.proj, mul(ubo.view, worldPos));

    output.WorldPos = worldPos.xyz;

    float3x3 normalMatrix = float3x3(
        input.InstanceNormal0.xyz,
        input.InstanceNormal1.xyz,
        input.InstanceNormal2.xyz
    );
    
    output.Normal = normalize(mul(normalMatrix, input.Normal));
    output.TexCoord = input.TexCoord;
    output.Tangent = input.Tangent;
    output.MaterialIndex = input.MaterialIndex;

    return output;
}

// Fragment shader entry point
[[shader("fragment")]]
float4 PSMain(VSOutput input) : SV_TARGET
{
    MaterialData mat = materialBuffer[NonUniformResourceIndex(input.MaterialIndex)];
    int texIndex = mat.baseColorTexIndex;

    // Sample the texture with flipped V coordinate (glTF UV origin vs Vulkan)
    float2 uv = float2(input.TexCoord.x, 1.0 - input.TexCoord.y);
    
    float4 texColor = float4(1, 1, 1, 1);
    if (texIndex >= 0) {
        texColor = texTable[NonUniformResourceIndex(texIndex)].Sample(uv);
    } else {
        // Debug color: magenta/purple for missing texture
        // Alternating pattern or just solid
        texColor = float4(0.8, 0.2, 0.8, 1.0);
    }

    // Diagnostic: Visualize UVs
    // return float4(input.TexCoord, 0, 1);
    
    // Diagnostic: Visualize Material Index
    // return float4((float)(input.MaterialIndex % 10) / 10.0, (float)(input.MaterialIndex / 10) / 10.0, 0, 1);

    // Diagnostic: Visualize texIndex
    // if (texIndex < 0) return float4(1, 0, 0, 1); // Red if no texture

    // Simple directional lighting
    float3 lightDir = normalize(float3(0.5, 1.0, 0.3)); // Fixed light direction
    float3 normal = normalize(input.Normal);
    float lightIntensity = max(dot(normal, lightDir), 0.2); // Minimum ambient of 0.2

    // Apply color and intensity
    float3 color = texColor.rgb * mat.albedo * lightIntensity;
    
    return float4(color, texColor.a * mat.alpha);
}
