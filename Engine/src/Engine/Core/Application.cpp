#include "pch.h"
#include "Application.h"
#include "Engine/Renderer/Renderer.h"
#include "Input.h"

#include <GLFW/glfw3.h>
namespace Engine {
#define BIND_EVENT_FN(x) std::bind(&x, this, std::placeholders::_1)

	Application* Application::s_Instance = nullptr;
	Application::Application()
	{
		EG_CORE_ASSERT(!s_Instance, "Application already exists!");
		s_Instance = this;

		m_Window = std::unique_ptr<Window>(Window::Create());
		m_Window->SetEventCallback(BIND_EVENT_FN(Application::OnEvent));

		m_EditorLayer = new EditorLayer();
		PushLayer(m_EditorLayer);

		m_ImGuiLayer = new ImGuiLayer();
		PushOverlay(m_ImGuiLayer);
	}
	Application::~Application() 
	{
		if (m_Window) {
			auto* context = static_cast<VulkanContext*>(m_Window->GetContext());
			if (context) {
				vkDeviceWaitIdle(context->GetDevice());
			}
		}

		for (Layer* layer : m_LayerStack)
		{
			layer->OnDetach();
			delete layer;
		}
	}
	
	/// <summary>
	/// 所有事件都会激活该回调函数
	/// </summary>
	/// <param name="e"></param>
	void Application::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<WindowResizeEvent>(BIND_EVENT_FN(Application::OnWindowResize));
		dispatcher.Dispatch<WindowCloseEvent>(BIND_EVENT_FN(Application::OnWindowClose));

		for (auto it = m_LayerStack.end(); it != m_LayerStack.begin();)
		{
			(*--it)->OnEvent(e);
			if (e.Handled)
				break;
		}
	}
	void Application::PushLayer(Layer* layer)
	{
		m_LayerStack.PushLayer(layer);
		layer->OnAttach();
	}

	void Application::PushOverlay(Layer* layer)
	{
		m_LayerStack.PushOverlay(layer);
		layer->OnAttach();
	}

	void Application::PopLayer(Layer* layer)
	{
		m_LayerStack.PopLayer(layer); 
		layer->OnDetach();            
	}
	void Application::PopOverlay(Layer* layer)
	{
		m_LayerStack.PopOverlay(layer);
		layer->OnDetach();
	}

	void Application::Run()
	{
		while (m_Running)
		{
			glfwPollEvents();

			//计算时间
			float time = (float)glfwGetTime();
			m_TimeStep = time - m_LastFrameTime;
			m_LastFrameTime = time;

			if (m_TimeStep > 0.25f) m_TimeStep = 0.25f;

			m_Accumulator += m_TimeStep;

			//FixedUpdate
			while (m_Accumulator >= m_FixedTimeStep)
			{
				for (Layer* layer : m_LayerStack)
				{
					layer->OnFixedUpdate(); 
				}
				m_Accumulator -= m_FixedTimeStep;
			}

			//Update
			Renderer::BeginScene(m_EditorLayer->GetEditorCamera());

			for (Layer* layer : m_LayerStack)
			{
				layer->OnUpdate();
			}

			m_ImGuiLayer->Begin();

			for (Layer* layer : m_LayerStack)
				layer->OnImGuiRender();

			m_ImGuiLayer->End();
			Renderer::EndScene();

		}
	}

	bool Application::OnWindowResize(WindowResizeEvent& e)
	{
		((VulkanContext*)(m_Window->GetContext()))->OnWindowResized(e.GetWidth(), e.GetHeight());

		return false;
	}

	bool Application::OnWindowClose(WindowCloseEvent& e)
	{
		m_Running = false;
		return true;
	}

}