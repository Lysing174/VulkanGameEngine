#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out mat3 fragTBN;

// 合并 push constant: 与 fragment 端 PushConsts 完全一致 (同名同偏移), C++ 按偏移推送.
// 总大小 192B, 需 device.limits.maxPushConstantsSize >= 192 (绝大多数桌面 GPU 为 256).
layout(push_constant) uniform PushConsts {
    layout(offset = 0)   mat4 model;             // VERTEX
    layout(offset = 64)  vec4  pcAlbedoColor;    // FRAGMENT (未在此阶段使用)
    layout(offset = 80)  uint  pcLightCount;     // FRAGMENT
    layout(offset = 84)  uint  pcLightIndices[4];// FRAGMENT
    layout(offset = 112) mat4  projView;         // VERTEX
    layout(offset = 176) vec4  cameraPosition;   // FRAGMENT
} pc;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = pc.projView * worldPos;
    fragTexCoord = inTexCoord;

    mat3 normalMatrix = mat3(pc.model);
    vec3 N = normalize(normalMatrix * inNormal);
    vec3 T = normalize(normalMatrix * inTangent);

    // Reconstruct B via cross product; Assimp stores handedness in mBitangents,
    // not tangent.w, so we don't need to read w — cross(N,T) is correct.
    vec3 B = cross(N, T);

    fragTBN = mat3(T, B, N);
    fragNormal = N;
    fragWorldPos = worldPos.xyz;
}
