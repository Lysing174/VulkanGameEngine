#pragma once

#include <string>
#include <unordered_map>
#include <Engine/Renderer/Buffer.h>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace Engine {

	class Shader
	{
	public:
		virtual ~Shader() = default;

		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		virtual void CreatePipeline(const BufferLayout& layout) = 0;

		//virtual void SetInt(const std::string& name, int value) = 0;
		//virtual void SetFloat3(const std::string& name, const glm::vec3& value) = 0;
		//virtual void SetMat4(const std::string& name, const glm::mat4& value) = 0;

		virtual const std::string& GetName() const = 0;
		virtual uint32_t GetRendererID() const = 0;
		virtual const VkPipelineLayout GetPipelineLayout() const = 0;

		uint32_t GetBinding(const std::string& name) const
		{
			// 临时硬编码示例 (对应你之前的 Shader)
			if (name == "u_AlbedoMap") return 0;
			if (name == "u_NormalMap") return 1;
			if (name == "u_MetalRoughAO") return 2;

			EG_CORE_WARN("Shader resource not found: {0}", name);
			return -1;
		}

		static std::shared_ptr<Shader> Create(const std::string& vertexSrc, const std::string& fragmentSrc);

	};

	class ShaderLibrary
	{
	public:
		//void Add(const std::string& name, const std::shared_ptr<Shader>& shader);
		void Add(const std::shared_ptr<Shader>& shader); 

		std::shared_ptr<Shader> Load(const std::string& vertexSrc, const std::string& fragmentSrc); 
		//std::shared_ptr<Shader> Load(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc);

		std::shared_ptr<Shader> Get(const std::string& name);
		bool Exists(const std::string& name) const;

	private:
		std::unordered_map<std::string, std::shared_ptr<Shader>> m_Shaders;
	};
}