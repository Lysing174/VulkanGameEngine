#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec4 fragColor;      // rgb + base opacity
layout(location = 1) in vec2 fragPosition;    // offset from center (in sqrt8 units)
layout(location = 2) in vec2 fragScreenUV;    // screen UV for depth lookup

// Depth texture from mesh render pass (set=1, binding=3)
layout(set = 1, binding = 3) uniform sampler2D u_DepthMap;

layout(location = 0) out vec4 outColor;

void main() {
    // Squared distance from center (already scaled by sqrt8 in vertex shader)
    float A = dot(fragPosition, fragPosition);

    // Discard fragments outside the gaussian (beyond sqrt8 standard deviations)
    if (A > 8.0) discard;

    // Depth test against mesh geometry: discard if behind scene
    float sceneDepth = texture(u_DepthMap, fragScreenUV).r;
    if (gl_FragCoord.z > sceneDepth + 0.0001) discard;

    // Gaussian opacity: exp(-0.5 * A) * base_opacity
    float opacity = exp(-0.5 * A) * fragColor.a;

    // Output with alpha for blending (standard alpha blending: src=SRC_ALPHA, dst=ONE_MINUS_SRC_ALPHA)
    outColor = vec4(fragColor.rgb, opacity);
}
