#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D u_AlbedoMap;
layout(set = 0, binding = 5) uniform MaterialUBO {
    vec4 AlbedoColor;
    float Metalness;
    float Roughness;
    float EmissiveIntensity;
    float AOStrength;
    vec4 EmissiveColor;
    int HasAlbedoMap;
    int HasMetalRoughnessMap;
    int HasNormalMap;
    int HasEmissiveMap;
    int HasAoMap;
} u_Material;

void main() {
    vec4 color = u_Material.AlbedoColor;
    if (u_Material.HasAlbedoMap != 0)
    {
        // PyTorch3D align_corners=True maps UV endpoints to edge texel centers.
        vec2 size = vec2(textureSize(u_AlbedoMap, 0));
        vec2 uv = (fragTexCoord * (size - 1.0) + 0.5) / size;
        color = texture(u_AlbedoMap, uv);
    }

    // Keep SuGaR's raw RGB values; quantization happens only after composition.
    outColor = color;
}
