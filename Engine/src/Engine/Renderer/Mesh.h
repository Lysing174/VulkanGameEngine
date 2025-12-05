#pragma once

#include "Engine/Renderer/Buffer.h"
#include <vector>

namespace Engine {
    class Mesh
    {
    public:
        Mesh() {}
        template<typename T>
        Mesh(const std::vector<T>& vertices, const std::vector<uint32_t>& indices)
        {
            uint32_t size = (uint32_t)vertices.size() * sizeof(T);
            m_VertexBuffer = VertexBuffer::Create(vertices);
            m_IndexBuffer = IndexBuffer::Create(indices, (uint32_t)indices.size());
        }
        virtual ~Mesh() = default;

        void Bind();

        std::shared_ptr<VertexBuffer> GetVertexBuffer() const { return m_VertexBuffer; }
        std::shared_ptr<IndexBuffer> GetIndexBuffer() const { return m_IndexBuffer; }
        uint32_t GetIndexCount() const { return m_IndexBuffer->GetCount(); }

        static Mesh CreateCube();

    private:
        std::shared_ptr<VertexBuffer> m_VertexBuffer;
        std::shared_ptr<IndexBuffer> m_IndexBuffer;
    };
}