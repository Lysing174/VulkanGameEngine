// Shared ABI for the tile stages. Included after #version / include extension.
struct ProjectedSplatGPU {
    vec4 ndcCenter;
    vec4 ndcBasis;
    vec4 color;
    vec4 viewCenter;
    vec4 quadCenter;
    vec4 tileBounds;
    vec4 conic;
    vec4 ndcBounds;
};
layout(std140, set=0, binding=0) uniform FrameInfo {
    vec4 cameraPosAndScale;
    vec4 focal;
    vec4 viewportInfo;
    vec4 extraInfo;
} frame;
layout(std430, set=0, binding=1) readonly buffer Projected { ProjectedSplatGPU splats[]; };
layout(std430, set=0, binding=2) readonly buffer Sorted { uint sortedIDs[]; };
layout(std430, set=0, binding=3) readonly buffer Work { uvec4 counts; } work;
layout(std430, set=0, binding=4) buffer Scratch { uvec2 scratch[]; };
layout(std430, set=0, binding=5) buffer Groups { uvec2 groups[]; };
// Same leading ABI as the radix sort WorkInfo. z marks fallback, w is capacity.
layout(std430, set=0, binding=6) buffer TileWork {
    uvec4 counts;
    uvec4 dispatch;
} tileWork;
layout(std430, set=0, binding=7) buffer TileKeys { uint tileKeys[]; };
layout(std430, set=0, binding=8) buffer TileIDs { uint tileIDs[]; };
layout(std430, set=0, binding=9) buffer Ranges { uvec2 ranges[]; };
layout(push_constant) uniform TileParameters {
    uvec4 grid; // tile width, tile height, reference capacity, input group count
    vec4 projection; // projection[2][2], projection[3][2], unused, unused
} params;

uint cappedAdd(uint a, uint b) {
    return a + min(b, params.grid.z + 1u - a);
}

uvec4 tileRect(ProjectedSplatGPU p) {
    // Conservative compact support AABB intersected with the original tile domain.
    // Keep a small expansion when converting floating-point NDC back to pixels.
    vec4 bounds = vec4(max(p.ndcBounds.xy, p.tileBounds.xy),
                       min(p.ndcBounds.zw, p.tileBounds.zw));
    vec4 pixels = (bounds * 0.5 + 0.5) * frame.viewportInfo.xyxy;
    uvec2 lo = uvec2(clamp(floor((pixels.xy - 0.001) / 16.0), vec2(0), vec2(params.grid.xy)));
    uvec2 hi = uvec2(clamp(ceil((pixels.zw + 0.001) / 16.0), vec2(lo), vec2(params.grid.xy)));
    return uvec4(lo, hi);
}
