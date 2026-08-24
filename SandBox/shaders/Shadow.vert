#version 450
#extension GL_ARB_separate_shader_objects : enable

// 阴影 Pass 顶点着色器 (方向光阴影贴图)
// 仅写深度: 把顶点变换到光视空间 (lightViewProj * model * pos)

layout(location = 0) in vec3 inPosition;

// Push constants: model (offset 0) + lightViewProj (offset 64)
layout(push_constant) uniform PushConsts {
    mat4 model;
    mat4 lightViewProj;
} pc;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = pc.lightViewProj * worldPos;
}
