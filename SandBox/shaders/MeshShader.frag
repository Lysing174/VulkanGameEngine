#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 fragTexCoord;

layout(set = 1,binding = 0) uniform sampler2D u_AlbedoMap;
layout(set = 1,binding = 1) uniform sampler2D u_NormalMap;
layout(set = 1,binding = 2) uniform sampler2D u_MetalRoughAO;
layout(set = 1,binding = 3) uniform MaterialUBO {
    vec4 AlbedoColor;
    
    float Metalness;
    float Roughness;
    float EmissiveIntensity;
    float AOStrength;
    
    vec4 EmissiveColor; // 对应 C++ 的 vec4
    
    int UseNormalMap;
    int HasAlbedoMap;
    int HasMetalRoughnessMap;
    int HasNormalMap;
} u_Material;

layout(location = 0) out vec4 outColor;

void main() {
    // 1. 采样基础颜色
    vec4 albedo = texture(u_AlbedoMap, fragTexCoord);

    // 2. Alpha discard (支持 MTL 的 d=0 透明度)
    if (u_Material.AlbedoColor.a < 0.01) {
        discard;
    }
    
    // 3. 采样混合纹理
        vec4 mra = texture(u_MetalRoughAO, fragTexCoord);
        
        // 4. 拆分通道 (根据你的打包顺序，这里假设是 ORM: R=AO, G=Rough, B=Metal)
        float ao        = mra.r; 
        float roughness = mra.g;
        float metallic  = mra.b;

    // 3. 最终输出
    outColor = albedo * u_Material.AlbedoColor;
}
