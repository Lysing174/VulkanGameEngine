#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec2 fragTexCoord;

out gl_PerVertex {
    vec4 gl_Position;
};

const vec2 positions[6] = vec2[](
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(0.0, 1.0),
    vec2(1.0, 0.0),
    vec2(1.0, 1.0),
    vec2(0.0, 1.0)
);

// 矩形参数：屏幕空间 [0,1]，左上角为原点
layout(push_constant) uniform PushConsts {
    vec2 rectOffset;   // 矩形起始偏移 (0.0, 0.0) = 左上角
    vec2 rectScale;    // 矩形缩放 (0.3, height_by_aspect)
} pc;

void main() {
    vec2 pos = positions[gl_VertexIndex];
    
    // 映射到目标矩形区域：offset + position * scale
    // Vulkan的NDC: y轴向下为正，所以需要翻转y
    vec2 screenPos = pc.rectOffset + pos * pc.rectScale;
    
    // 转换到NDC [-1, 1]
    gl_Position = vec4(screenPos * 2.0 - 1.0, 0.0, 1.0);
    // 翻转Y轴以匹配纹理坐标
    //gl_Position.y = -gl_Position.y;
    
    // 输出纹理坐标
    fragTexCoord = pos;
//fragTexCoord = vec2(pos.x, 1.0 - pos.y);
}
