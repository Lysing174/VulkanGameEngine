#version 450
#extension GL_ARB_separate_shader_objects : enable

const float PI = 3.14159265359;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in mat3 fragTBN;

// Global UBO: projView (vertex) + camera (fragment)
layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 projView;
    vec4 cameraPosition;        // xyz = camera world pos
} u_Global;

// Light SSBO: all scene lights (direction & point)
struct Light {
    vec4 PositionType;    // xyz=position(point) or 0(directional), w=type: 0=point, 1=directional
    vec4 ColorIntensity;  // rgb=color, a=intensity
    vec4 DirectionRange;  // xyz=direction(directional), w=range(point)
};

layout(set = 0, binding = 1) readonly buffer LightBuffer {
    Light lights[];
} u_LightBuffer;

// SH Irradiance coefficients (3-band, 9 vec4)
layout(set = 0, binding = 2) uniform SHBuffer {
    vec4 SH[9];
} u_SH;

// Prefiltered environment cubemap (GGX mip chain)
layout(set = 0, binding = 3) uniform samplerCube u_PrefilteredEnvMap;

// Per-object push constants (Fragment stage only)
layout(push_constant) uniform FragPushConsts {
    layout(offset = 64) vec4 pcAlbedoColor;
    layout(offset = 80) uint pcLightCount;
    layout(offset = 84) uint pcLightIndices[4];
};

