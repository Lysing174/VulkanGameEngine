#pragma once

#include "Engine/Renderer/Buffer.h"
#include <vector>

namespace Engine {
    class Mesh
    {
    public:
        struct Submesh
        {
            uint32_t FirstVertex;
            uint32_t FirstIndex;
            uint32_t MaterialIndex;
            uint32_t IndexCount;

            // 如果是 glTF 导入的，可能还有 AABB 包围盒等
            glm::mat4 Transform; 
        };
        Mesh() {}
        template<typename T>
        Mesh(const std::vector<T>& vertices, const std::vector<uint32_t>& indices, std::vector<Submesh> submeshs = {})
        {
            uint32_t size = (uint32_t)vertices.size() * sizeof(T);
            m_VertexBuffer = VertexBuffer::Create(vertices);
            m_IndexBuffer = IndexBuffer::Create(indices, (uint32_t)indices.size());

            m_Submeshes=submeshs;
        }
        virtual ~Mesh() = default;

        void Bind();

        void SetSubmeshes(const std::vector<Submesh>& submeshes) { m_Submeshes = submeshes; }
        const std::vector<Submesh>& GetSubmeshes() const { return m_Submeshes; }
        std::shared_ptr<VertexBuffer> GetVertexBuffer() const { return m_VertexBuffer; }
        std::shared_ptr<IndexBuffer> GetIndexBuffer() const { return m_IndexBuffer; }

        static Mesh CreateCube();

    private:
        std::vector<Submesh> m_Submeshes;

        std::shared_ptr<VertexBuffer> m_VertexBuffer;
        std::shared_ptr<IndexBuffer> m_IndexBuffer;
    };
}