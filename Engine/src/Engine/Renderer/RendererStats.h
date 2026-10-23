#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Engine {

    enum class RenderStage : uint32_t
    {
        Shadow, Mesh, GaussianSort, Gaussian, Count
    };

    constexpr size_t RenderStageCount = static_cast<size_t>(RenderStage::Count);

    struct GpuTimings
    {
        std::array<double, RenderStageCount> Milliseconds = {};
        bool Supported = false;
        bool Valid = false;
    };

    struct RendererStatistics
    {
        uint64_t SceneGaussianCount = 0;
        uint64_t DrawnGaussianCount = 0;
        uint64_t GaussianDataBytes = 0;
        uint64_t SceneVertexCount = 0;
        uint64_t DrawnVertexCount = 0;
        uint64_t PointLightCount = 0;
        uint64_t DirectLightCount = 0;
        double CpuTimeMs = 0.0;
        double CpuWaitGpuTimeMs = 0.0;
        GpuTimings GPU;
        bool HasCompletedFrame = false;
    };
}
