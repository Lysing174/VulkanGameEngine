#version 450
#extension GL_ARB_separate_shader_objects : enable

// Cube mesh vertex inputs (matching MeshVertex layout)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;

layout(location = 0) out vec3 fragLocalPos;

// View-projection matrix (without translation for infinite skybox)
layout(push_constant) uniform PushConsts {
    mat4 vp;
} pc;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
    // Transform without translation → infinite distance
    vec4 clipPos = pc.vp * vec4(inPosition, 1.0);

    // .xyww ensures depth = 1.0 after perspective divide (behind everything)
    gl_Position = vec4(clipPos.xy,clipPos.w*0.9999f,clipPos.w);//clipPos.xyww;

    // Pass local position for cubemap sampling
    fragLocalPos = inPosition;
}
