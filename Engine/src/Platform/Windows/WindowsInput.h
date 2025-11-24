#pragma once
#include "Engine/Input.h"

namespace Engine {
	class WindowsInput :public Input
	{
	protected:
		virtual bool IsKeyPressedImpl(int keycode) override;
		virtual bool IsMouseButtonPressedImpl(int button) override;
		virtual glm::vec2 GetMousePositionImpl() override;

	};
}