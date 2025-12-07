#pragma once

#include "Core.h"
#include "Engine/Layer/LayerStack.h"
#include "Engine/Events/Event.h"
#include "Engine/Events/ApplicationEvent.h"
#include "Engine/ImGui/ImGuiLayer.h"
#include "Engine/Layer/EditorLayer.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Window.h"
namespace Engine {
	class Application
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
		inline float GetTimeStep() { return m_TimeStep; }
		inline float GetFixedTimeStep() { return m_FixedTimeStep; }

		inline static Application& Get() { return *s_Instance; }
	private:
		bool OnWindowResize(WindowResizeEvent& e);
		bool OnWindowClose(WindowCloseEvent& e);

		std::unique_ptr<Window> m_Window;
		static Application* s_Instance;
		bool m_Running = true;
		LayerStack m_LayerStack;
		ImGuiLayer* m_ImGuiLayer;
		EditorLayer* m_EditorLayer;
		const float m_FixedTimeStep = 1.0f / 60.0f;
		float m_TimeStep;
		float m_LastFrameTime = 0.0f;
		float m_Accumulator = 0.0f;//纯私有，不外用
	};

	Application* CreateApplication();
}
