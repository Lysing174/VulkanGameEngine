#pragma once
#include <Engine/Renderer/RendererAPI.h>

namespace Engine
{
	class VertexBuffer
	{
	public:
		virtual ~VertexBuffer() = default;

		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		virtual void SetData(const void* data, uint32_t size) = 0;
		static std::shared_ptr<VertexBuffer> Create(std::vector<Vertex> vertices, uint32_t size);

	};

	class IndexBuffer
	{
	public:
		virtual ~IndexBuffer() = default;

		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		virtual uint32_t GetCount() const = 0;
		static std::shared_ptr<IndexBuffer> Create(std::vector<uint32_t> indices, uint32_t count);

	};
}

