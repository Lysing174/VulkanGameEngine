#pragma once

#include "Shader.h"
#include <glm/glm.hpp>
#include <memory>

namespace Engine {

    class Material
    {
    public:
        Material() {}
        Material(const std::shared_ptr<Shader>& shader);
        virtual ~Material() = default;

        void Bind();

        void SetColor(const glm::vec4& color) { m_Color = color; }
        glm::vec4 GetColor() const { return m_Color; }

        std::shared_ptr<Shader> GetShader() const { return m_Shader; }

        // 以后这里可以加 SetTexture()

    private:
        std::shared_ptr<Shader> m_Shader;

        glm::vec4 m_Color = { 1.0f, 1.0f, 1.0f, 1.0f }; 
        float m_Roughness = 0.5f;
        float m_Metallic = 0.0f;
    };
}