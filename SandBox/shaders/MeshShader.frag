#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D texSampler;

layout(push_constant) uniform PushConstants {
    layout(offset = 64)vec4 inMaterialColor; 
} pc;

void main() {
    outColor = texture(texSampler, fragTexCoord) * pc.inMaterialColor;
}
