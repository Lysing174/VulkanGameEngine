#pragma once
#include "imgui.h"
#include "Engine/ImGui/ImGuiLayer.h"
#include "Engine/Core/Application.h"
#include "Engine/Eval/ApplicationSpecification.h"
#include "Engine/Core/Log.h"

#ifdef EG_PLATFORM_WINDOWS

extern Engine::Application* Engine::CreateApplication();

// 测试模式开关: 改这里即可切换。
//   true  → 走 ApplicationSpecification 离线测试流程 (路径/参数全部在代码里写死, 见 ApplicationSpecification.h 的 TestSpecification)
//   false → 走原有编辑器流程 (Application)
static const bool ENABLE_TEST_MODE = true;

int main(int argc,char** argv) 
{
	Engine::Log::Init();

	if (ENABLE_TEST_MODE)
	{
		Engine::ApplicationSpecification spec;
		spec.Run();
		return 0;
	}

	auto* app = Engine::CreateApplication();
	app->Run();
	delete app;
	return 0;
}

#endif
