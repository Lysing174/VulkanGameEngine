#include "pch.h"
#include "VulkanBuffer.h"

#include "Engine/Core/Application.h"
#include "Platform/Vulkan/VulkanContext.h"

namespace Engine {

	// ==========================================================================
	// VulkanBuffer 实现
	// ==========================================================================

	VulkanBuffer::VulkanBuffer() = default;

	VulkanBuffer::VulkanBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
		: m_Size(size)
	{
		auto* ctx = VulkanContext::Get();
		VkDevice device = ctx->GetDevice();

		// 1. 创建 Buffer
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateBuffer(device, &bufferInfo, nullptr, &m_Buffer) != VK_SUCCESS)
			throw std::runtime_error("failed to create buffer!");

		// 2. 分配内存
		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements(device, m_Buffer, &memRequirements);

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = ctx->FindMemoryType(memRequirements.memoryTypeBits, properties);

		if (vkAllocateMemory(device, &allocInfo, nullptr, &m_Memory) != VK_SUCCESS)
			throw std::runtime_error("failed to allocate buffer memory!");

		// 3. 绑定
		vkBindBufferMemory(device, m_Buffer, m_Memory, 0);
	}

	VulkanBuffer::~VulkanBuffer()
	{
		Destroy();
	}

	VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept
		: m_Buffer(other.m_Buffer)
		, m_Memory(other.m_Memory)
		, m_Size(other.m_Size)
	{
		other.m_Buffer = VK_NULL_HANDLE;
		other.m_Memory = VK_NULL_HANDLE;
		other.m_Size = 0;
	}

	VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept
	{
		if (this != &other)
		{
			Destroy();
			m_Buffer = other.m_Buffer;
			m_Memory = other.m_Memory;
			m_Size = other.m_Size;
			other.m_Buffer = VK_NULL_HANDLE;
			other.m_Memory = VK_NULL_HANDLE;
			other.m_Size = 0;
		}
		return *this;
	}

	void VulkanBuffer::SetData(const void* data, VkDeviceSize size, VkDeviceSize offset)
	{
		if (size == 0) return;
		if (data == nullptr || offset > m_Size || size > m_Size - offset)
			throw std::invalid_argument("VulkanBuffer::SetData: invalid upload range or data!");

		VkDevice device = VulkanContext::Get()->GetDevice();

		// 映射整块内存 (offset=0, size=VK_WHOLE_SIZE): 这样映射末端 = 整个内存对象末尾,
		// 后续用 VK_WHOLE_SIZE flush 时即满足 VUID-VkMappedMemoryRange-size-01389
		// (映射末端必须是 nonCoherentAtomSize 倍数 或 等于内存末尾).
		// 若只映射 [offset, offset+size) 一段再用 VK_WHOLE_SIZE flush, 映射末端非 atom 倍数
		// 且不等内存末尾 → 触发 01389.
		void* mapped;
		if (vkMapMemory(device, m_Memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS)
			throw std::runtime_error("VulkanBuffer::SetData: failed to map buffer memory!");
		memcpy((uint8_t*)mapped + offset, data, (size_t)size);

		// 整块映射 + flush 整块内存: 映射末端即内存末尾, VK_WHOLE_SIZE 合法, 无需按 atom 对齐.
		VkMappedMemoryRange range = {};
		range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		range.memory = m_Memory;
		range.offset = 0;
		range.size = VK_WHOLE_SIZE;
		VkResult flushResult = vkFlushMappedMemoryRanges(device, 1, &range);

		vkUnmapMemory(device, m_Memory);
		if (flushResult != VK_SUCCESS)
			throw std::runtime_error("VulkanBuffer::SetData: failed to flush buffer memory!");
	}

	void VulkanBuffer::SetDataStaged(const void* data, VkDeviceSize size, VkDeviceSize offset)
	{
		if (size == 0) return;
		if (data == nullptr || offset > m_Size || size > m_Size - offset)
			throw std::invalid_argument("VulkanBuffer::SetDataStaged: invalid upload range or data!");

		VulkanBuffer staging(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		staging.SetData(data, size);
		CopyBuffer(staging, *this, size, 0, offset);
	}

	VulkanBuffer::Handles VulkanBuffer::Release()
	{
		Handles h = { m_Buffer, m_Memory, m_Size };
		m_Buffer = VK_NULL_HANDLE;
		m_Memory = VK_NULL_HANDLE;
		m_Size = 0;
		return h;
	}

	void VulkanBuffer::Destroy()
	{
		if (m_Buffer != VK_NULL_HANDLE)
		{
			VkDevice device = VulkanContext::Get()->GetDevice();
			vkDestroyBuffer(device, m_Buffer, nullptr);
			vkFreeMemory(device, m_Memory, nullptr);
			m_Buffer = VK_NULL_HANDLE;
			m_Memory = VK_NULL_HANDLE;
			m_Size = 0;
		}
	}

	void VulkanBuffer::CopyBuffer(VulkanBuffer& src, VulkanBuffer& dst, VkDeviceSize size,
		VkDeviceSize srcOffset, VkDeviceSize dstOffset)
	{
		if (size == 0) return;
		if (srcOffset > src.GetSize() || size > src.GetSize() - srcOffset ||
			dstOffset > dst.GetSize() || size > dst.GetSize() - dstOffset)
			throw std::invalid_argument("VulkanBuffer::CopyBuffer: invalid copy range!");

		auto* ctx = VulkanContext::Get();
		VkCommandBuffer cmd = ctx->BeginSingleTimeCommands();

		VkBufferCopy copyRegion = {};
		copyRegion.srcOffset = srcOffset;
		copyRegion.dstOffset = dstOffset;
		copyRegion.size = size;
		vkCmdCopyBuffer(cmd, src.GetBuffer(), dst.GetBuffer(), 1, &copyRegion);

		VkBufferMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.buffer = dst.GetBuffer();
		barrier.offset = dstOffset;
		barrier.size = size;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			0, 0, nullptr, 1, &barrier, 0, nullptr);

		ctx->EndSingleTimeCommands(cmd);
	}

	// ==========================================================================
	// VulkanUniformBuffer 实现
	// ==========================================================================

	VulkanUniformBuffer::VulkanUniformBuffer(VkDeviceSize size)
		: m_Buffer(size,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
	{
	}

	void VulkanUniformBuffer::SetData(const void* data, VkDeviceSize size, VkDeviceSize offset)
	{
		m_Buffer.SetData(data, size, offset);
	}

	// ==========================================================================
	// VulkanVertexBuffer 实现
	// ==========================================================================

	VulkanVertexBuffer::VulkanVertexBuffer(uint32_t size)
		: m_Buffer(size,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
	{
	}

	VulkanVertexBuffer::VulkanVertexBuffer(void* data, uint32_t size)
	{
		// Staging buffer
		VulkanBuffer staging(size,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		staging.SetData(data, size);

		// Device-local buffer
		m_Buffer = VulkanBuffer(size,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		VulkanBuffer::CopyBuffer(staging, m_Buffer, size);
	}

	VulkanVertexBuffer::~VulkanVertexBuffer() = default;

	void VulkanVertexBuffer::Bind() const
	{
		VkBuffer vertexBuffers[] = { m_Buffer.GetBuffer() };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(VulkanContext::Get()->GetCurrentCommandBuffer(), 0, 1, vertexBuffers, offsets);
	}

	void VulkanVertexBuffer::Unbind() const {}

	void VulkanVertexBuffer::SetData(const void* data, uint32_t size)
	{
		m_Buffer.SetData(data, size);
	}

	VkFormat VulkanVertexBuffer::ShaderDataTypeToVulkanFormat(ShaderDataType type)
	{
		switch (type)
		{
		case ShaderDataType::Float:  return VK_FORMAT_R32_SFLOAT;
		case ShaderDataType::Float2: return VK_FORMAT_R32G32_SFLOAT;
		case ShaderDataType::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
		case ShaderDataType::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
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

	VkPipelineVertexInputStateCreateInfo VulkanVertexBuffer::CreateVertexInputInfo(
		const BufferLayout& layout,
		std::vector<VkVertexInputBindingDescription>& bindings,
		std::vector<VkVertexInputAttributeDescription>& attributes)
	{
		VkVertexInputBindingDescription bindingDesc{};
		bindingDesc.binding = 0;
		bindingDesc.stride = layout.GetStride();
		bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		bindings.push_back(bindingDesc);

		uint32_t location = 0;
		for (const auto& element : layout)
		{
			if (element.Type == ShaderDataType::Mat3)
			{
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
				VkVertexInputAttributeDescription attributeDesc{};
				attributeDesc.binding = 0;
				attributeDesc.location = location;
				attributeDesc.format = ShaderDataTypeToVulkanFormat(element.Type);
				attributeDesc.offset = element.Offset;
				attributes.push_back(attributeDesc);
				location++;
			}
		}

		VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = bindings.data();
		vertexInputInfo.vertexAttributeDescriptionCount = (uint32_t)attributes.size();
		vertexInputInfo.pVertexAttributeDescriptions = attributes.data();

		return vertexInputInfo;
	}

	// ==========================================================================
	// VulkanIndexBuffer 实现
	// ==========================================================================

	VulkanIndexBuffer::VulkanIndexBuffer(std::vector<uint32_t> indices, uint32_t count)
		: m_Count(count)
	{
		VkDeviceSize bufferSize = sizeof(uint32_t) * count;

		// Staging buffer
		VulkanBuffer staging(bufferSize,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		staging.SetData(indices.data(), bufferSize);

		// Device-local buffer
		m_Buffer = VulkanBuffer(bufferSize,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		VulkanBuffer::CopyBuffer(staging, m_Buffer, bufferSize);
	}

	VulkanIndexBuffer::~VulkanIndexBuffer() = default;

	void VulkanIndexBuffer::Bind() const
	{
		vkCmdBindIndexBuffer(VulkanContext::Get()->GetCurrentCommandBuffer(),
			m_Buffer.GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
	}

	void VulkanIndexBuffer::Unbind() const {}

	// ==========================================================================
	// VulkanShaderStorageBuffer 实现
	// ==========================================================================

	VulkanShaderStorageBuffer::VulkanShaderStorageBuffer(uint32_t size)
		: m_Buffer(size,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
	{
	}

	VulkanShaderStorageBuffer::VulkanShaderStorageBuffer(const void* data, uint32_t size)
	{
		// Staging buffer
		VulkanBuffer staging(size,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		staging.SetData(data, size);

		// Device-local buffer
		m_Buffer = VulkanBuffer(size,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		VulkanBuffer::CopyBuffer(staging, m_Buffer, size);
	}

	VulkanShaderStorageBuffer::~VulkanShaderStorageBuffer() = default;

	void VulkanShaderStorageBuffer::Bind(uint32_t binding) const
	{
		// Descriptor set allocation should be handled at a higher level.
	}

	void VulkanShaderStorageBuffer::SetData(const void* data, uint32_t size)
	{
		m_Buffer.SetData(data, size);
	}

}
