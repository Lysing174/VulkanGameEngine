#pragma once

#include "Core.h"
#include "Engine/Layer/LayerStack.h"
#include "Events/Event.h"
#include "Events/ApplicationEvent.h"
#include "Engine/ImGui/ImGuiLayer.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Window.h"
namespace Engine {
	class ENGINE_API Application
	{
	public:
		Application();
		virtual ~Application();

		void Run();

		void OnEvent(Event& e);

		void PushLayer(Layer* layer);
		void PushOverlay(Layer* layer);
		void PopLayer(Layer* layer);
		void PopOverlay(Layer* layer);


		inline Window& GetWindow() { return *m_Window; }

		inline static Application& Get() { return *s_Instance; }
	private:
		bool OnWindowResize(WindowResizeEvent& e);
		bool OnWindowClose(WindowCloseEvent& e);

		std::unique_ptr<Window> m_Window;
		static Application* s_Instance;
		bool m_Running = true;
		LayerStack m_LayerStack;
		ImGuiLayer* m_ImGuiLayer;
	};

	Application* CreateApplication();
}
