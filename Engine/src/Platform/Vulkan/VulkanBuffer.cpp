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

    VulkanVertexBuffer::VulkanVertexBuffer(void* data, uint32_t size)
    {
        m_Size = size;

        VkDevice device = VulkanContext::Get()->GetDevice();

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        VulkanContext::Get()->CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

        void* memoryData;
        vkMapMemory(device, stagingBufferMemory, 0, size, 0, &memoryData);
        memcpy(memoryData, data, (size_t)size);
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

    // 把 Hazel 的数据类型转换成 Vulkan 的格式
    VkFormat VulkanVertexBuffer::ShaderDataTypeToVulkanFormat(ShaderDataType type)
    {
        switch (type)
        {
        case ShaderDataType::Float:  return VK_FORMAT_R32_SFLOAT;
        case ShaderDataType::Float2: return VK_FORMAT_R32G32_SFLOAT;
        case ShaderDataType::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
        case ShaderDataType::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;

            // 矩阵本质上是多个向量的组合，返回其基向量的格式
        case ShaderDataType::Mat3:   return VK_FORMAT_R32G32B32_SFLOAT;
        case ShaderDataType::Mat4:   return VK_FORMAT_R32G32B32A32_SFLOAT;

        case ShaderDataType::Int:    return VK_FORMAT_R32_SINT;
        case ShaderDataType::Int2:   return VK_FORMAT_R32G32_SINT;
        case ShaderDataType::Int3:   return VK_FORMAT_R32G32B32_SINT;
        case ShaderDataType::Int4:   return VK_FORMAT_R32G32B32A32_SINT;
        case ShaderDataType::Bool:   return VK_FORMAT_R8_UINT;
        }

        return VK_FORMAT_UNDEFINED;
    }

    // 核心转换逻辑
    VkPipelineVertexInputStateCreateInfo VulkanVertexBuffer::CreateVertexInputInfo(
        const BufferLayout& layout,
        std::vector<VkVertexInputBindingDescription>& bindings,
        std::vector<VkVertexInputAttributeDescription>& attributes)
    {
        // 1. 设置 Binding (整个 Buffer 的信息)
        VkVertexInputBindingDescription bindingDesc{};
        bindingDesc.binding = 0;
        bindingDesc.stride = layout.GetStride(); 
        bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        bindings.push_back(bindingDesc);

        // 2. 设置 Attributes (每个属性的信息)
        uint32_t location = 0;
        for (const auto& element : layout)
        {
            if (element.Type == ShaderDataType::Mat3)
            {
                // Mat3 需要循环 3 次，创建 3 个 vec3 属性
                for (int i = 0; i < 3; i++)
                {
                    VkVertexInputAttributeDescription attribute{};
                    attribute.location = location;
                    attribute.binding = 0;
                    attribute.format = ShaderDataTypeToVulkanFormat(element.Type);
                    attribute.offset = element.Offset + sizeof(float) * 3 * i;
                    attributes.push_back(attribute);
                    location++;
                }
            }
            else if (element.Type == ShaderDataType::Mat4)
            {
                // Mat4 需要循环 4 次，创建 4 个 vec4 属性
                for (int i = 0; i < 4; i++)
                {
                    VkVertexInputAttributeDescription attribute{};
                    attribute.location = location;
                    attribute.binding = 0;
                    attribute.format = ShaderDataTypeToVulkanFormat(element.Type);
                    attribute.offset = element.Offset + sizeof(float) * 4 * i;
                    attributes.push_back(attribute);
                    location++;
                }
            }
            else
            {
                // 普通类型直接添加
                VkVertexInputAttributeDescription attributeDesc{};
                attributeDesc.binding = 0;
                attributeDesc.location = location;
                attributeDesc.format = ShaderDataTypeToVulkanFormat(element.Type);
                attributeDesc.offset = element.Offset; // Hazel 帮你算好了 Offset
                attributes.push_back(attributeDesc);
                location++;
            }
        }
        // 3. 返回填写好的 Info
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = bindings.data();
        vertexInputInfo.vertexAttributeDescriptionCount = (uint32_t)attributes.size();
        vertexInputInfo.pVertexAttributeDescriptions = attributes.data();

        return vertexInputInfo;
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