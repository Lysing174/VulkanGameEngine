#version 450
#extension GL_ARB_separate_shader_objects : enable

// Set 0: Global UBO (projView - not used by this shader, but required by pipeline layout)
layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 projView;
} globalUbo;

// Set 1, Binding 0: FrameInfo UBO
layout(std140, set = 1, binding = 0) uniform FrameInfo {
    mat4 viewMatrix;
    mat4 projectionMatrix;
    vec4 cameraPosAndScale;  // xyz = camera position, w = splatScale
    vec4 focal;              // xy = focal length (fx, fy)
    vec4 viewportInfo;       // xy = viewport size, zw = basisViewport
    vec4 extraInfo;          // x = alphaCullThreshold
} frameInfo;

// Set 1, Binding 1: Gaussian data SSBO
struct GaussianDataGPU {
    vec4 posAndOpacity;  // xyz = position, w = sigmoid(opacity)
    vec4 color;          // rgb = SH DC color, w = unused
    float cov3d[6];      // symmetric 3x3 upper triangle: M00,M01,M02,M11,M12,M22
    uint modelIndex;     // index into model transform SSBO
    float _pad1;
};

layout(std430, set = 1, binding = 1) readonly buffer GaussianDataBuffer {
    GaussianDataGPU gaussians[];
};

// Set 1, Binding 2: Sorted indices SSBO
layout(std430, set = 1, binding = 2) readonly buffer SortedIndicesBuffer {
    uint sortedIndices[];
};

// Set 1, Binding 4: Model transform SSBO (one mat4 per model)
layout(std430, set = 1, binding = 4) readonly buffer ModelTransformBuffer {
    mat4 modelTransforms[];
};

// Outputs
layout(location = 0) out vec4 fragColor;     // rgb + base opacity
layout(location = 1) out vec2 fragPosition;   // offset from center (in sqrt8 units)
layout(location = 2) out vec2 fragScreenUV;   // screen UV for depth texture lookup

const float sqrt8 = 2.8284271247461903; // sqrt(8)

