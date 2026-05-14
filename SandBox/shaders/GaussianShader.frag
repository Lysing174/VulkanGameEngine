#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragColor;
layout(location = 1) in float fragNdcDepth;
layout(location = 2) in vec2 fragScreenUV;

// Depth texture sampler (set=1, binding=0)
layout(set = 1, binding = 0) uniform sampler2D u_DepthMap;

layout(location = 0) out vec4 outColor;

void main() {
    // Sample the depth buffer at this fragment's screen position
    float sceneDepth = texture(u_DepthMap, fragScreenUV).r;

    // If the Gaussian point is behind the scene geometry, discard
    // (small bias to avoid z-fighting)
    if (fragNdcDepth > sceneDepth) {
        discard;
    }

    outColor = vec4(fragColor, 1.0);
}

// === Previous depth visualization code (preserved) ===
// layout(location = 0) in vec2 fragTexCoord;
//
// void main() {
//     float depth = texture(u_DepthMap, fragTexCoord).r;
//     float mask = depth < 0.999 ? 1.0 : 0.0;
//     float n = 0.1; float f = 100.0;
//     float linearDepth = (2.0 * n) / (f + n - depth * (f - n));
//     outColor = vec4(vec3(mask * (1.0 - linearDepth)), 1.0);
// }
