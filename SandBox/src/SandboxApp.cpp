#include "pch.h"
#include <Engine.h>
#include <Engine/Renderer/Shader.h>
#include <Engine/Renderer/Material.h>
#include <Engine/Renderer/Mesh.h>
#include "Engine/Renderer/EditorCamera.h"

const std::string MODEL_PATH = "models/cottage_obj.obj";
const std::string TEXTURE_PATH = "textures/cottage_diffuse.png";

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
		m_Shader=Engine::Shader::Create("shaders/Mesh.vert.spv", "shaders/Mesh.frag.spv");
		m_Material = Engine::Material(m_Shader);

        std::vector<Engine::MeshVertex> vertices;
        std::vector<uint32_t> indices;

		Engine::RendererAPI::LoadModel(MODEL_PATH, vertices, indices);

		m_Mesh = Engine::Mesh::CreateCube();

		m_Shader->CreatePipeline(m_Mesh.GetVertexBuffer()->GetLayout());

	}
	void OnDetach() override
	{

	}
	void OnUpdate() override
	{
		m_Material.Bind();
		m_Mesh.Bind();

        Engine::Application::Get().GetWindow().GetContext()->DrawModel(m_Mesh.GetIndexCount());
	}
	void OnEvent(Engine::Event& event) override
	{
	}
private:
	std::shared_ptr<Engine::Shader> m_Shader;
	Engine::Mesh m_Mesh;
	Engine::Material m_Material;

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
