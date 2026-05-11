#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 fragTexCoord;

// 深度纹理采样器 (输入布局需与C++侧对应)
layout(set = 1, binding = 0) uniform sampler2D u_DepthMap;

layout(location = 0) out vec4 outColor;

void main() {
    // 采样深度值
    float depth = texture(u_DepthMap, fragTexCoord).r;

    // 可视化深度：将线性深度映射到可见颜色
    // 方法1：灰度显示（近黑远白/近白远黑）
    //float visualDepth = 1.0 - depth;  // 近处亮、远处暗
    
    // 方法2：彩色热力图风格（可选启用）
    // 这里使用简单的渐变色增强可视化效果
    //vec3 colorNear = vec3(0.1, 0.1, 0.8);  // 近处：蓝色
    //vec3 colorFar  = vec3(0.8, 0.2, 0.1);  // 远处：红色
    //vec3 depthColor = mix(colorNear, colorFar, depth);

    // 最终输出：使用彩色深度 + 边框
    //outColor = vec4(depthColor, 1.0);

float mask = depth < 0.999 ? 1.0 : 0.0;
    
    // 2. 线性化处理 (假设近平面 0.1, 远平面 100.0)
    float n = 0.1; float f = 100.0;
    float linearDepth = (2.0 * n) / (f + n - depth * (f - n));

    // 3. 输出对比度极高的黑白图
    outColor = vec4(vec3(mask * (1.0 - linearDepth)), 1.0);
}
