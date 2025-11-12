#include "pch.h"
#include <Engine.h>

class SandBox : public Engine::Application
{
public:
	SandBox() {}
	~SandBox() {}

private:

};

Engine::Application* Engine::CreateApplication()
{
	return new SandBox();
}
