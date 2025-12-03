#pragma once

#include "Engine/Layer/Layer.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "Engine/Events/MouseEvent.h"
#include "Engine/Events/KeyEvent.h"
#include "Engine/Events/ApplicationEvent.h"

//实现ImGuiLayer类，用于imgui渲染和事件
namespace Engine {
	class ImGuiLayer:public Layer
	{
	public:
		ImGuiLayer();
		~ImGuiLayer();

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		/// <summary>
		/// 为空，可删
		/// </summary>
		virtual void OnImGuiRender() override {}
		virtual void OnEvent(Event& event) override;

		/// <summary>
		/// 开始imgui渲染（每帧执行一次）
		/// </summary>
		void Begin();
		/// <summary>
		/// 结束imgui渲染，vulkan版不负责在此时渲染，渲染挪到vulkan的renderpass里
		/// </summary>
		void End();

		static ImGuiContext* GetContext();

	private:
		float m_Time = 0.0f;

		void SetDarkThemeColors();
	};
}