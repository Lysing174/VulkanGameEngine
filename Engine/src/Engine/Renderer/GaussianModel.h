#pragma once

#include "Engine/Renderer/Buffer.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>
#include <string>
#include <limits>

namespace Engine {

    // 球谐阶数 (degree). 3 = 三阶, 共 (3+1)^2 = 16 个基函数/通道, RGB 合计 48 个 float.
    constexpr uint32_t SH_DEGREE = 3;
    constexpr uint32_t SH_BASIS  = (SH_DEGREE + 1) * (SH_DEGREE + 1); // 16
    constexpr uint32_t SH_TOTAL  = SH_BASIS * 3;                       // 48

    // Shared CPU cache / GPU layout for 3DGS splat rendering (std430 compatible).
    // 颜色不再在 CPU 烘焙: 完整球谐系数上传显存, 由 shader 用视线方向实时求值 (3DGS 原论文做法).
    // 3D 协方差仍预计算 (from scale + rotation).
    struct GaussianDataGPU
    {
        glm::vec4 posAndOpacity;        // 16 B  xyz = position, w = sigmoid(opacity)
        float     sh[SH_TOTAL];         // 192 B 完整三阶球谐系数 (std430, 逐 float 紧密排列)
        float     cov3d[6];             // 24 B  对称 3x3 上三角: M00,M01,M02,M11,M12,M22
        uint32_t  modelIndex;           //  4 B  每实例变换槽位, 上传时填充 (非资产状态)
        float     _pad1;                //  4 B  补齐到 240 B
    };

    static_assert(std::is_standard_layout_v<GaussianDataGPU> && std::is_trivially_copyable_v<GaussianDataGPU>);
    static_assert(sizeof(GaussianDataGPU) == 240);
    static_assert(offsetof(GaussianDataGPU, posAndOpacity) == 0);
    static_assert(offsetof(GaussianDataGPU, sh) == 16);
    static_assert(offsetof(GaussianDataGPU, cov3d) == 208);
    static_assert(offsetof(GaussianDataGPU, modelIndex) == 232);
    static_assert(offsetof(GaussianDataGPU, _pad1) == 236);

    // Gaussian asset: immutable, upload-ready splat data loaded from disk (.ply/.splat).
    // Cached by AssetLibrary<GaussianModel> keyed by asset UUID — the heavy parse happens once
    // and the result is shared by every GaussianComponent referencing the same UUID.
    //
    // Per-instance GPU state (global SSBO offset / transform index) is intentionally NOT
    // stored here: it belongs to the instance (component) and the global Gaussian renderer,
    // so a cached GaussianModel can be reused by multiple placed instances without conflict.
    class GaussianModel
    {
    public:
        // 用 Create() 而非直接构造：从磁盘加载并解析 splat 文件。
        static std::shared_ptr<GaussianModel> Create(const std::string& path);

        ~GaussianModel() = default;

        // modelIndex is zero in the asset cache; the renderer fills it in staging per instance.
        const std::vector<GaussianDataGPU>& GetGaussians() const { return m_Gaussians; }
        uint32_t GetGaussianCount() const { return (uint32_t)m_Gaussians.size(); }

        // Copy directly into caller-owned staging space and fill this instance's transform slot.
        void CopyGPUData(GaussianDataGPU* destination, uint32_t modelIndex) const;

    private:
        GaussianModel(const std::string& path);
        void LoadModel(const std::string& path);

    private:
        std::vector<GaussianDataGPU> m_Gaussians;
    };

}
