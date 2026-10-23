#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <Engine/Scene/Components.h>

namespace Engine {

// 前置声明: MeshRenderCommandRequest 仅以 shared_ptr 持有, 头文件无需完整定义
class Mesh;
class Material;

	// ============================================================
	// Descriptor Set 编号 — 单一事实来源 (后续 Phase 才真正接入 shader/pipeline)
	//   set 0: GaussianRenderer (b0(FrameInfo UBO) / b1(Gaussian) / b2(SortedIndex) / b4(ModelTransform))
	//   set 1: Material        (逐材质 UBO + 贴图, 保持不变)
	//   set 2: LightRenderer   (光源 SSBO + 阴影 UBO + 阴影贴图)
	//   set 3: Env             (SH IBL UBO + 环境 cubemap)
	//   set 4: (保留)
	//   相机矩阵 (viewProj + cameraPosition) 走 push constant, 不再占任何 set
	// ============================================================
	inline constexpr uint32_t GAUSSIAN_SET = 0;
	inline constexpr uint32_t MATERIAL_SET = 1;
	inline constexpr uint32_t LIGHT_SET    = 2;
	inline constexpr uint32_t ENV_SET      = 3;

	// 共享常量
	constexpr uint32_t MAX_LIGHTS = 32;           // 场景最大光源数
	constexpr uint32_t MAX_LIGHTS_PER_OBJECT = 4; // 每物体最多光源数

	constexpr uint32_t MAX_SHADOW_MAPS = 4;       // 阴影贴图数组上限 (descriptor 数组大小)
	constexpr uint32_t SHADOW_MAP_SIZE = 2048;    // 单张阴影贴图尺寸 (固定)

	// GPU 端光源结构 (std430, SSBO 用)
	struct GPULight
	{
		glm::vec4 PositionType;   // xyz = 位置(点光) 或 0(方向光), w = 类型: 0=点光, 1=方向光
		glm::vec4 ColorIntensity; // rgb = 颜色, a = 强度
		glm::vec4 DirectionRange; // xyz = 方向(方向光), w = 范围(点光衰减距离)
	};

	struct MeshRenderCommandRequest
	{
		glm::mat4 Transform;
		std::shared_ptr<Mesh> Mesh;
		std::shared_ptr<Material> Material;
		int EntityID;

		uint32_t SubmeshIndexCount;
		uint32_t SubmeshFirstIndex;
		uint32_t SubmeshFirstVertex;

		// 每物体光源裁剪结果
		uint32_t LightCount = 0;
		uint32_t LightIndices[MAX_LIGHTS_PER_OBJECT] = {};
	};

	// Shadow data UBO (set=2) — std140 布局, CPU/GPU 必须一致
	// 注意: std140 中 ivec4 数组每个元素占 16 字节, mat4 占 64 字节
	struct ShadowUBO
	{
		glm::mat4 lightViewProj[MAX_SHADOW_MAPS];   // 每个阴影光的光视投影矩阵 (已含 [0,1] UV 偏移)
		glm::vec4  shadowBias[MAX_SHADOW_MAPS];      // .x = 阴影偏移 bias
		glm::ivec4 shadowMapOfLight[8];             // 每光源(最多32)对应的阴影图 slot, -1 = 无阴影
	};

}
