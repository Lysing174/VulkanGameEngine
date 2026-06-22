#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 projView;
    vec4 cameraPosition;
    vec4 lightPosition;
    vec4 lightColorIntensity;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out mat3 fragTBN;

layout(push_constant) uniform PushConsts {
    mat4 model;
} pc;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = ubo.projView * worldPos;
    fragTexCoord = inTexCoord;

    mat3 normalMatrix = mat3(pc.model);
    vec3 N = normalize(normalMatrix * inNormal);
    vec3 T = normalize(normalMatrix * inTangent);

    // Re-orthogonalize T against N (Gram-Schmidt)
    T = normalize(T - dot(T, N) * N);

    // Reconstruct B via cross product; Assimp stores handedness in mBitangents,
    // not tangent.w, so we don't need to read w — cross(N,T) is correct.
    vec3 B = cross(N, T);

    fragTBN = mat3(T, B, N);
    fragNormal = N;
    fragWorldPos = worldPos.xyz;
}
