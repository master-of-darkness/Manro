## Plan: Sponza and Engine Optimizations

Optimierung der Ladezeiten durch binäre Formate und der Rendering-Dauer durch fortgeschrittenes Culling sowie effizienteres Speichermanagement. Die Engine nutzt bereist vorteilhaft Compute-Frustum-Culling und Bindless Textures, kann jedoch noch weiter ausgebaut werden.

### Steps
2. Erweitere `mesh_cull.slang` um **Occlusion Culling** (Hi-Z Map) und **LOD-Auswahl** basierend auf Kamera-Distanz, um noch vor dem Fragment-Shader unsichtbare Geometrie zu verwerfen.
3. Nutze [KTX2 / BCn komprimierte Texturen](src/Resource/TextureLoader.cpp) anstelle von PNGs via `stb_image`, um VRAM-Bandbreite und den Transfer über den PCI-e-Bus drastisch zu reduzieren.
4. Implementiere Multi-Draw-Indirect in [Renderer::RenderQueue](src/Render/Renderer.cpp) (über `vkCmdDrawIndexedIndirect`), falls aktuell noch Loop-basiert gezeichnet wird, was den CPU-Overhead senkt.
5. Parallelisiere den Iterationsprozess in `Model::Load`, sodass das Hochladen der Texturen (`TextureLoader::Load`) parallel zum Laden der Meshes (`ModelLoader::LoadSubMeshes`) gestartet und via `JobSystem` orchestriert wird.

### Further Considerations
1. Sollen wir zuerst die Ladezeiten (OBJ -> GLB) oder die Rendering-Framerate (Occlusion Culling) in Angriff nehmen?
2. Sollen Vulkan Mesh Shaders (Task/Mesh) evaluiert werden, um den Compute-Culling-Ansatz zukünftig komplett zu ersetzen?

