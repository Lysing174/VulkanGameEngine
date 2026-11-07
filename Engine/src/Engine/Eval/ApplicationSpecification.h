#pragma once

#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/Core/Window.h"
#include "Engine/Renderer/DatasetCamera.h"
#include "Engine/Renderer/FrameBuffer.h"
#include "Engine/Renderer/GaussianModel.h"
#include "Engine/Renderer/Model.h"
#include "Engine/Renderer/RendererStats.h"
#include "Engine/Scene/UUID.h"
#include "Platform/Vulkan/VulkanBuffer.h"

namespace Engine
{
    // 离线测试模式配置 —— 全部在代码里写死, 想改路径/参数就直接改下面的初值。
    struct TestSpecification
    {
        // 高斯 .ply 路径 (可空: 留空 "" 表示不绘制高斯)
        std::string GaussianPly      = "D:/source/python_projects/3DGS/output/baseline/playroom/point_cloud/iteration_30000/point_cloud.ply";
        //std::string GaussianPly      =  "D:/source/repos/VulkanEngine/paper_reproduction/lmg/output/playroom/distortion_160000_sh3_r1/point_cloud/iteration_15000/point_cloud.ply";
        glm::mat4   GaussianTransform = glm::mat4(1.0f);    // 高斯整体变换 (无则不改)

        // 网格模型路径 (.gltf/.glb/.obj, 可空: 留空 "" 表示不绘制 mesh)
        std::string MeshPath      = "D:/source/python_projects/SuGaR/output/lmg_assets/playroom/playroom.obj";
        glm::mat4   MeshTransform = glm::mat4(1.0f);    // 网格整体变换 (无则不改)

        // COLMAP 内参 / 位姿 (必填)
        std::string ColmapCamerasTxt = "D:/source/python_projects/3DGS/data/deep_blending/playroom/sparse/0/cameras.bin";
        std::string ColmapImagesTxt  = "D:/source/python_projects/3DGS/data/deep_blending/playroom/sparse/0/images.bin";

        // 测试集图片目录: 留空 "" 表示渲染 images.txt 里全部位姿;
        // 填了目录则只渲染该目录下真实存在的图片 (用于从训练+测试混合的位姿里筛出测试集)
        std::string ImageDir         = "D:/source/python_projects/3DGS/data/deep_blending/playroom/test_images";

        // GT (真值) 图片目录: 每张渲染图会与 GroundTruthDir/<相机名> 比对, 计算 PSNR/SSIM。
        // 留空 "" 表示不计算质量指标。通常与 ImageDir 指向同一个测试图目录。
        std::string GroundTruthDir   = "D:/source/python_projects/3DGS/data/deep_blending/playroom/test_images";

        // 渲染结果输出目录
        std::string OutputDir        = "test_output";

        float Scale         = 1.0f;    // 输出分辨率缩放 (1.0 = 与数据集一致)
        bool  ShowWindow    = true;   // 是否显示复用出来的窗口 (默认隐藏, 不往屏幕输出)
        bool  DumpImages    = true;    // 是否把渲染结果写成图片
        int   MaxImageCount = -1;      // 最多渲染张数 (<0 = 全部)

        bool Valid() const
        {
            // 至少要有高斯或网格其中之一, 否则没有可渲染内容
            const bool hasContent = !GaussianPly.empty() || !MeshPath.empty();
            return hasContent && !ColmapCamerasTxt.empty() && !ColmapImagesTxt.empty();
        }
    };

    // 测试模式驱动器: 职责相当于 "Application + EditorLayer", 但不带 ImGui、不建 ECS,
    // 只用几个成员变量持有高斯资产与相机列表, 按数据集位姿逐张渲染并输出图片 + 性能统计。
    //
    // 注意: VulkanContext 目前需要 surface + swapchain (按约定不改动既有 Window / VulkanContext),
    // 所以这里复用现有的 Window::Create() 建一个窗口 (默认隐藏), 真实渲染全部发生在离屏 FBO。
    class ApplicationSpecification final
    {
    public:
        ApplicationSpecification();
        ~ApplicationSpecification();

