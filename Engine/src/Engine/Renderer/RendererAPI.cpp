#include "pch.h"
#include "Engine/Renderer/RendererAPI.h"

//#include "Platform/Vulkan/VulkanRendererAPI.h"

namespace Engine {

	RendererAPI::API RendererAPI::s_API = RendererAPI::API::Vulkan;

	//Scope<RendererAPI> RendererAPI::Create()
	//{
	//	switch (s_API)
	//	{
	//	case RendererAPI::API::None:    HZ_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
	//	case RendererAPI::API::OpenGL:  return CreateScope<OpenGLRendererAPI>();
	//	}

	//	HZ_CORE_ASSERT(false, "Unknown RendererAPI!");
	//	return nullptr;
	//}

	void RendererAPI::LoadModel(std::string modelPath, std::vector<Engine::Vertex>& vertices, std::vector<uint32_t>& indices)
	{
		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;
		std::string warn, err;

		if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, modelPath.c_str())) {
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

	}


}