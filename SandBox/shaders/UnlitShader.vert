#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;

// Match the existing mesh draw path's push-constant offsets and range.
layout(push_constant) uniform PushConsts {
    layout(offset = 0) mat4 model;
    layout(offset = 64) vec4 pcAlbedoColor;
    layout(offset = 80) uint pcLightCount;
    layout(offset = 84) uint pcLightIndices[4];
    layout(offset = 112) mat4 projView;
    layout(offset = 176) vec4 cameraPosition;
} pc;

void main() {
    gl_Position = pc.projView * pc.model * vec4(inPosition, 1.0);
    fragTexCoord = inTexCoord;
}
