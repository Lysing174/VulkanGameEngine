#pragma once

#include "Engine/Layer/Layer.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

namespace Engine {
	class ImGuiLayer:public Layer
	{
	public:
		ImGuiLayer();
		~ImGuiLayer();

		void OnAttach();
		void OnDetach();
		void OnUpdate();
		void OnEvent(Event& event);

		void Begin();
		void End();

		static ENGINE_API ImGuiContext* GetContext();

	private:
	};
}