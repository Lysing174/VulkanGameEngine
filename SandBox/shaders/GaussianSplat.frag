#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec4 fragColor;      // rgb + base opacity
layout(location = 1) in vec2 fragPosition;    // offset from center (in sqrt8 units)
layout(location = 2) in vec2 fragScreenUV;    // screen UV for depth lookup
layout(location = 3) in vec3 fragViewCenter;   // 椭球中心 E (view space)
layout(location = 4) in vec4 fragInvSigma0;    // Σ_view⁻¹: (M00, M01, M02, M11)
layout(location = 5) in vec4 fragInvSigma1;    // (M12, M22, c=EᵀME - r², _pad)

// 相机矩阵经 push constant 传入 (与 vertex 共用同一 block)
layout(push_constant) uniform CameraData {
    mat4 viewMatrix;
    mat4 projectionMatrix;
} camera;

// Set 0 (GAUSSIAN_SET): 高斯相关资源
// Set 0, Binding 3: Mesh 深度图 (zbuffer)
layout(set = 0, binding = 3) uniform sampler2D u_DepthMap;

layout(location = 0) out vec4 outColor;

void main() {
    // 高斯到中心的平方距离 (sqrt8 单位)
    float A = dot(fragPosition, fragPosition);

    // 丢弃高斯范围外的片元
    if (A > 8.0) discard;

    float sceneDepth = texture(u_DepthMap, fragScreenUV).r;

    // 默认完全可见; 仅在有 mesh 几何的像素上做遮挡
    float occlusion = 1.0;

    if (sceneDepth < 1.0)
    {
        // ===== 重建 view-space mesh 表面点 X_m (透视投影反推) =====
        // clip.z = P[2][2]*view.z + P[3][2],  clip.w = -view.z
        // => ndc.z = -P[2][2] - P[3][2]/view.z  =>  view.z = -P[3][2] / (ndc.z + P[2][2])
        mat4 P = camera.projectionMatrix;
        float p00 = P[0][0], p11 = P[1][1], p22 = P[2][2], p32 = P[3][2];
        vec2 ndcXY = fragScreenUV * 2.0 - 1.0;

        float meshViewZ = -p32 / (sceneDepth + p22);   // < 0
        vec3  meshViewPos = vec3(ndcXY.x * (-meshViewZ) / p00,
                                 ndcXY.y * (-meshViewZ) / p11,
                                 meshViewZ);

        // ===== 视线方向 (相机在原点, 穿过 X_m) =====
        vec3 E = fragViewCenter;
        vec3 D = meshViewPos;                          // 相机→片元的世界方向 (未归一化)
        float Dlen = length(D);
        vec3 Dhat = D / max(Dlen, 1e-6);               // 单位视线

        // ===== 完整二次方程: a t² - 2b t + c = 0 =====
        // 其中 a = D̂ᵀ M D̂,  b = D̂ᵀ M E,  c = Eᵀ M E - r² (c 由 vert 预算传入)
        float M00 = fragInvSigma0.x, M01 = fragInvSigma0.y, M02 = fragInvSigma0.z, M11 = fragInvSigma0.w;
        float M12 = fragInvSigma1.x, M22 = fragInvSigma1.y;
        float c_quad = fragInvSigma1.z;

        vec3 MDhat = vec3(M00*Dhat.x + M01*Dhat.y + M02*Dhat.z,
                          M01*Dhat.x + M11*Dhat.y + M12*Dhat.z,
                          M02*Dhat.x + M12*Dhat.y + M22*Dhat.z);
        float a_quad = dot(Dhat, MDhat);
        vec3 ME     = vec3(M00*E.x + M01*E.y + M02*E.z,
                           M01*E.x + M11*E.y + M12*E.z,
                           M02*E.x + M12*E.y + M22*E.z);
        float b_quad = dot(Dhat, ME);

        // ===== 判别式与两根 (t_near < t_far, 沿视线方向) =====
        float disc = b_quad * b_quad - a_quad * c_quad;
        if (disc < 0.0 || a_quad <= 0.0)
        {
            // 视线不交椭球 → 此片元完全不被该高斯覆盖 (虽然在广告牌内)
            discard;
        }
        float sqrtDisc = sqrt(disc);
        float t_near = (b_quad - sqrtDisc) / a_quad;   // 更小 (近)
        float t_far  = (b_quad + sqrtDisc) / a_quad;   // 更大 (远)

        // ===== 椭球两交点的 view-space 深度 =====
        // P(t) = t·D̂,  z = t·D̂.z (D̂.z<0 → t 越大 z 越小越远)
        float z_near = t_near * Dhat.z;   // 椭球近端深度 (更大, 更靠近相机)
        float z_far  = t_far  * Dhat.z;   // 椭球远端深度 (更小, 更远)

        // ===== 与 mesh 深度比较, 算未被遮挡的比例 =====
        // mesh 在 meshViewZ; 椭球占据 [z_far, z_near]
        // visibility = clamp((z_near - mesh_z) / (z_near - z_far), 0, 1)
        float zRange = z_near - z_far;
        occlusion = clamp((z_near - meshViewZ) / max(zRange, 1e-6), 0.0, 1.0);

        // mesh 在椭球近端之前 (meshViewZ >= z_near) → 完全遮挡
        // mesh 在椭球远端之后 (meshViewZ <= z_far)  → 完全可见
        // 中间 → 线性过渡
    }

    // 基础高斯 opacity * 遮挡
    float opacity = exp(-0.5 * A) * fragColor.a;
    opacity *= occlusion;

    outColor = vec4(fragColor.rgb, opacity);
}