void main() {
    // Quad vertex positions: 2 triangles forming a quad
    // gl_VertexIndex: 0-5 for each instance
    const vec2 quadPositions[6] = vec2[](
        vec2(-1, -1), vec2( 1, -1), vec2(-1,  1),  // triangle 1
        vec2(-1,  1), vec2( 1, -1), vec2( 1,  1)   // triangle 2
    );

    vec2 quadPos = quadPositions[gl_VertexIndex];

    // Get the gaussian index from sorted indices (for back-to-front rendering)
    uint gaussianIdx = sortedIndices[gl_InstanceIndex];
    // Bounds check to prevent GPU fault from invalid sort output
    if (gaussianIdx >= gaussians.length()) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        fragColor = vec4(0.0);
        fragPosition = vec2(0.0);
        return;
    }
    GaussianDataGPU g = gaussians[gaussianIdx];

    // Get model transform for this gaussian
    mat4 modelTransform = modelTransforms[g.modelIndex];

    // World-space position (apply model transform)
    vec3 splatCenter = (modelTransform * vec4(g.posAndOpacity.xyz, 1.0)).xyz;

    // Transform to view space
    vec4 viewCenter = frameInfo.viewMatrix * vec4(splatCenter, 1.0);

    // Frustum culling: discard if behind camera
    if (viewCenter.z > 0.0) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        fragColor = vec4(0.0);
        fragPosition = vec2(0.0);
        return;
    }

    // Clip space position
    vec4 clipCenter = frameInfo.projectionMatrix * viewCenter;

    // Discard if behind near plane
    if (clipCenter.w <= 0.0) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        fragColor = vec4(0.0);
        fragPosition = vec2(0.0);
        return;
    }

    // Alpha culling: skip very transparent gaussians
    if (g.posAndOpacity.w < frameInfo.extraInfo.x) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        fragColor = vec4(0.0);
        fragPosition = vec2(0.0);
        return;
    }

    // ====== 3D Covariance → 2D Covariance Projection ======
    // Reconstruct 3x3 symmetric covariance matrix (local space)
    float cov3D00 = g.cov3d[0], cov3D01 = g.cov3d[1], cov3D02 = g.cov3d[2];
    float cov3D11 = g.cov3d[3], cov3D12 = g.cov3d[4];
    float cov3D22 = g.cov3d[5];
    mat3 Vrk = mat3(
        cov3D00, cov3D01, cov3D02,
        cov3D01, cov3D11, cov3D12,
        cov3D02, cov3D12, cov3D22
    );

    // Jacobian of perspective projection (approximation for preserving 2D Gaussian)
    float fx = frameInfo.focal.x;
    float fy = frameInfo.focal.y;
    float s = 1.0 / (viewCenter.z * viewCenter.z);
    mat3 J = mat3(
        fx / viewCenter.z,  0.0,                -(fx * viewCenter.x) * s,
        0.0,                fy / viewCenter.z,  -(fy * viewCenter.y) * s,
        0.0,                0.0,                0.0
    );

    // W = transpose(mat3(viewMatrix)) — upper-left 3x3 of view matrix
    mat3 W = transpose(mat3(frameInfo.viewMatrix));

    // T = W * J (view-projection Jacobian)
    mat3 T = W * J;

    // Apply model transform to the projection Jacobian:
    // cov2D = T^T * (M3 * Vrk * M3^T) * T = (M3^T * T)^T * Vrk * (M3^T * T)
    mat3 M3 = mat3(modelTransform);
    T = transpose(M3) * T;

    // 2D covariance: cov2D = transpose(T) * Vrk * T + low-pass filter
    mat3 cov2Dm = transpose(T) * Vrk * T;

    // Low-pass filter: add 0.3 to diagonal to avoid aliasing
    cov2Dm[0][0] += 0.3;
    cov2Dm[1][1] += 0.3;

    // ====== Eigenvalue Decomposition of 2x2 Covariance ======
    float a = cov2Dm[0][0];
    float b = cov2Dm[0][1];
    float d = cov2Dm[1][1];

    float trace = a + d;
    float traceOver2 = trace * 0.5;
    float det = a * d - b * b;
    float midSquare = max(0.0, traceOver2 * traceOver2 - det);
    float term2 = sqrt(midSquare);

    float eigenValue1 = traceOver2 + term2;
    float eigenValue2 = traceOver2 - term2;

    // Discard degenerate gaussians
    if (eigenValue2 <= 0.0) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        fragColor = vec4(0.0);
        fragPosition = vec2(0.0);
        return;
    }

    // Eigenvectors
    vec2 eigenVector1;
    if (abs(b) > 0.001) {
        eigenVector1 = normalize(vec2(eigenValue1 - d, b));
    } else if (a >= d) {
        eigenVector1 = vec2(1.0, 0.0);
    } else {
        eigenVector1 = vec2(0.0, 1.0);
    }
    vec2 eigenVector2 = vec2(-eigenVector1.y, eigenVector1.x);

    // ====== Generate Quad Vertices ======
    float splatScale = frameInfo.cameraPosAndScale.w;
    vec2 basisVector1 = eigenVector1 * splatScale * min(sqrt8 * sqrt(eigenValue1), 2048.0);
    vec2 basisVector2 = eigenVector2 * splatScale * min(sqrt8 * sqrt(eigenValue2), 2048.0);

    // NDC offset
    vec2 basisViewport = frameInfo.viewportInfo.zw;
    vec2 ndcOffset = (quadPos.x * basisVector1 + quadPos.y * basisVector2) * basisViewport * 2.0;

    vec2 ndcCenter = clipCenter.xy / clipCenter.w;
    gl_Position = vec4(ndcCenter + ndcOffset, clipCenter.z / clipCenter.w, 1.0);

    // Pass to fragment shader
    fragColor = vec4(g.color.rgb, g.posAndOpacity.w); // rgb + base opacity
    fragPosition = quadPos * sqrt8; // scaled for gaussian evaluation in fragment

    // Screen UV for depth texture lookup (occlusion test)
    vec2 ndcPos = gl_Position.xy / gl_Position.w;
    fragScreenUV = ndcPos * 0.5 + 0.5;
}
