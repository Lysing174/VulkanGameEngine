#pragma once
#include "imgui.h"
#include "Engine/ImGui/ImGuiLayer.h"

#ifdef EG_PLATFORM_WINDOWS

extern Engine::Application* Engine::CreateApplication();

int main(int argc,char** argv) 
{
	Engine::Log::Init();
	auto* app = Engine::CreateApplication();
	ImGui::SetCurrentContext(Engine::ImGuiLayer::GetContext());
	app->Run();
	delete app;
}

#endif