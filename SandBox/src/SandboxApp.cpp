#include "pch.h"
#include <Engine.h>
#include <Engine/Renderer/Shader.h>

class ExampleLayer :public Engine::Layer
{
public:
	ExampleLayer()
		:Layer("Example")
	{

	}
	void OnImGuiRender() override
	{
		ImGui::ShowDemoWindow();
	}
	void OnAttach() override
	{
		m_Shader=Engine::Shader::Create("shaders/vert.spv", "shaders/frag.spv");
	}
	void OnDetach() override
	{

	}
	void OnUpdate() override
	{
		m_Shader->Bind();
	}
	void OnEvent(Engine::Event& event) override
	{
	}
private:
	std::shared_ptr<Engine::Shader> m_Shader;
};

class SandBox : public Engine::Application
{
public:
	SandBox() 
	{
		PushLayer(new ExampleLayer());
	}
	~SandBox() {}

private:

};

Engine::Application* Engine::CreateApplication()
{
	return new SandBox();
}
