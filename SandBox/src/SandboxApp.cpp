#include "pch.h"
#include <Engine.h>
#include <Engine/Renderer/Shader.h>
#include <Engine/Renderer/Material.h>
#include <Engine/Renderer/Mesh.h>

#include "Engine/Renderer/EditorCamera.h"


class ExampleLayer :public Engine::Layer
{
public:
	ExampleLayer()
		:Layer("Example")
	{

	}
	void OnImGuiRender() override
	{
	}
	void OnAttach() override
	{

	}
	void OnDetach() override
	{

	}
	void OnUpdate() override
	{
	}
	void OnEvent(Engine::Event& event) override
	{
	}
private:
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
