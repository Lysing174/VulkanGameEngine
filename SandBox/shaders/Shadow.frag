#version 450
#extension GL_ARB_separate_shader_objects : enable

// 阴影 Pass 片元着色器 (方向光阴影贴图)
// 无颜色输出, 仅深度写入 (depth-only subpass)

void main() {
    // 深度值由 gl_FragCoord.z 自动写入深度附件, 无需额外操作
}
