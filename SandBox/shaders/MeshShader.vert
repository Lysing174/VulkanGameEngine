#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform UniformBufferObject {
    mat4 projView;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;

layout(push_constant) uniform PushConsts {
    mat4 model;
} pc;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
    gl_Position = ubo.projView * pc.model * vec4(inPosition, 1.0);
    fragTexCoord = inTexCoord;
}