// Material descriptor set (set=1)
layout(set = 1, binding = 0) uniform sampler2D u_AlbedoMap;
layout(set = 1, binding = 1) uniform sampler2D u_NormalMap;
layout(set = 1, binding = 2) uniform sampler2D u_MetalRoughAO;
layout(set = 1, binding = 3) uniform sampler2D u_AOMap;
layout(set = 1, binding = 4) uniform sampler2D u_EmissiveMap;
layout(set = 1, binding = 5) uniform MaterialUBO {
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
// IBL Functions
// ---------------------------------------------------------------------------

// Analytical environment BRDF (Lazarov fit) — replaces lookup table
vec3 EnvDFGLazarov(vec3 specularColor, float gloss, float NdotV)
{
    vec4 p0 = vec4(0.5745, 1.548, -0.02397, 1.301);
    vec4 p1 = vec4(0.5753, -0.2511, -0.02066, 0.4755);
    vec4 t = gloss * p0 + p1;
    float bias = clamp(t.x * min(t.y, exp2(-7.672 * NdotV)) + t.z, 0.0, 1.0);
    float delta = clamp(t.w, 0.0, 1.0);
    float scale = delta - bias;
    bias *= clamp(50.0 * specularColor.y, 0.0, 1.0);
    return specularColor * scale + bias;
}

// Reconstruct irradiance from 3-band SH coefficients given a normal direction.
// Standard real SH basis (normalized).
vec3 EvalSHIrradiance(vec3 n)
{
    // Band 0
    vec3 result = 0.282095 * u_SH.SH[0].rgb;

    // Band 1
    result += 0.488603 * (n.y * u_SH.SH[1].rgb + n.z * u_SH.SH[2].rgb + n.x * u_SH.SH[3].rgb);

    // Band 2
    float xx = n.x * n.x;
    float yy = n.y * n.y;
    float zz = n.z * n.z;
    float xy = n.x * n.y;
    float yz = n.y * n.z;
    float xz = n.x * n.z;

    result += 1.092548 * xy * u_SH.SH[4].rgb;                             // L2m2
    result += 1.092548 * yz * u_SH.SH[5].rgb;                             // L2m1
    result += 0.315392 * (3.0 * zz - 1.0) * u_SH.SH[6].rgb;              // L20
    result += 1.092548 * xz * u_SH.SH[7].rgb;                             // L2p1
    result += 0.546274 * (xx - yy) * u_SH.SH[8].rgb;                     // L2p2

    return max(result, vec3(0.0));
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


    // 2. ORM & AO
    //    - u_AOMap (R=AO)                   — 独立 AO 贴图, 优先
    //    - u_MetalRoughAO (R=AO, G=Roughness, B=Metallic) — ORM 打包贴图
    //    - UBO 默认值                         — 无贴图时使用
    float ao = 1.0;
    float roughness = u_Material.Roughness;
    float metallic  = u_Material.Metalness;

    if (u_Material.HasAoMap == 1)
    {
        ao = texture(u_AOMap, fragTexCoord).r;
    }
    else if (u_Material.HasMetalRoughnessMap == 1)
    {
        ao = 1.0f-texture(u_MetalRoughAO, fragTexCoord).r;
    }

    if (u_Material.HasMetalRoughnessMap == 1)
    {
        vec4 mra = texture(u_MetalRoughAO, fragTexCoord);
        roughness = mra.g * u_Material.Roughness;
        metallic  = mra.b * u_Material.Metalness;
    }

    // Clamp roughness to avoid artifacts
    roughness = clamp(roughness, 0.04, 1.0);

    // 3. Compute normal (With Normal Mapping into World Space)
    vec3 N = normalize(fragNormal); // 基础世界空间几何法线
    vec3 tangentNormal;
    // 只有当材质启用了法线贴图时才进行空间转换
    if (u_Material.HasNormalMap == 1)
    {
        // 3.1 采样法线贴图并解包到 [-1, 1] 范围
        tangentNormal = texture(u_NormalMap, fragTexCoord).xyz * 2.0 - 1.0;
        //tangentNormal.y = -tangentNormal.y;

        // 3.2 使用顶点插值的 TBN 矩阵将切线空间法线转换到世界空间
        N = normalize(fragTBN * tangentNormal);
    }
    
    
    // 4. Compute view direction
    vec3 V = normalize(u_Global.cameraPosition.xyz - fragWorldPos);

    // 5. Fresnel: F0 mix based on metalness
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // 6. Multi-light accumulation
    vec3 Lo = vec3(0.0);

    for (uint i = 0; i < pcLightCount && i < 4; i++)
    {
        uint lightIdx = pcLightIndices[i];

        // Note: SSBO array access must be within bounds; 0 lights empty is handled by pcLightCount=0
        float lightType = u_LightBuffer.lights[lightIdx].PositionType.w;
        vec3 lightColor = u_LightBuffer.lights[lightIdx].ColorIntensity.rgb;
        float intensity = u_LightBuffer.lights[lightIdx].ColorIntensity.a;

        vec3 L;
        float attenuation = 1.0;

        if (lightType < 0.5) // Point light
        {
            vec3 lightPos = u_LightBuffer.lights[lightIdx].PositionType.xyz;
            float range = u_LightBuffer.lights[lightIdx].DirectionRange.w;

            vec3 toLight = lightPos - fragWorldPos;
            float dist = length(toLight);
            L = normalize(toLight);

            // Inverse square attenuation with range limit
            float att = 1.0 / (dist * dist + 0.001);
            // Smooth fade to zero at range boundary
            float rangeAtt = clamp(1.0 - (dist / max(range, 0.001)), 0.0, 1.0);
            attenuation = att * rangeAtt;
        }
        else // Directional light
        {
            L = normalize(-u_LightBuffer.lights[lightIdx].DirectionRange.xyz);
            attenuation = 1.0;
        }

        vec3 radiance = lightColor * intensity * attenuation;

        // Cook-Torrance BRDF
        vec3 H = normalize(V + L);
        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
        vec3 specular = numerator / denominator;

        // Energy conservation
        vec3 kD = (1.0 - F) * (1.0 - metallic);

        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    // -------------------------------------------------------------------
    // 7. Image-Based Lighting (IBL)
    // -------------------------------------------------------------------
    float NdotV = max(dot(N, V), 0.0);
    float ambientAO = mix(1.0, ao, u_Material.AOStrength);//ao有问题

    // Diffuse IBL: SH irradiance
    vec3 irradiance = EvalSHIrradiance(N);
    vec3 diffuseIBL = irradiance * albedo * ambientAO;

    // Specular IBL: prefiltered cubemap + analytical BRDF
    vec3 R = reflect(-V, N);
    float maxMipLevel = float(textureQueryLevels(u_PrefilteredEnvMap) - 1);
    vec3 prefilteredColor = textureLod(u_PrefilteredEnvMap, R, roughness * maxMipLevel).rgb;

    float gloss = 1.0 - roughness;  // perceptual roughness → gloss
    vec3 envBRDF = EnvDFGLazarov(F0, gloss, NdotV);
    vec3 specularIBL = prefilteredColor * envBRDF * ambientAO;

    // Energy conservation: modulate diffuse by (1 - envBRDF)
    vec3 kD_ibl = (1.0 - envBRDF) * (1.0 - metallic);
    vec3 ambient = kD_ibl * diffuseIBL + specularIBL;

    // -------------------------------------------------------------------
    // 8. Emissive
    // -------------------------------------------------------------------
    vec3 emissive = u_Material.EmissiveColor.rgb * u_Material.EmissiveIntensity;
    if (u_Material.HasEmissiveMap == 1)
    {
        vec4 emissiveSample = texture(u_EmissiveMap, fragTexCoord);
        emissive = emissiveSample.rgb * u_Material.EmissiveColor.rgb * u_Material.EmissiveIntensity;
    }

    // -------------------------------------------------------------------
    // 9. Final color
    // -------------------------------------------------------------------
    vec3 color = ambient + Lo + emissive;

    // Tone mapping (Reinhard)
    color = color / (color + vec3(1.0));

    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));
    
    //outColor = vec4(worldPos,1.0f);
    //outColor = vec4(tangentNormal/2.0+vec3(0.5,0.5,0.5),1.0);
    //outColor = vec4(abs(N.x),abs(N.y),abs(N.z),1.0);
    //outColor = vec4(N,1.0f);
    //outColor=vec4(fragTexCoord.y,0.0,0.0,1.0);
    //outColor = vec4(roughness,0,0,1.0);
    //outColor = vec4(0,metallic,0,1.0);
    //outColor = pow(outColor, vec4(1.0 / 2.2));
    //outColor = vec4(vec3(D),1.0);
    //outColor = vec4(mat.baseColorFactor.rgb,1.0f);
    //outColor = vec4(F0,1.0);
    //outColor = vec4(F,1.0);
    //outColor = vec4(F,1.0);
    //outColor = vec4(vec3(NdotH),1.0);
    //outColor = vec4(ao,0.0,0.0, 1.0);
    outColor = vec4(color, 1.0);
}

