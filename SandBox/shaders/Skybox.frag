#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragLocalPos;

// Prefiltered environment cubemap from global descriptor set
layout(set = 0, binding = 3) uniform samplerCube u_PrefilteredEnvMap;

layout(location = 0) out vec4 outColor;

void main() {
    // Sample cubemap using the local position as direction
    vec3 dir = normalize(fragLocalPos);
    vec3 color = texture(u_PrefilteredEnvMap, dir).rgb;
    
    // Tone mapping (Reinhard)
    color = color / (color + vec3(1.0));
    
    // Simple gamma correction (optional, remove if already linear)
    // color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
