#version 450
#extension GL_ARB_separate_shader_objects : enable

const float PI = 3.14159265359;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in mat3 fragTBN;

// Global UBO: projView (vertex) + camera/light data (fragment)
layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 projView;
    vec4 cameraPosition;        // xyz = camera world pos
    vec4 lightPosition;          // xyz = light world pos
    vec4 lightColorIntensity;    // rgb = light color, a = intensity
} u_Global;

// Material descriptor set (set=1)
layout(set = 1, binding = 0) uniform sampler2D u_AlbedoMap;
layout(set = 1, binding = 1) uniform sampler2D u_NormalMap;
layout(set = 1, binding = 2) uniform sampler2D u_MetalRoughAO;
layout(set = 1, binding = 3) uniform sampler2D u_EmissiveMap;
layout(set = 1, binding = 4) uniform MaterialUBO {
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
} u_Material;

layout(location = 0) out vec4 outColor;

// ---------------------------------------------------------------------------
// PBR Functions (Cook-Torrance BRDF)
// ---------------------------------------------------------------------------

// Fresnel-Schlick
vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// GGX Normal Distribution Function
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 0.001);
}

// Schlick-GGX Geometry Function (for one direction)
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Smith Geometry Function
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

vec3 ACESFilm(vec3 x) {
    float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
void main()
{
    // 1. Albedo: sample texture if present, otherwise use UBO color directly
    //    纹理以 VK_FORMAT_*_SRGB 格式创建，硬件自动完成 sRGB→线性 转换
    vec3 albedo;
    if (u_Material.HasAlbedoMap == 1)
    {
        vec4 albedoSample = texture(u_AlbedoMap, fragTexCoord);
        albedo = albedoSample.rgb * u_Material.AlbedoColor.rgb;
    }
    else
    {
        albedo = u_Material.AlbedoColor.rgb;
    }


    // 2. ORM (R = AO, G = Roughness, B = Metalness)
    //    Sample texture if present, otherwise use UBO factors directly
    float ao = 1.0;
    float roughness = u_Material.Roughness;
    float metallic  = u_Material.Metalness;

    if (u_Material.HasMetalRoughnessMap == 1)
    {
        vec4 mra = texture(u_MetalRoughAO, fragTexCoord);
        ao        = mra.r;
        roughness = mra.g * u_Material.Roughness;
        metallic  = mra.b * u_Material.Metalness;
    }

    // Clamp roughness to avoid artifacts
    roughness = clamp(roughness, 0.04, 1.0);

    // 3. Compute normal (With Normal Mapping into World Space)
    vec3 N = normalize(fragNormal); // 基础世界空间几何法线

    // 只有当材质启用了法线贴图时才进行空间转换
    if (u_Material.HasNormalMap == 1)
    {
        // 3.1 采样法线贴图并解包到 [-1, 1] 范围
        vec3 tangentNormal = texture(u_NormalMap, fragTexCoord).xyz * 2.0 - 1.0;

        // 3.2 使用顶点插值的 TBN 矩阵将切线空间法线转换到世界空间
        N = normalize(fragTBN * tangentNormal);
    }
    
    
    // 4. Compute directions
    vec3 V = normalize(u_Global.cameraPosition.xyz - fragWorldPos);
    vec3 L = normalize(u_Global.lightPosition.xyz - fragWorldPos);
    vec3 H = normalize(V + L);

    
    // 5. Light radiance with distance attenuation (inverse square)
    float dist = length(u_Global.lightPosition.xyz - fragWorldPos);
    float attenuation = 1.0 / (dist * dist + 0.001);
    vec3 radiance = u_Global.lightColorIntensity.rgb * u_Global.lightColorIntensity.a * attenuation;

    // 6. Fresnel: F0 mix based on metalness
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // 7. Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
    vec3 specular = numerator / denominator;

    // Energy conservation: kD = diffuse component
    vec3 kD = (1.0 - F) * (1.0 - metallic);

    // Lambertian diffuse
    float NdotL = max(dot(N, L), 0.0);

    // 8. Combine
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    // 9. Ambient (using AO from texture)
    float ambientAO = mix(1.0, ao, u_Material.AOStrength);
    vec3 ambient = vec3(0.03) * albedo * ambientAO;

    // 10. Emissive
    vec3 emissive = u_Material.EmissiveColor.rgb * u_Material.EmissiveIntensity;
    if (u_Material.HasEmissiveMap == 1)
    {
        vec4 emissiveSample = texture(u_EmissiveMap, fragTexCoord);
        emissive = emissiveSample.rgb * u_Material.EmissiveColor.rgb * u_Material.EmissiveIntensity;
    }

    // 11. Final color
    vec3 color = ambient + Lo + emissive;

    // Tone mapping (Reinhard)
    color = color / (color + vec3(1.0));
    
    // 替换原来的 Reinhard
    //color = ACESFilm(color);

    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));
    
    //outColor = vec4(worldPos,1.0f);
    //outColor = vec4(fragNormal/2.0+vec3(0.5,0.5,0.5),1.0);
    //outColor = vec4(fragNormal.x,fragNormal.y,0.0,1.0);
    //outColor = vec4(N,1.0f);
    // outColor=vec4(fragTexCoord.y,fragTexCoord.x,0.0,1.0);
    //outColor = vec4(roughness,0,0,1.0);
    //outColor = vec4(0,metallic,0,1.0);
    //outColor = pow(outColor, vec4(1.0 / 2.2));
    //outColor = vec4(vec3(D),1.0);
    //outColor = vec4(mat.baseColorFactor.rgb,1.0f);
    //outColor = vec4(F0,1.0);
    //outColor = vec4(F,1.0);
    //outColor = vec4(F,1.0);
    //outColor = vec4(vec3(NdotH),1.0);
    outColor = vec4(color, 1.0);
}

