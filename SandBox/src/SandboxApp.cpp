#include "pch.h"
#include <Engine.h>

class ExampleLayer :public Engine::Layer
{
public:
	ExampleLayer()
		:Layer("Example")
	{

	}
	void OnUpdate() override
	{
		EG_INFO("ExampleLayer::Update");
	}
	void OnEvent(Engine::Event& event) override
	{
		EG_TRACE("{0}", event.ToString());
	}
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
