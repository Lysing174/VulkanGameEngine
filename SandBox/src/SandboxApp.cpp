#include "pch.h"
#include <Engine.h>
#include <Engine/Renderer/Shader.h>
#include <Engine/Renderer/Buffer.h>

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

        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;

        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, MODEL_PATH.c_str())) {
            throw std::runtime_error(err);
        }
        std::unordered_map<Engine::Vertex, uint32_t> uniqueVertices = {};

        for (const auto& shape : shapes) {
            for (const auto& index : shape.mesh.indices) {
                Engine::Vertex vertex = {};

                vertex.pos = {
                    attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2]
                };

                if (index.texcoord_index >= 0) {
                    vertex.texCoord = {
                        attrib.texcoords[2 * index.texcoord_index + 0],
                        1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
                    };
                }
                else {
                    vertex.texCoord = { 0.0f, 0.0f };  // 使用默认纹理坐标
                }

                vertex.color = { 1.0f, 1.0f, 1.0f };

                if (uniqueVertices.count(vertex) == 0) {
                    uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                    vertices.push_back(vertex);
                }

                indices.push_back(uniqueVertices[vertex]);
            }
        }

		m_VertexBuffer = Engine::VertexBuffer::Create(vertices, sizeof(vertices[0]) * vertices.size());
        m_IndexBuffer = Engine::IndexBuffer::Create(indices, indices.size());

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
