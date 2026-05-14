#version 450
#extension GL_ARB_separate_shader_objects : enable

// Set 0: Global UBO (projView matrix)
layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 projView;
} ubo;

// Set 1, Binding 0: Depth map sampler (fragment shader)
// Set 1, Binding 1: Gaussian data SSBO

struct GaussianDataGPU {
    vec3 position;  float _pad0;
    vec3 normal;    float _pad1;
    vec3 shDC;     float opacity;
    vec3 scale;    float _pad2;
    vec4 rotation;
};

layout(std430, set = 1, binding = 1) readonly buffer GaussianDataBuffer {
    GaussianDataGPU gaussians[];
};

layout(push_constant) uniform PushConsts {
    mat4 transform;
} pc;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out float fragNdcDepth;
layout(location = 2) out vec2 fragScreenUV;

out gl_PerVertex {
    vec4 gl_Position;
    float gl_PointSize;
};

void main() {
    uint idx = gl_VertexIndex;
    GaussianDataGPU g = gaussians[idx];

    vec4 worldPos = pc.transform * vec4(g.position.x, -g.position.y,g.position.z,1.0);
    vec4 clipPos = ubo.projView * worldPos;

    // Discard points behind the camera
    if (clipPos.w <= 0.0) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        gl_PointSize = 0.0;
        fragColor = vec3(0.0);
        fragNdcDepth = 1.0;
        fragScreenUV = vec2(0.0);
        return;
    }

    gl_Position = clipPos;
    gl_PointSize = 3.0;

    // SH DC to color: C0 = 0.28209479177
    fragColor = 0.28209479177 * g.shDC + 0.5;

    // NDC depth for depth comparison in fragment shader
    fragNdcDepth = clipPos.z / clipPos.w;

    // Screen UV for depth map lookup
    vec2 ndc = clipPos.xy / clipPos.w;
    fragScreenUV = ndc * 0.5 + 0.5;
}

// === Previous depth visualization code (preserved) ===
// layout(location = 0) out vec2 fragTexCoord;
//
// const vec2 positions[6] = vec2[](
//     vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
//     vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
// );
//
// void main() {
//     vec2 pos = positions[gl_VertexIndex];
//     vec2 screenPos = pc.rectOffset + pos * pc.rectScale;
//     gl_Position = vec4(screenPos * 2.0 - 1.0, 0.0, 1.0);
//     fragTexCoord = pos;
// }
