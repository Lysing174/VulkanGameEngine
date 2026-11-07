#version 450
#extension GL_ARB_separate_shader_objects : enable

// 相机矩阵经 push constant 传入 (与 fragment 共用同一 block; fragment 用于深度重建)
layout(push_constant) uniform CameraData {
    mat4 viewMatrix;
    mat4 projectionMatrix;
} camera;

// Set 0 (GAUSSIAN_SET): 高斯相关资源
// 投影/视锥剔除已在 PrefixCull compute 阶段完成, 本 VS 仅做 quad 展开 + 透传.

// Set 0, Binding 2: 排序后的索引 SSBO (值为原始高斯 ID)
layout(std430, set = 0, binding = 2) readonly buffer SortedIndicesBuffer {
    uint sortedIndices[];
};

layout(std140, set = 0, binding = 0) uniform FrameInfo {
    vec4 cameraPosAndScale;
    vec4 focal;
    vec4 viewportInfo;
    vec4 extraInfo;
} frameInfo;

// Set 0, Binding 5: PrefixCull projected splats (128 bytes per original Gaussian).
struct ProjectedSplatGPU {
    vec4 ndcCenter;   // xyz = center/depth; w = 1
    vec4 ndcBasis;    // xy = first basis; zw = second basis, in NDC units
    vec4 color;       // rgb + base opacity
    vec4 viewCenter;  // xyz = view-space center; w = unused
    vec4 quadCenter;  // xy = quad center in NDC; z = needs tile-domain rejection
    vec4 tileBounds;  // original tile candidate rectangle in NDC
    vec4 conic;       // inverse screen covariance: (xx, xy, yy); w = conservative power cutoff
    vec4 ndcBounds;   // conservative contributing-pixel AABB, within the original tile bounds
};
layout(std430, set = 0, binding = 5) readonly buffer ProjectedSplatBuffer {
    ProjectedSplatGPU projectedSplats[];
};

// Outputs (与 GaussianSplat.frag 严格对应)
layout(location = 0) flat out vec4 fragColor; // rgb + base opacity, constant per Gaussian
layout(location = 1) flat out vec2 fragCenterPixel; // center in framebuffer pixel coordinates
layout(location = 2) out vec2 fragScreenUV;   // screen UV for depth texture lookup
layout(location = 3) flat out vec3 fragViewCenter;  // 椭球中心 E (view space)
layout(location = 4) flat out vec4 fragInvSigma0;   // Σ_view⁻¹ 对称 6 元前 4 个
layout(location = 5) flat out vec4 fragInvSigma1;   // (M12, M22, c=EᵀME - r², _pad)
layout(location = 6) flat out vec4 fragConic;
layout(location = 7) flat out vec4 fragBounds; // original tile bounds in pixels; zero disables the test

const float sqrt8 = 2.8284271247461903; // sqrt(8)

void main() {
    const vec2 quadPositions[6] = vec2[](
        vec2(-1, -1), vec2( 1, -1), vec2(-1,  1),  // triangle 1
        vec2(-1,  1), vec2( 1, -1), vec2( 1,  1)   // triangle 2
    );
    vec2 quadPos = quadPositions[gl_VertexIndex];

    // 安全初值 (不可见/越界高斯也需有限输出, 避免片段着色器 NaN)
    fragViewCenter  = vec3(0.0, 0.0, -1.0);
    fragInvSigma0   = vec4(1.0, 0.0, 0.0, 1.0);
    fragInvSigma1   = vec4(0.0, 1.0, 1.0, 0.0);
    fragScreenUV    = vec2(0.0);
    fragConic      = vec4(0.0);
    fragBounds     = vec4(0.0);

    uint gaussianIdx = sortedIndices[gl_InstanceIndex];
    if (gaussianIdx >= projectedSplats.length()) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        fragColor = vec4(0.0);
        fragCenterPixel = vec2(0.0);
        return;
    }

    ProjectedSplatGPU p = projectedSplats[gaussianIdx];

    /* Original rotated sqrt(8) quad, retained for comparison.
    // NDC 偏移 = quadPos.x * basis1 + quadPos.y * basis2 (basis 已是 NDC 单位, 见 PrefixCull)
    vec2 ndcOffset = quadPos.x * p.ndcBasis.xy + quadPos.y * p.ndcBasis.zw;
    gl_Position = vec4(p.ndcCenter.xy + ndcOffset, p.ndcCenter.z, 1.0);

    fragColor      = p.color;
    fragPosition   = quadPos * sqrt8;                 // scaled for gaussian evaluation in fragment
    */
    vec2 ndcPosition = p.quadCenter.xy + quadPos.x * p.ndcBasis.xy + quadPos.y * p.ndcBasis.zw;
    // LMG does not far-clip splat centers; depth testing is disabled for this pass.
    gl_Position = vec4(ndcPosition, clamp(p.ndcCenter.z, 0.0, 1.0), 1.0);
    fragColor = p.color;
    fragCenterPixel = (p.ndcCenter.xy * 0.5 + 0.5) * frameInfo.viewportInfo.xy;
    fragScreenUV   = gl_Position.xy * 0.5 + 0.5;      // screen UV for depth lookup (w = 1)
    fragViewCenter = p.viewCenter.xyz;
    fragInvSigma0  = vec4(0.0);
    fragInvSigma1  = vec4(0.0);
    fragConic      = p.conic;
    // Most small quads already fit the original tile domain; skip its fragment test.
    fragBounds = p.quadCenter.z != 0.0
        ? (p.tileBounds * 0.5 + 0.5) * frameInfo.viewportInfo.xyxy : vec4(0.0);
}
