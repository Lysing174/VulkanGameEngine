#pragma once

#include "Engine/Renderer/Buffer.h"
#include <happly.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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

    // GPU-side data for 3DGS splat rendering (fixed-size, std430 compatible)
    // Precomputed: color (from SH DC + sigmoid opacity), 3D covariance (from scale + rotation)
    struct GaussianDataGPU
    {
        glm::vec4 posAndOpacity;   // xyz = position, w = sigmoid(opacity)
        glm::vec4 color;           // rgb = SH DC color, w = unused
        float cov3d[6];            // symmetric 3x3 upper triangle: M00,M01,M02,M11,M12,M22
        uint32_t modelIndex;       // index into model transform SSBO
        float _pad1;               // padding to 64 bytes
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

        // Model index into the ModelTransform SSBO (assigned by Renderer)
        uint32_t GetModelIndex() const { return m_ModelIndex; }
        void SetModelIndex(uint32_t index) { m_ModelIndex = index; }

        // Get flattened GPU data for uploading to the global SSBO
        std::vector<GaussianDataGPU> GetGPUData() const;

    private:
        GaussianModel(const std::string& path);
        void LoadModel(const std::string& path);

    private:
        std::vector<GaussianData> m_Gaussians;
        uint32_t m_GlobalOffset = UINT32_MAX; // offset into global SSBO (element index)
        uint32_t m_ModelIndex = 0;            // index into ModelTransform SSBO
    };

}
