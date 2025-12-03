#include "pch.h"
#include "VulkanBuffer.h"

#include "Engine/Application.h"
#include "Platform/Vulkan/VulkanContext.h"

namespace Engine {
	// ==============================================================================
	// VertexBuffer 实现
	// ==============================================================================

	VulkanVertexBuffer::VulkanVertexBuffer(uint32_t size)
		: m_Size(size)
	{
		VulkanContext::Get()->CreateBuffer(size,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			m_Buffer,
			m_BufferMemory);
	}

	VulkanVertexBuffer::VulkanVertexBuffer(std::vector<Vertex> vertices, uint32_t size)
		: m_Size(size)
	{
		VkDevice device = VulkanContext::Get()->GetDevice();

		VkBuffer stagingBuffer;
		VkDeviceMemory stagingBufferMemory;
		VulkanContext::Get()->CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

		void* data;
		vkMapMemory(device, stagingBufferMemory, 0, size, 0, &data);
		memcpy(data, vertices.data(), (size_t)size);
		vkUnmapMemory(device, stagingBufferMemory);

		VulkanContext::Get()->CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_Buffer, m_BufferMemory);

		VulkanContext::Get()->CopyBuffer(stagingBuffer, m_Buffer, size);

		vkDestroyBuffer(device, stagingBuffer, nullptr);
		vkFreeMemory(device, stagingBufferMemory, nullptr);
	}

	VulkanVertexBuffer::~VulkanVertexBuffer()
	{
		VkDevice device = VulkanContext::Get()->GetDevice();
		vkDestroyBuffer(device, m_Buffer, nullptr);
		vkFreeMemory(device, m_BufferMemory, nullptr);
	}

	void VulkanVertexBuffer::Bind() const
	{
		VkBuffer vertexBuffers[] = { m_Buffer };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(VulkanContext::Get()->GetCurrentCommandBuffer(), 0, 1, vertexBuffers, offsets);

	}

	void VulkanVertexBuffer::Unbind() const
	{
	}

	void VulkanVertexBuffer::SetData(const void* data, uint32_t size)
	{
		VkDevice device = VulkanContext::Get()->GetDevice();
		void* mappedData;

		vkMapMemory(device, m_BufferMemory, 0, size, 0, &mappedData);
		memcpy(mappedData, data, size);
		vkUnmapMemory(device, m_BufferMemory);
	}


	// ==============================================================================
	// IndexBuffer 实现 (逻辑几乎一样)
	// ==============================================================================

	VulkanIndexBuffer::VulkanIndexBuffer(std::vector<uint32_t> indices, uint32_t count)
		: m_Count(count)
	{
		VkDevice device = VulkanContext::Get()->GetDevice();

		VkDeviceSize bufferSize = sizeof(indices[0]) * count;

		VkBuffer stagingBuffer;
		VkDeviceMemory stagingBufferMemory;
		VulkanContext::Get()->CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

		void* data;
		vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
		memcpy(data, indices.data(), (size_t)bufferSize);
		vkUnmapMemory(device, stagingBufferMemory);

		VulkanContext::Get()->CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_Buffer, m_BufferMemory);

		VulkanContext::Get()->CopyBuffer(stagingBuffer, m_Buffer, bufferSize);

		vkDestroyBuffer(device, stagingBuffer, nullptr);
		vkFreeMemory(device, stagingBufferMemory, nullptr);

	}

	VulkanIndexBuffer::~VulkanIndexBuffer()
	{
		VkDevice device = VulkanContext::Get()->GetDevice();
		vkDestroyBuffer(device, m_Buffer, nullptr);
		vkFreeMemory(device, m_BufferMemory, nullptr);
	}

	void VulkanIndexBuffer::Bind() const 
	{
		vkCmdBindIndexBuffer(VulkanContext::Get()->GetCurrentCommandBuffer(), m_Buffer, 0, VK_INDEX_TYPE_UINT32);
	}
	void VulkanIndexBuffer::Unbind() const {}

}