#include "pch.h"
#include "Model.h"
#include "Engine/Renderer/Texture.h"
#include "Platform/Vulkan/VulkanTexture.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>

namespace Engine {

	// ---- 辅助：从 tinygltf::Node 计算局部变换矩阵 ----
	// glTF 规范: 局部变换 = T * R * S (先缩放，再旋转，最后平移)
	static glm::mat4 GetNodeTransform(const tinygltf::Node& node)
	{
		if (node.matrix.size() == 16)
			return glm::make_mat4(node.matrix.data());

		glm::mat4 S = glm::mat4(1.0f);
		glm::mat4 R = glm::mat4(1.0f);
		glm::mat4 T = glm::mat4(1.0f);

		if (node.scale.size() == 3)
			S = glm::scale(glm::mat4(1.0f), glm::vec3((float)node.scale[0], (float)node.scale[1], (float)node.scale[2]));
		if (node.rotation.size() == 4)
			R = glm::mat4_cast(glm::quat((float)node.rotation[3], (float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2]));
		if (node.translation.size() == 3)
			T = glm::translate(glm::mat4(1.0f), glm::vec3((float)node.translation[0], (float)node.translation[1], (float)node.translation[2]));

		return T * R * S;
	}

	// ---- 辅助：从 accessor 获取类型化数据指针 ----
	template<typename T>
	static const T* GetAccessorData(const tinygltf::Model& model, int accessorIndex)
	{
		if (accessorIndex < 0 || accessorIndex >= (int)model.accessors.size())
			return nullptr;
		const auto& accessor = model.accessors[accessorIndex];
		const auto& bufferView = model.bufferViews[accessor.bufferView];
		const auto& buffer = model.buffers[bufferView.buffer];
		return reinterpret_cast<const T*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);
	}

	// ========================================================================
	// Model
	// ========================================================================

	Model::Model(const std::string& path, const std::shared_ptr<Shader>& shader)
		: m_BaseShader(shader)
	{
		LoadModel(path);
	}

	void Model::LoadModel(const std::string& path)
	{
		// 仅加载 .gltf / .glb 文件
		std::string pathLower = path;
		std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(), ::tolower);

		if (pathLower.find(".gltf") != std::string::npos ||
			pathLower.find(".glb") != std::string::npos)
		{
			LoadGLTF(path);
		}
		else
		{
			EG_CORE_ERROR("Model::LoadModel: unsupported format, only .gltf/.glb is supported: {0}", path);
		}
	}

	// ------- 纹理加载 -------
	std::shared_ptr<Texture2D> Model::LoadGLTFTexture(const tinygltf::Model& model, int textureIndex, bool sRGB)
	{
		if (textureIndex < 0 || textureIndex >= (int)model.textures.size())
			return nullptr;

		const auto& gltfTex = model.textures[textureIndex];
		if (gltfTex.source < 0 || gltfTex.source >= (int)model.images.size())
			return nullptr;

		const auto& image = model.images[gltfTex.source];
		VkFormat format = sRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

		// 1) 内嵌图片（bufferView 已解码为 RGBA 像素）
		if (image.bufferView >= 0 && !image.image.empty())
		{
			return std::make_shared<VulkanTexture2D>(
				(uint32_t)image.width, (uint32_t)image.height,
				(void*)image.image.data(), format);
		}

		// 2) 外部文件 URI
		if (!image.uri.empty() && image.uri.find("data:") != 0)
		{
			std::string texPath = m_Directory + "/" + image.uri;
			std::replace(texPath.begin(), texPath.end(), '\\', '/');
			try
			{
				return Texture2D::Create(texPath, sRGB);
			}
			catch (const std::exception& e)
			{
				EG_CORE_WARN("Failed to load texture {0}: {1}", texPath, e.what());
			}
		}

		// 3) Data URI（tinygltf 已解码，image.image 中有 RGBA 数据）
		if (!image.image.empty())
		{
			return std::make_shared<VulkanTexture2D>(
				(uint32_t)image.width, (uint32_t)image.height,
				(void*)image.image.data(), format);
		}

		return nullptr;
	}

	// ------- 材质加载 (glTF PBR Metallic-Roughness) -------
	void Model::LoadGLTFMaterials(const tinygltf::Model& model)
	{
		if (model.materials.empty()) return;

		m_Materials.resize(model.materials.size());
		for (size_t i = 0; i < model.materials.size(); i++)
		{
			const auto& gltfMat = model.materials[i];
			auto material = std::make_shared<Material>(m_BaseShader);
			material->BeginBatchUpdate();

			const auto& pbr = gltfMat.pbrMetallicRoughness;

			// Albedo / BaseColor
			auto albedoTex = LoadGLTFTexture(model, pbr.baseColorTexture.index, true);
			if (albedoTex)
			{
				material->SetTexture("u_AlbedoMap", albedoTex);
				material->SetHasAlbedoMap(true);
			}
			material->SetAlbedoColor(glm::vec4(
				(float)pbr.baseColorFactor[0],
				(float)pbr.baseColorFactor[1],
				(float)pbr.baseColorFactor[2],
				(float)pbr.baseColorFactor[3]));

			// Normal map (sRGB=false: 法线贴图是向量数据)
			auto normalTex = LoadGLTFTexture(model, gltfMat.normalTexture.index, false);
			if (normalTex)
			{
				material->SetTexture("u_NormalMap", normalTex);
				material->SetUseNormalMap(true);
			}

			// ---- Metallic-Roughness 贴图 (G=Roughness, B=Metallic, 可能含 AO 于 R 通道) ----
			bool hasMR = (pbr.metallicRoughnessTexture.index >= 0);
			bool hasAO = (gltfMat.occlusionTexture.index >= 0);

			if (hasMR)
			{
				auto ormTex = LoadGLTFTexture(model, pbr.metallicRoughnessTexture.index, false);
				if (ormTex)
				{
					material->SetTexture("u_MetalRoughAO", ormTex);
					material->SetHasMetalRoughnessMap(true);
					// 如果 ORM 贴图的 R 通道含 AO，设置强度
					material->SetAOStrength(1.0f);
				}
			}
			// Metallic / Roughness factor
			if (pbr.metallicFactor < 0.01f && hasMR)
				material->SetMetalness(1.0f);
			else
				material->SetMetalness((float)pbr.metallicFactor);
			
			if (pbr.roughnessFactor < 0.01f && hasMR)
				material->SetRoughness(1.0f);
			else
				material->SetRoughness((float)pbr.roughnessFactor);

			// ---- AO 独立贴图 (走 u_AOMap binding，优先于 ORM 的 R 通道) ----
			if (hasAO)
			{
				auto aoTex = LoadGLTFTexture(model, gltfMat.occlusionTexture.index, false);
				if (aoTex)
				{
					material->SetTexture("u_AOMap", aoTex);
					material->SetHasAoMap(true);
					float strength = gltfMat.occlusionTexture.strength >= 0.01f
						? (float)gltfMat.occlusionTexture.strength : 1.0f;
					material->SetAOStrength(strength);
				}
			}

			// Emissive
			auto emissiveTex = LoadGLTFTexture(model, gltfMat.emissiveTexture.index, true);
			if (emissiveTex)
			{
				material->SetTexture("u_EmissiveMap", emissiveTex);
				material->SetHasEmissiveMap(true);
			}
			glm::vec3 emissiveColor(
				(float)gltfMat.emissiveFactor[0],
				(float)gltfMat.emissiveFactor[1],
				(float)gltfMat.emissiveFactor[2]);
			float emissiveIntensity = glm::length(emissiveColor);
			if (emissiveIntensity > 0.0f)
				material->SetEmissive(1.0f, emissiveColor);

			// Alpha mode / cutoff
			if (gltfMat.alphaMode == "MASK")
			{
				glm::vec4 col = material->GetColor();
				// alphaCutoff is already applied by shader; keep the color
			}

			material->EndBatchUpdate();
			m_Materials[i] = material;
		}
	}

	// ------- 场景图遍历 -------
	void Model::ProcessGLTFNode(int nodeIndex, const tinygltf::Model& model, const glm::mat4& parentTransform)
	{
		if (nodeIndex < 0 || nodeIndex >= (int)model.nodes.size())
			return;

		const auto& node = model.nodes[nodeIndex];
		glm::mat4 worldTransform = parentTransform * GetNodeTransform(node);

		if (node.mesh >= 0 && node.mesh < (int)model.meshes.size())
		{
			ProcessGLTFMesh(model.meshes[node.mesh], model, worldTransform);
		}

		for (int child : node.children)
			ProcessGLTFNode(child, model, worldTransform);
	}

	// ------- 处理一个 glTF Mesh (可能包含多个 Primitive) -------
	void Model::ProcessGLTFMesh(const tinygltf::Mesh& mesh, const tinygltf::Model& model, const glm::mat4& transform)
	{
		for (size_t pi = 0; pi < mesh.primitives.size(); pi++)
		{
			const auto& primitive = mesh.primitives[pi];

			// 必须有 POSITION 属性
			auto posIt = primitive.attributes.find("POSITION");
			if (posIt == primitive.attributes.end()) continue;

			uint32_t firstVertex = (uint32_t)m_GlobalVertices.size();
			uint32_t firstIndex = (uint32_t)m_GlobalIndices.size();

			// 获取各属性访问器指针
			const float* posData = GetAccessorData<float>(model, posIt->second);

			auto norIt = primitive.attributes.find("NORMAL");
			const float* normalData = (norIt != primitive.attributes.end())
				? GetAccessorData<float>(model, norIt->second) : nullptr;

			auto texIt = primitive.attributes.find("TEXCOORD_0");
			const float* texCoordData = (texIt != primitive.attributes.end())
				? GetAccessorData<float>(model, texIt->second) : nullptr;

			auto tanIt = primitive.attributes.find("TANGENT");
			const float* tangentData = (tanIt != primitive.attributes.end())
				? GetAccessorData<float>(model, tanIt->second) : nullptr;

			int vertexCount = (int)model.accessors[posIt->second].count;

			// 遍历顶点
			for (int v = 0; v < vertexCount; v++)
			{
				MeshVertex vertex;
				vertex.pos = { posData[v * 3 + 0], posData[v * 3 + 1], posData[v * 3 + 2] };

				vertex.normal = normalData
					? glm::vec3(normalData[v * 3 + 0], normalData[v * 3 + 1], normalData[v * 3 + 2])
					: glm::vec3(0.0f);

				vertex.texCoord = texCoordData
					? glm::vec2(texCoordData[v * 2 + 0], texCoordData[v * 2 + 1])
					: glm::vec2(0.0f);

				// glTF TANGENT 是 vec4 (x, y, z, w=手性)
				vertex.tangent = tangentData
					? glm::vec3(tangentData[v * 4 + 0], tangentData[v * 4 + 1], tangentData[v * 4 + 2])
					: glm::vec3(1.0f, 0.0f, 0.0f);

				m_GlobalVertices.push_back(vertex);
			}

			// 索引
			uint32_t indexCount = 0;
			if (primitive.indices >= 0)
			{
				const auto& idxAccessor = model.accessors[primitive.indices];
				const auto& bufferView = model.bufferViews[idxAccessor.bufferView];
				const auto& buffer = model.buffers[bufferView.buffer];
				const uint8_t* base = &buffer.data[bufferView.byteOffset + idxAccessor.byteOffset];
				indexCount = (uint32_t)idxAccessor.count;

				for (uint32_t k = 0; k < indexCount; k++)
				{
					uint32_t indexValue = 0;
					switch (idxAccessor.componentType)
					{
					case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
						indexValue = reinterpret_cast<const uint32_t*>(base)[k]; break;
					case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
						indexValue = reinterpret_cast<const uint16_t*>(base)[k]; break;
					case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
						indexValue = base[k]; break;
					default:
						EG_CORE_WARN("Unsupported index component type: {0}", idxAccessor.componentType);
						continue;
					}
					m_GlobalIndices.push_back(indexValue);
				}
			}
			else
			{
				// 无索引：生成顺序索引
				indexCount = (uint32_t)vertexCount;
				for (uint32_t k = 0; k < indexCount; k++)
					m_GlobalIndices.push_back(k);
			}

			// 创建 Submesh
			Mesh::Submesh submesh;
			submesh.FirstVertex = firstVertex;
			submesh.FirstIndex = firstIndex;
			submesh.IndexCount = indexCount;
			submesh.MaterialIndex = (primitive.material >= 0) ? (uint32_t)primitive.material : 0;
			submesh.Transform = transform;

			m_GlobalSubmeshes.push_back(submesh);
		}
	}

	// ------- 主加载入口 -------
	void Model::LoadGLTF(const std::string& path)
	{
		tinygltf::TinyGLTF loader;
		loader.SetImageLoader(tinygltf::LoadImageData, nullptr);

		tinygltf::Model model;
		std::string err, warn;

		bool ret = false;
		std::string pathLower = path;
		std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(), ::tolower);

		if (pathLower.find(".glb") != std::string::npos)
			ret = loader.LoadBinaryFromFile(&model, &err, &warn, path);
		else
			ret = loader.LoadASCIIFromFile(&model, &err, &warn, path);

		if (!warn.empty()) EG_CORE_WARN("glTF warning: {0}", warn);
		if (!ret)
		{
			EG_CORE_ERROR("Failed to load glTF file {0}: {1}", path, err);
			return;
		}

		// 目录路径
		size_t lastSlash = path.find_last_of("/\\");
		m_Directory = (lastSlash != std::string::npos) ? path.substr(0, lastSlash) : "";

		// 加载材质
		LoadGLTFMaterials(model);

		// 遍历默认场景
		int sceneIdx = (model.defaultScene >= 0) ? model.defaultScene : 0;
		if (sceneIdx < (int)model.scenes.size())
		{
			const auto& scene = model.scenes[sceneIdx];
			for (int nodeIdx : scene.nodes)
				ProcessGLTFNode(nodeIdx, model, glm::mat4(1.0f));
		}
		else
		{
			EG_CORE_WARN("glTF has no scenes, using fallback root nodes");
			for (size_t i = 0; i < model.nodes.size(); i++)
			{
				// 仅处理顶级节点（未被其他节点引用为 child）
				bool isChild = false;
				for (const auto& n : model.nodes)
				{
					for (int c : n.children)
						if (c == (int)i) { isChild = true; break; }
					if (isChild) break;
				}
				if (!isChild)
					ProcessGLTFNode((int)i, model, glm::mat4(1.0f));
			}
		}

		m_Mesh = std::make_shared<Mesh>(m_GlobalVertices, m_GlobalIndices, m_GlobalSubmeshes);
	}

}