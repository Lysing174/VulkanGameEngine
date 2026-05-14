#pragma once

#include "Engine/Renderer/Buffer.h"
#include <happly.h>
#include <glm/glm.hpp>

#include <vector>
#include <string>
#include <limits>

namespace Engine {

    // CPU-side data (variable-length shRest)
    struct GaussianData
    {
        glm::vec3 position  = { 0.0f, 0.0f, 0.0f };
        glm::vec3 normal    = { 0.0f, 0.0f, 0.0f };
        glm::vec3 shDC      = { 0.0f, 0.0f, 0.0f };     // f_dc_0, f_dc_1, f_dc_2
        float     opacity   = 0.0f;                      // raw opacity (needs sigmoid)
        glm::vec3 scale     = { 0.0f, 0.0f, 0.0f };      // scale_0, scale_1, scale_2 (needs exp)
        glm::vec4 rotation  = { 1.0f, 0.0f, 0.0f, 0.0f }; // rot_0=w, rot_1=x, rot_2=y, rot_3=z

        // Higher-order SH coefficients (f_rest_0 ... f_rest_N)
        std::vector<float> shRest;
    };

    // GPU-side data (fixed-size, std430 compatible)
    struct GaussianDataGPU
    {
        glm::vec3 position;  float _pad0;
        glm::vec3 normal;    float _pad1;
        glm::vec3 shDC;     float opacity;
        glm::vec3 scale;    float _pad2;
        glm::vec4 rotation; // w, x, y, z
    };

    class GaussianModel : public std::enable_shared_from_this<GaussianModel>
    {
    public:
        // Use Create() instead of constructor directly — auto-registers with Renderer
        static std::shared_ptr<GaussianModel> Create(const std::string& path);

        ~GaussianModel() = default;

        const std::vector<GaussianData>& GetGaussians() const { return m_Gaussians; }
        uint32_t GetGaussianCount() const { return (uint32_t)m_Gaussians.size(); }

        // Global SSBO offset (assigned by Renderer during registration)
        uint32_t GetGlobalOffset() const { return m_GlobalOffset; }
        void SetGlobalOffset(uint32_t offset) { m_GlobalOffset = offset; }
        bool IsRegistered() const { return m_GlobalOffset != UINT32_MAX; }

        // Get flattened GPU data for uploading to the global SSBO
        std::vector<GaussianDataGPU> GetGPUData() const;

    private:
        GaussianModel(const std::string& path);
        void LoadModel(const std::string& path);

    private:
        std::vector<GaussianData> m_Gaussians;
        uint32_t m_GlobalOffset = UINT32_MAX; // offset into global SSBO (element index)
    };

}
