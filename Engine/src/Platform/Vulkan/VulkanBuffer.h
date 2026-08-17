#pragma once

#include "Engine/Renderer/Buffer.h"
#include <vulkan/vulkan.h>

namespace Engine {

	// ==========================================================================
	// VulkanBuffer — RAII wrapper for VkBuffer + VkDeviceMemory
	// ==========================================================================
	class VulkanBuffer
	{
	public:
		VulkanBuffer();
		VulkanBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
		~VulkanBuffer();

		// Move-only
		VulkanBuffer(VulkanBuffer&& other) noexcept;
		VulkanBuffer& operator=(VulkanBuffer&& other) noexcept;
		VulkanBuffer(const VulkanBuffer&) = delete;
		VulkanBuffer& operator=(const VulkanBuffer&) = delete;

		void SetData(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

		// Release ownership: transfer handles out, caller is responsible for destruction
		struct Handles { VkBuffer Buffer; VkDeviceMemory Memory; VkDeviceSize Size; };
		Handles Release();

		VkBuffer GetBuffer() const { return m_Buffer; }
		VkDeviceMemory GetMemory() const { return m_Memory; }
		VkDeviceSize GetSize() const { return m_Size; }
		bool IsValid() const { return m_Buffer != VK_NULL_HANDLE; }

		void Destroy();

		// Copy GPU buffer via single-time command (staging → device-local)
		static void CopyBuffer(VulkanBuffer& src, VulkanBuffer& dst, VkDeviceSize size);

	private:
		VkBuffer m_Buffer = VK_NULL_HANDLE;
		VkDeviceMemory m_Memory = VK_NULL_HANDLE;
		VkDeviceSize m_Size = 0;
	};

	// ==========================================================================
	// VulkanUniformBuffer — Uniform buffer for per-frame / material data
	// ==========================================================================
	class VulkanUniformBuffer
	{
	public:
		VulkanUniformBuffer() = default;
		explicit VulkanUniformBuffer(VkDeviceSize size);
		~VulkanUniformBuffer() = default;

		void SetData(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

		VkBuffer GetBuffer() const { return m_Buffer.GetBuffer(); }
		VkDeviceMemory GetMemory() const { return m_Buffer.GetMemory(); }
		VkDeviceSize GetSize() const { return m_Buffer.GetSize(); }
		bool IsValid() const { return m_Buffer.IsValid(); }

		void Destroy() { m_Buffer.Destroy(); }
		VulkanBuffer::Handles Release() { return m_Buffer.Release(); }

	private:
		VulkanBuffer m_Buffer;
	};

	// ==========================================================================
	// VulkanVertexBuffer
	// ==========================================================================
	class VulkanVertexBuffer : public VertexBuffer
	{
	public:
		VulkanVertexBuffer(uint32_t size);
		VulkanVertexBuffer(void* data, uint32_t size);
		virtual ~VulkanVertexBuffer();

		virtual void Bind() const override;
		virtual void Unbind() const override;

		virtual void SetData(const void* data, uint32_t size) override;
		virtual const std::shared_ptr<BufferLayout> GetLayout() const override { return m_Layout; }
		virtual void SetLayout(const std::shared_ptr<BufferLayout> layout) override { m_Layout = layout; }

		static VkPipelineVertexInputStateCreateInfo CreateVertexInputInfo(
			const BufferLayout& layout,
			std::vector<VkVertexInputBindingDescription>& bindings,
			std::vector<VkVertexInputAttributeDescription>& attributes);

	private:
		static VkFormat ShaderDataTypeToVulkanFormat(ShaderDataType type);

	private:
		VulkanBuffer m_Buffer;
		std::shared_ptr<BufferLayout> m_Layout;
	};

	// ==========================================================================
	// VulkanIndexBuffer
	// ==========================================================================
	class VulkanIndexBuffer : public IndexBuffer
	{
	public:
		VulkanIndexBuffer(std::vector<uint32_t> indices, uint32_t count);
		virtual ~VulkanIndexBuffer();

		virtual void Bind() const override;
		virtual void Unbind() const override;

		virtual uint32_t GetCount() const override { return m_Count; }

	private:
		VulkanBuffer m_Buffer;
		uint32_t m_Count;
	};

	// ==========================================================================
	// VulkanShaderStorageBuffer
	// ==========================================================================
	class VulkanShaderStorageBuffer : public ShaderStorageBuffer
	{
	public:
		VulkanShaderStorageBuffer(uint32_t size);
		VulkanShaderStorageBuffer(const void* data, uint32_t size);
		virtual ~VulkanShaderStorageBuffer();

		virtual void Bind(uint32_t binding) const override;
		virtual void SetData(const void* data, uint32_t size) override;

	private:
		VulkanBuffer m_Buffer;
	};

}
