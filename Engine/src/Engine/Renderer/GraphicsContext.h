#pragma once
#include "Engine/Renderer/EditorCamera.h"

namespace Engine {

	class GraphicsContext
	{
	public:
		virtual ~GraphicsContext() = default;

		// 负责初始化图形 API (比如创建 Vulkan Instance, Device)
		virtual void Init() = 0;
		virtual void BeginFrame(const EditorCamera& camera) = 0;
		virtual void DrawFrame() = 0;
		virtual void EndFrame() = 0;

		virtual void DrawModel(uint32_t indexCount) = 0;
	};

}