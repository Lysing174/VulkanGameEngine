#pragma once

namespace Engine {

	class GraphicsContext
	{
	public:
		virtual ~GraphicsContext() = default;

		// 负责初始化图形 API (比如创建 Vulkan Instance, Device)
		virtual void Init() = 0;
		virtual void BeginFrame() = 0;
		virtual void DrawFrame() = 0;

		// 负责把画好的帧显示到屏幕上 (Vulkan 的 Present)
		virtual void EndFrame() = 0;
	};

}