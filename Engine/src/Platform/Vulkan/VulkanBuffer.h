#pragma once

#include "Engine/Renderer/Buffer.h"
#include <vulkan/vulkan.h>

namespace Engine {

	class VulkanVertexBuffer : public VertexBuffer
	{
	public:
		VulkanVertexBuffer(uint32_t size);
		VulkanVertexBuffer(std::vector<Vertex> vertices, uint32_t size);
		virtual ~VulkanVertexBuffer();

		virtual void Bind() const override;
		virtual void Unbind() const override;

		virtual void SetData(const void* data, uint32_t size) override;
		//virtual const BufferLayout& GetLayout() const override { return m_Layout; }
		//virtual void SetLayout(const BufferLayout& layout) override { m_Layout = layout; }

		VkBuffer GetVulkanBuffer() const { return m_Buffer; }

	private:

	private:
		VkBuffer m_Buffer;
		VkDeviceMemory m_BufferMemory;
		//BufferLayout m_Layout;

		uint32_t m_Size;
	};

	class VulkanIndexBuffer : public IndexBuffer
	{
	public:
		VulkanIndexBuffer(std::vector<uint32_t> indices, uint32_t count);
		virtual ~VulkanIndexBuffer();

		virtual void Bind() const override;
		virtual void Unbind() const override;

		virtual uint32_t GetCount() const { return m_Count; }
		VkBuffer GetVulkanBuffer() const { return m_Buffer; }

	private:
	private:
		VkBuffer m_Buffer;
		VkDeviceMemory m_BufferMemory;
		uint32_t m_Count;
	};

}