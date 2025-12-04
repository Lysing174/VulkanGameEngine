#include "pch.h"
#include <Engine.h>
#include <Engine/Renderer/Shader.h>
#include <Engine/Renderer/Buffer.h>
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
		m_Shader=Engine::Shader::Create("shaders/vert.spv", "shaders/frag.spv");

        std::vector<Engine::Vertex> vertices;
        std::vector<uint32_t> indices;

		Engine::RendererAPI::LoadModel(MODEL_PATH, vertices, indices);

        Engine::BufferLayout layout = {
			{ Engine::ShaderDataType::Float3, "a_Position" },
			{ Engine::ShaderDataType::Float3, "a_Color" },
			{Engine::ShaderDataType::Float2,"a_Texcoord"}
        };
		m_VertexBuffer = Engine::VertexBuffer::Create(vertices, sizeof(vertices[0]) * vertices.size());
        m_IndexBuffer = Engine::IndexBuffer::Create(indices, indices.size());

		m_Shader->CreatePipeline(layout);

	}
	void OnDetach() override
	{

	}
	void OnUpdate() override
	{
		m_Shader->Bind();

        m_VertexBuffer->Bind();
        m_IndexBuffer->Bind();

        Engine::Application::Get().GetWindow().GetContext()->DrawModel(m_IndexBuffer->GetCount());
	}
	void OnEvent(Engine::Event& event) override
	{
	}
private:
	std::shared_ptr<Engine::Shader> m_Shader;
	std::shared_ptr<Engine::VertexBuffer> m_VertexBuffer;
    std::shared_ptr<Engine::IndexBuffer> m_IndexBuffer;

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
