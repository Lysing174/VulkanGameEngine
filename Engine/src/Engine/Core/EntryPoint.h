#pragma once
#include "imgui.h"
#include "Engine/ImGui/ImGuiLayer.h"
#include "Engine/Core/Application.h"
#include "Engine/Core/Log.h"

#ifdef EG_PLATFORM_WINDOWS

extern Engine::Application* Engine::CreateApplication();

int main(int argc,char** argv) 
{
	Engine::Log::Init();
	auto* app = Engine::CreateApplication();
	app->Run();
	delete app;
}

#endif