        void Run();

        inline float GetTimeStep() { return m_TimeStep; }
        inline float GetFixedTimeStep() { return m_FixedTimeStep; }

        inline static ApplicationSpecification& Get() { return *s_Instance; }

    private:
        bool Init();
        void Shutdown();

        bool LoadGaussian();
        bool LoadMesh();
        bool LoadCameras();
        void CreateOffscreenTarget(uint32_t width, uint32_t height);

        // 渲染单张 (只含渲染本身: 收集 → BeginScene → Mesh/Gaussian Pass → EndScene → 等 GPU)。
        // 返回该张耗时 (ms); GPU 各阶段耗时写入 outGpu (读的是时间戳流水线里的上一帧)。
        double RenderImage(const DatasetCamera& camera, GpuTimings& outGpu);

        // Read RGBA32F offscreen color and quantize to BGRA8 for BMP/metrics.
        bool ReadbackColor(std::vector<uint8_t>& outPixels);

        static bool WriteBMP(const std::string& path, const uint8_t* bgra, uint32_t width, uint32_t height);

        // ---- 质量指标 (PSNR / SSIM) ----
        // 单张指标: 输入两张 RGBA8 图 (尺寸必须一致), 返回 PSNR(dB) 与 SSIM。
        //   PSNR 在 RGB 三通道上整体算 MSE; SSIM 在亮度 (Rec.709) 上用 9x9 滑动窗口计算。
        struct ImageMetrics { double psnr = 0.0; double ssim = 0.0; };
        static ImageMetrics ComputeMetrics(const std::vector<uint8_t>& aRGBA,
                                           const std::vector<uint8_t>& bRGBA,
                                           uint32_t width, uint32_t height);

        // BGRA8 (渲染回读) → RGBA8 (与 stbi 读出的 GT 对齐通道顺序)
        static void BGRAtoRGBA(const std::vector<uint8_t>& bgra, std::vector<uint8_t>& rgba,
                               uint32_t width, uint32_t height);
        // RGBA8 最近邻缩放 (分辨率不一致时把 GT 缩到渲染尺寸)
        static void ResizeNearestRGBA(const std::vector<uint8_t>& src, uint32_t sw, uint32_t sh,
                                      std::vector<uint8_t>& dst, uint32_t dw, uint32_t dh);
        // 用 stb_image 加载真值图为 RGBA8 (PNG/JPG/BMP/TGA... 均可)
        static bool LoadImageRGBA(const std::string& path, std::vector<uint8_t>& out,
                                  uint32_t& width, uint32_t& height);

    private:
        static ApplicationSpecification* s_Instance;

        TestSpecification m_Spec;
        std::unique_ptr<Window>      m_Window;      // 复用现有 Window (VulkanContext 需要 surface/swapchain)
        std::shared_ptr<Framebuffer> m_OffscreenFB; // 离屏渲染目标 (尺寸 = 数据集分辨率)

        // 场景 (不用 ECS, 直接持有高斯资产 + 变换; mesh 同样可空)
        UUID                           m_GaussianID = UUID(0);
        std::shared_ptr<GaussianModel> m_GaussianModel;
        glm::mat4                      m_GaussianTransform = glm::mat4(1.0f);

        // 网格模型 (可空: 留空则不绘制)
        UUID                      m_MeshID = UUID(0);
        std::shared_ptr<Model>    m_MeshModel;
        glm::mat4                 m_MeshTransform = glm::mat4(1.0f);

        std::vector<DatasetCamera> m_Cameras;

        // 回读用 staging buffer
        VulkanBuffer m_ReadbackBuffer;
        uint32_t     m_ReadbackWidth  = 0;
        uint32_t     m_ReadbackHeight = 0;

        bool       m_Running = true;
        const float m_FixedTimeStep = 1.0f / 60.0f;
        float      m_TimeStep = 0.0f;
        float      m_LastFrameTime = 0.0f;
        float      m_Accumulator = 0.0f;//纯私有，不外用
    };
};
