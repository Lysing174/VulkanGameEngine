#include "pch.h"
#include "ApplicationSpecification.h"

#include "Engine/Renderer/Renderer.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanFrameBuffer.h"

#include <stb_image.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Engine
{
    ApplicationSpecification* ApplicationSpecification::s_Instance = nullptr;

    // ============================================================
    // 构造 / 析构
    // ============================================================
    ApplicationSpecification::ApplicationSpecification()
    {
        s_Instance = this;
    }

    ApplicationSpecification::~ApplicationSpecification()
    {
        Shutdown();
    }

    // ============================================================
    // Init / Shutdown
    // ============================================================
    bool ApplicationSpecification::Init()
    {
        EG_CORE_ASSERT(!s_Instance || s_Instance == this, "ApplicationSpecification already exists!");
        s_Instance = this;

        if (!m_Spec.Valid())
        {
            EG_CORE_ERROR("[Test] invalid test spec: GaussianPly / ColmapCamerasTxt / ColmapImagesTxt 不能为空 "
                          "(在 ApplicationSpecification.h 的 TestSpecification 里改)");
            return false;
        }

        // 1) 窗口: 复用现有 Window (VulkanContext 需要 surface + swapchain)。
        //    测试模式默认隐藏, 屏幕不输出; 真实渲染全部发生在离屏 FBO。
        WindowProps props("VulkanEngine [TestMode]", 1280, 720, false);
        m_Window = std::unique_ptr<Window>(Window::Create(props));
        if (!m_Spec.ShowWindow)
            glfwHideWindow(static_cast<GLFWwindow*>(m_Window->GetNativeWindow()));

        // 2) 渲染器初始化 (与 Application 一致; Renderer::Init 需要上面的 context 已经就绪)
        Renderer::Init();

        // 3) 资源 (高斯 / 网格均可为空, 为空则不加载、不绘制)
        if (!m_Spec.GaussianPly.empty() && !LoadGaussian()) return false;
        if (!m_Spec.MeshPath.empty()     && !LoadMesh())     return false;
        if (!LoadCameras())  return false;

        std::error_code ec;
        std::filesystem::create_directories(m_Spec.OutputDir, ec);

        return true;
    }

    void ApplicationSpecification::Shutdown()
    {
        if (!m_Window) return;   // 已关闭 (Run 的失败路径 / 析构时二次调用)

        auto* context = static_cast<VulkanContext*>(m_Window->GetContext());
        if (context)
            vkDeviceWaitIdle(context->GetDevice());

        Renderer::Shutdown();

        m_OffscreenFB.reset();
        m_Window.reset();

        if (s_Instance == this) s_Instance = nullptr;
    }

    // ============================================================
    // 资源加载
    // ============================================================
    bool ApplicationSpecification::LoadGaussian()
    {
        std::error_code ec;
        std::filesystem::path plyPath(m_Spec.GaussianPly);
        if (!std::filesystem::exists(plyPath, ec))
        {
            EG_CORE_ERROR("[Test] gaussian ply not found: '{0}'", m_Spec.GaussianPly);
            return false;
        }

        std::string dir  = plyPath.parent_path().string();
        std::string file = plyPath.filename().string();
        if (dir.empty()) dir = ".";

        auto* library = Renderer::GetGaussianModelLibrary();
        library->ScanDirectory(dir);            // 建立清单 + .meta UUID (已存在则复用)

        UUID id = library->FindIDByPath(file);
        if ((uint64_t)id == 0)
        {
            EG_CORE_ERROR("[Test] cannot resolve gaussian asset '{0}' in '{1}'", file, dir);
            return false;
        }

        m_GaussianID        = id;
        m_GaussianModel     = library->Load(id);
        m_GaussianTransform = m_Spec.GaussianTransform;

        if (!m_GaussianModel || m_GaussianModel->GetGaussianCount() == 0)
        {
            EG_CORE_ERROR("[Test] gaussian '{0}' loaded but empty", file);
            return false;
        }

        EG_CORE_INFO("[Test] gaussian '{0}': {1} splats", file, m_GaussianModel->GetGaussianCount());
        return true;
    }

    bool ApplicationSpecification::LoadMesh()
    {
        std::error_code ec;
        std::filesystem::path path(m_Spec.MeshPath);
        if (!std::filesystem::exists(path, ec))
        {
            EG_CORE_ERROR("[Test] mesh model not found: '{0}'", m_Spec.MeshPath);
            return false;
        }

        std::string dir  = path.parent_path().string();
        std::string file = path.filename().string();
        if (dir.empty()) dir = ".";

        auto* library = Renderer::GetModelLibrary();
        library->ScanDirectory(dir);            // 建立清单 + .meta UUID (已存在则复用)

        UUID id = library->FindIDByPath(file);
        if ((uint64_t)id == 0)
        {
            EG_CORE_ERROR("[Test] cannot resolve mesh asset '{0}' in '{1}'", file, dir);
            return false;
        }

        m_MeshID        = id;
        m_MeshModel     = library->Load(id);
        m_MeshTransform = m_Spec.MeshTransform;

        if (!m_MeshModel)
        {
            EG_CORE_ERROR("[Test] mesh '{0}' failed to load", file);
            return false;
        }

        EG_CORE_INFO("[Test] mesh '{0}': loaded", file);
        return true;
    }

    bool ApplicationSpecification::LoadCameras()
    {
        m_Cameras = DatasetCamera::LoadFromColmap(m_Spec.ColmapCamerasTxt, m_Spec.ColmapImagesTxt);
        if (m_Cameras.empty())
        {
            EG_CORE_ERROR("[Test] no camera loaded from '{0}' + '{1}'",
                          m_Spec.ColmapCamerasTxt, m_Spec.ColmapImagesTxt);
            return false;
        }

        // 测试集过滤: 只保留 ImageDir 里真实存在的图片
        if (!m_Spec.ImageDir.empty())
        {
            std::vector<DatasetCamera> filtered;
            filtered.reserve(m_Cameras.size());
            for (const auto& camera : m_Cameras)
            {
                std::error_code ec;
                if (std::filesystem::exists(std::filesystem::path(m_Spec.ImageDir) / camera.GetName(), ec))
                    filtered.push_back(camera);
            }
            if (filtered.empty())
            {
                EG_CORE_ERROR("[Test] no image of the pose file exists in '{0}'", m_Spec.ImageDir);
                return false;
            }
            m_Cameras = std::move(filtered);
        }

        if (m_Spec.Scale != 1.0f)
        {
            for (auto& camera : m_Cameras)
                camera.ApplyScale(m_Spec.Scale);
        }

        if (m_Spec.MaxImageCount >= 0 && (size_t)m_Spec.MaxImageCount < m_Cameras.size())
            m_Cameras.resize((size_t)m_Spec.MaxImageCount);

        return !m_Cameras.empty();
    }

    void ApplicationSpecification::CreateOffscreenTarget(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0) return;
        if (m_OffscreenFB && m_OffscreenFB->GetWidth() == width && m_OffscreenFB->GetHeight() == height)
            return;

        FramebufferSpecification fbSpec;
        fbSpec.Width      = width;
        fbSpec.Height     = height;
        fbSpec.Samples    = VulkanContext::Get()->GetMSAASamples();
        fbSpec.Readback   = true;   // 需要 CPU 回读颜色附件
        fbSpec.RenderPass = VulkanContext::Get()->GetRenderPass("Mesh")->GetVkRenderPass();

        // 重建 (而非 Resize) 以确保 Readback 标记生效
        m_OffscreenFB.reset();
        m_OffscreenFB = Framebuffer::Create(fbSpec);

        Renderer::SetOffscreenFramebuffer(m_OffscreenFB);
    }

    // ============================================================
    // 渲染单张
    // ============================================================
    double ApplicationSpecification::RenderImage(const DatasetCamera& camera, GpuTimings& outGpu)
    {
        const auto start = std::chrono::steady_clock::now();

        Renderer::BeginRenderQueueSubmit();
        if (m_GaussianModel)
            Renderer::SubmitGaussian(m_GaussianTransform, m_GaussianID, 0);
        if (m_MeshModel)
            Renderer::SubmitMesh(m_MeshTransform, m_MeshID, MeshRendererComponent(m_MeshID), 0);
        Renderer::EndRenderQueueSubmit();

        Renderer::BeginScene(camera);
        Renderer::RunShadowPass();   // 无光源时内部直接返回 (不产生耗时)
        Renderer::RunMeshPass();     // Mesh pass + 高斯 splat pass
        Renderer::EndScene();

        // 等本帧 GPU 真正跑完: 计时才包含 GPU 时间, 且 GPU 时间戳可读
        //vkDeviceWaitIdle(VulkanContext::Get()->GetDevice());

        const auto end = std::chrono::steady_clock::now();

        // EndScene / EndFrame 内读取的是时间戳流水线里的上一帧结果
        outGpu = Renderer::GetStatistics().GPU;

        m_TimeStep = (float)std::chrono::duration<double>(end - start).count();

        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    // ============================================================
    // Read floating-point color, then quantize once to the existing BGRA8 output.
    // ============================================================
    bool ApplicationSpecification::ReadbackColor(std::vector<uint8_t>& outPixels)
    {
        auto* context = VulkanContext::Get();
        auto  framebuffer = std::static_pointer_cast<VulkanFramebuffer>(Renderer::GetOffscreenFramebuffer());
        if (!context || !framebuffer) return false;

        VkImage image = framebuffer->GetResolveImage();
        const uint32_t width  = framebuffer->GetWidth();
        const uint32_t height = framebuffer->GetHeight();
        if (image == VK_NULL_HANDLE || width == 0 || height == 0) return false;

        const VkFormat format = framebuffer->GetColorFormat();
        const bool floatColor = format == VK_FORMAT_R32G32B32A32_SFLOAT;
        if (!floatColor && format != VK_FORMAT_B8G8R8A8_UNORM)
        {
            EG_CORE_ERROR("Unsupported offscreen readback format: {0}", static_cast<int>(format));
            return false;
        }
        const size_t pixelCount = static_cast<size_t>(width) * height;
        const VkDeviceSize size = static_cast<VkDeviceSize>(pixelCount) * 4 * (floatColor ? sizeof(float) : 1);

        if (!m_ReadbackBuffer.IsValid() || m_ReadbackBuffer.GetSize() < size ||
            m_ReadbackWidth != width || m_ReadbackHeight != height)
        {
            m_ReadbackBuffer.Destroy();
            m_ReadbackBuffer = VulkanBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            m_ReadbackWidth  = width;
            m_ReadbackHeight = height;
        }
        if (!m_ReadbackBuffer.IsValid()) return false;

        VkCommandBuffer cmd = context->BeginSingleTimeCommands();

        // 高斯 pass 结束后颜色附件处于 SHADER_READ_ONLY → 转 TRANSFER_SRC
        {
            VkImageMemoryBarrier barrier = {};
            barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            barrier.image                           = image;
            barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel   = 0;
            barrier.subresourceRange.levelCount     = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount     = 1;
            barrier.srcAccessMask                   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        VkBufferImageCopy region = {};
        region.bufferOffset      = 0;
        region.bufferRowLength   = 0;   // 紧密排列
        region.bufferImageHeight = 0;
        region.imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageOffset       = { 0, 0, 0 };
        region.imageExtent       = { width, height, 1 };
        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               m_ReadbackBuffer.GetBuffer(), 1, &region);

        // 转回 SHADER_READ_ONLY (下一帧 BeginMeshRenderPass 从这里过渡回 COLOR_ATTACHMENT)
        {
            VkImageMemoryBarrier barrier = {};
            barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            barrier.image                           = image;
            barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel   = 0;
            barrier.subresourceRange.levelCount     = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount     = 1;
            barrier.srcAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        context->EndSingleTimeCommands(cmd);   // 提交并等待

        void* data = nullptr;
        if (vkMapMemory(context->GetDevice(), m_ReadbackBuffer.GetMemory(), 0, size, 0, &data) != VK_SUCCESS)
            return false;

        outPixels.resize(pixelCount * 4);
        if (floatColor)
        {
            const float* rgba = static_cast<const float*>(data);
            auto quantize = [](float value) -> uint8_t
            {
                if (!(value > 0.0f)) return 0; // Also handles NaN.
                if (value >= 1.0f) return 255;
                return static_cast<uint8_t>(value * 255.0f + 0.5f);
            };
            for (size_t pixel = 0; pixel < pixelCount; ++pixel)
            {
                outPixels[pixel * 4 + 0] = quantize(rgba[pixel * 4 + 2]);
                outPixels[pixel * 4 + 1] = quantize(rgba[pixel * 4 + 1]);
                outPixels[pixel * 4 + 2] = quantize(rgba[pixel * 4 + 0]);
                outPixels[pixel * 4 + 3] = quantize(rgba[pixel * 4 + 3]);
            }
        }
        else
        {
            std::memcpy(outPixels.data(), data, outPixels.size());
        }
        vkUnmapMemory(context->GetDevice(), m_ReadbackBuffer.GetMemory());

        return true;
    }

    // ============================================================
    // BMP 写出 (24bit 自下而上; 输入 BGRA 的前 3 字节正好是 BMP 的 BGR 顺序)
    // ============================================================
    bool ApplicationSpecification::WriteBMP(const std::string& path, const uint8_t* bgra,
                                            uint32_t width, uint32_t height)
    {
        if (!bgra || width == 0 || height == 0) return false;

        const uint32_t rowSize    = width * 3;
        const uint32_t rowPadding = (4 - (rowSize % 4)) % 4;
        const uint32_t imageSize  = (rowSize + rowPadding) * height;
        const uint32_t fileSize   = 54 + imageSize;

        std::ofstream out(path, std::ios::binary);
        if (!out) return false;

        auto writeU32 = [&](uint32_t value)
        {
            out.write(reinterpret_cast<const char*>(&value), sizeof(uint32_t));
        };
        auto writeU16 = [&](uint16_t value)
        {
            out.write(reinterpret_cast<const char*>(&value), sizeof(uint16_t));
        };

        out.put('B'); out.put('M');
        writeU32(fileSize);
        writeU32(0);            // reserved
        writeU32(54);           // 像素数据偏移
        writeU32(40);           // DIB header size
        writeU32(width);
        writeU32(height);
        writeU16(1);            // planes
        writeU16(24);           // bpp
        writeU32(0);            // BI_RGB
        writeU32(imageSize);
        writeU32(2835);         // 72 dpi
        writeU32(2835);
        writeU32(0);            // palette
        writeU32(0);            // important colors

        const char padding[4] = { 0, 0, 0, 0 };
        for (uint32_t y = height; y-- > 0;)
        {
            const uint8_t* row = bgra + (size_t)y * width * 4;
            for (uint32_t x = 0; x < width; ++x)
            {
                const uint8_t* px = row + x * 4;
                out.put((char)px[0]);   // B
                out.put((char)px[1]);   // G
                out.put((char)px[2]);   // R
            }
            if (rowPadding) out.write(padding, rowPadding);
        }

        return true;
    }

    // ============================================================
    // 质量指标 (PSNR / SSIM)
    // ============================================================
    bool ApplicationSpecification::LoadImageRGBA(const std::string& path,
                                                std::vector<uint8_t>& out,
                                                uint32_t& width, uint32_t& height)
    {
        int iw = 0, ih = 0, ch = 0;
        stbi_uc* data = stbi_load(path.c_str(), &iw, &ih, &ch, STBI_rgb_alpha);
        if (!data)
        {
            EG_CORE_WARN("ApplicationSpecification: cannot load GT image '{0}' ({1})",
                         path, stbi_failure_reason());
            return false;
        }
        out.assign(data, data + (size_t)iw * ih * 4);
        width  = (uint32_t)iw;
        height = (uint32_t)ih;
        stbi_image_free(data);
        return true;
    }

    void ApplicationSpecification::BGRAtoRGBA(const std::vector<uint8_t>& bgra, std::vector<uint8_t>& rgba,
                                             uint32_t width, uint32_t height)
    {
        const size_t n = (size_t)width * height;
        rgba.resize(n * 4);
        for (size_t p = 0; p < n; ++p)
        {
            rgba[p * 4 + 0] = bgra[p * 4 + 2];   // R
            rgba[p * 4 + 1] = bgra[p * 4 + 1];   // G
            rgba[p * 4 + 2] = bgra[p * 4 + 0];   // B
            rgba[p * 4 + 3] = bgra[p * 4 + 3];   // A
        }
    }

    void ApplicationSpecification::ResizeNearestRGBA(const std::vector<uint8_t>& src, uint32_t sw, uint32_t sh,
                                                    std::vector<uint8_t>& dst, uint32_t dw, uint32_t dh)
    {
        if (sw == dw && sh == dh) { dst = src; return; }
        dst.resize((size_t)dw * dh * 4);
        for (uint32_t y = 0; y < dh; ++y)
        {
            const uint32_t sy = (uint32_t)((double)y * sh / dh);
            for (uint32_t x = 0; x < dw; ++x)
            {
                const uint32_t sx = (uint32_t)((double)x * sw / dw);
                const uint8_t* s = &src[((size_t)sy * sw + sx) * 4];
                uint8_t*       d = &dst[((size_t)y * dw + x) * 4];
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
            }
        }
    }

    ApplicationSpecification::ImageMetrics ApplicationSpecification::ComputeMetrics(
        const std::vector<uint8_t>& aRGBA, const std::vector<uint8_t>& bRGBA,
        uint32_t width, uint32_t height)
    {
        ImageMetrics m;
        const size_t n = (size_t)width * height;
        if (n == 0 || aRGBA.size() < n * 4 || bRGBA.size() < n * 4) return m;

        // ---- PSNR (在 RGB 三通道上整体算 MSE) ----
        double sumSqErr = 0.0;
        for (size_t p = 0; p < n; ++p)
        {
            for (int c = 0; c < 3; ++c)
            {
                const double e = (double)aRGBA[p * 4 + c] - (double)bRGBA[p * 4 + c];
                sumSqErr += e * e;
            }
        }
        const double mse = sumSqErr / (double)(n * 3);
        if (mse > 0.0)
            m.psnr = 10.0 * std::log10((255.0 * 255.0) / mse);
        else
            m.psnr = 99.0;   // 完全一致

        // ---- SSIM (亮度, 9x9 滑动窗口, 积分图加速) ----
        const int W = (int)width, H = (int)height;
        const int r = 4;   // 窗口半径 → 9x9
        std::vector<double> La(n), Lb(n);
        for (size_t p = 0; p < n; ++p)
        {
            La[p] = 0.2126 * aRGBA[p * 4] + 0.7152 * aRGBA[p * 4 + 1] + 0.0722 * aRGBA[p * 4 + 2];
            Lb[p] = 0.2126 * bRGBA[p * 4] + 0.7152 * bRGBA[p * 4 + 1] + 0.0722 * bRGBA[p * 4 + 2];
        }

        auto buildIntegral = [](const std::vector<double>& v, int W, int H)
        {
            std::vector<double> I((size_t)(W + 1) * (H + 1), 0.0);
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                {
                    const double val = v[(size_t)y * W + x];
                    I[(size_t)(y + 1) * (W + 1) + (x + 1)] = val
                        + I[(size_t)(y + 1) * (W + 1) + x]
                        + I[(size_t)y * (W + 1) + (x + 1)]
                        - I[(size_t)y * (W + 1) + x];
                }
            return I;
        };

        auto ILa  = buildIntegral(La, W, H);
        auto ILb  = buildIntegral(Lb, W, H);
        // 对平方 / 乘积单独建积分图
        auto sqIntegral = [](const std::vector<double>& v, int W, int H)
        {
            std::vector<double> I((size_t)(W + 1) * (H + 1), 0.0);
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                {
                    const double val = v[(size_t)y * W + x] * v[(size_t)y * W + x];
                    I[(size_t)(y + 1) * (W + 1) + (x + 1)] = val
                        + I[(size_t)(y + 1) * (W + 1) + x]
                        + I[(size_t)y * (W + 1) + (x + 1)]
                        - I[(size_t)y * (W + 1) + x];
                }
            return I;
        };
        auto crossIntegral = [](const std::vector<double>& v1, const std::vector<double>& v2, int W, int H)
        {
            std::vector<double> I((size_t)(W + 1) * (H + 1), 0.0);
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                {
                    const double val = v1[(size_t)y * W + x] * v2[(size_t)y * W + x];
                    I[(size_t)(y + 1) * (W + 1) + (x + 1)] = val
                        + I[(size_t)(y + 1) * (W + 1) + x]
                        + I[(size_t)y * (W + 1) + (x + 1)]
                        - I[(size_t)y * (W + 1) + x];
                }
            return I;
        };

        // 重建正确的平方/乘积积分图
        auto ILa2 = sqIntegral(La, W, H);
        auto ILb2 = sqIntegral(Lb, W, H);
        auto ILaLb = crossIntegral(La, Lb, W, H);

        auto windowSum = [&](const std::vector<double>& I, int x, int y)
        {
            const int x0 = std::max(0, x - r), x1 = std::min(W - 1, x + r);
            const int y0 = std::max(0, y - r), y1 = std::min(H - 1, y + r);
            return I[(size_t)(y1 + 1) * (W + 1) + (x1 + 1)]
                 - I[(size_t)(y1 + 1) * (W + 1) + x0]
                 - I[(size_t)y0 * (W + 1) + (x1 + 1)]
                 + I[(size_t)y0 * (W + 1) + x0];
        };

        const double C1 = (0.01 * 255.0) * (0.01 * 255.0);
        const double C2 = (0.03 * 255.0) * (0.03 * 255.0);
        double ssimSum = 0.0;
        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                const double area = (double)((std::min(W - 1, x + r) - std::max(0, x - r) + 1) *
                                             (std::min(H - 1, y + r) - std::max(0, y - r) + 1));
                const double ma  = windowSum(ILa,  x, y) / area;
                const double mb  = windowSum(ILb,  x, y) / area;
                const double va  = windowSum(ILa2, x, y) / area - ma * ma;
                const double vb  = windowSum(ILb2, x, y) / area - mb * mb;
                const double vab = windowSum(ILaLb,x, y) / area - ma * mb;
                const double ssim = ((2.0 * ma * mb + C1) * (2.0 * vab + C2)) /
                                    ((ma * ma + mb * mb + C1) * (va + vb + C2));
                ssimSum += ssim;
            }
        }
        m.ssim = ssimSum / (double)(W * H);
        return m;
    }

    void ApplicationSpecification::Run()
    {
        if (!Init())
        {
            Shutdown();
            return;
        }

        const size_t   count  = m_Cameras.size();
        const uint32_t width  = m_Cameras[0].GetIntrinsics().Width;
        const uint32_t height = m_Cameras[0].GetIntrinsics().Height;

        EG_CORE_INFO("[Test] ===== test mode =====");
        EG_CORE_INFO("[Test] gaussians  : {0}", m_GaussianModel ? m_GaussianModel->GetGaussianCount() : 0);
        EG_CORE_INFO("[Test] mesh       : {0}",
                     m_MeshModel ? (m_MeshModel->GetMesh() ? m_MeshModel->GetMesh()->GetSubmeshes().size() : 0) : 0);
        EG_CORE_INFO("[Test] images     : {0}", count);
        EG_CORE_INFO("[Test] resolution : {0}x{1}", width, height);
        EG_CORE_INFO("[Test] output dir : {0}", m_Spec.OutputDir);

        // 预热: 完成高斯 SSBO 上传 / 排序资源建立 / GPU 时间戳流水线填充, 均不计入统计
        {
            GpuTimings warmup{};
            const DatasetCamera& camera = m_Cameras[0];
            const uint32_t w = camera.GetIntrinsics().Width;
            const uint32_t h = camera.GetIntrinsics().Height;
            CreateOffscreenTarget(w, h);
            for (int i = 0; i < 2; ++i)
                RenderImage(m_Cameras[0], warmup);
        }

        double totalRenderMs = 0.0;
        double totalIOMs     = 0.0;
        double totalPsnr     = 0.0;
        double totalSsim     = 0.0;
        uint32_t metricFrames = 0;
        std::array<double, RenderStageCount> gpuAccum{};
        uint32_t gpuFrames = 0;

        for (size_t i = 0; i < count && m_Running; ++i)
        {
            const DatasetCamera& camera = m_Cameras[i];
            const uint32_t w = camera.GetIntrinsics().Width;
            const uint32_t h = camera.GetIntrinsics().Height;
            CreateOffscreenTarget(w, h);

            GpuTimings gpu{};
            const double renderMs = RenderImage(camera, gpu);
            totalRenderMs += renderMs;

            if (gpu.Supported && gpu.Valid)
            {
                for (size_t s = 0; s < RenderStageCount; ++s)
                    gpuAccum[s] += gpu.Milliseconds[s];
                ++gpuFrames;
            }

            // ---- 回读 (不计入渲染耗时 / FPS) ----
            const bool needPixels = m_Spec.DumpImages || !m_Spec.GroundTruthDir.empty();
            std::vector<uint8_t> pixels;
            if (needPixels && !ReadbackColor(pixels))
                pixels.clear();

            // ---- 写盘 ----
            double ioMs = 0.0;
            if (m_Spec.DumpImages && !pixels.empty())
            {
                const auto ioStart = std::chrono::steady_clock::now();

                std::ostringstream nameStream;
                nameStream << std::setw(4) << std::setfill('0') << (i + 1) << "_"
                           << std::filesystem::path(camera.GetName()).stem().string() << ".bmp";
                const std::string path =
                    (std::filesystem::path(m_Spec.OutputDir) / nameStream.str()).string();
                WriteBMP(path, pixels.data(), w, h);

                ioMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - ioStart).count();
                totalIOMs += ioMs;
            }

            // ---- 质量指标 (PSNR / SSIM) ----
            if (!m_Spec.GroundTruthDir.empty() && !pixels.empty())
            {
                const std::string gtPath =
                    (std::filesystem::path(m_Spec.GroundTruthDir) / camera.GetName()).string();
                std::vector<uint8_t> gt; uint32_t gw = 0, gh = 0;
                if (LoadImageRGBA(gtPath, gt, gw, gh))
                {
                    std::vector<uint8_t> ren; BGRAtoRGBA(pixels, ren, w, h);
                    std::vector<uint8_t> gtR; ResizeNearestRGBA(gt, gw, gh, gtR, w, h);
                    const ImageMetrics im = ComputeMetrics(ren, gtR, w, h);
                    totalPsnr    += im.psnr;
                    totalSsim    += im.ssim;
                    ++metricFrames;
                    EG_CORE_INFO("[Test] [{0}/{1}] {2}  PSNR {3:.2f} dB  SSIM {4:.4f}",
                                 i + 1, count, camera.GetName(), im.psnr, im.ssim);
                }
                else
                {
                    EG_CORE_WARN("[Test] [{0}/{1}] GT not found: {2}", i + 1, count, gtPath);
                }
            }

            m_Accumulator += (float)(renderMs * 0.001);
            m_LastFrameTime = m_Accumulator;

            EG_CORE_INFO("[Test] [{0}/{1}] {2}  {3}x{4}  GPU render (isValid:{5}) {6:.2f} ms \n(mesh: {7:.2f} ms, gaussian sort: {8:.2f} ms, "
                         "gaussian render: {9:.2f} ms) CPU time: {10:.2f} ms, dump {11:.2f} ms",
                         i + 1, count, camera.GetName(), w, h,gpu.Valid&&gpu.Supported, gpu.Milliseconds[0]+gpu.Milliseconds[1]+gpu.Milliseconds[2]+gpu.Milliseconds[3],
                         gpu.Milliseconds[1],gpu.Milliseconds[2],gpu.Milliseconds[3],renderMs, ioMs);
        }

        // ---- 汇总 ----
        const double avgRenderMs = count > 0 ? totalRenderMs / (double)count : 0.0;
        const double avgFps      = avgRenderMs > 0.0 ? 1000.0 / avgRenderMs : 0.0;
        const double avgIOMs     = count > 0 ? totalIOMs / (double)count : 0.0;

        static const char* stageNames[RenderStageCount] = { "shadow", "mesh", "gaussianSort", "gaussian" };
        std::ostringstream gpuLine;
        double gpuTotal = 0.0;
        for (size_t s = 0; s < RenderStageCount; ++s)
        {
            const double avg = gpuFrames > 0 ? gpuAccum[s] / (double)gpuFrames : 0.0;
            gpuTotal += avg;
            gpuLine << stageNames[s] << " " << std::fixed << std::setprecision(2) << avg << " ms";
            if (s + 1 < RenderStageCount) gpuLine << " | ";
        }

        EG_CORE_INFO("[Test] ===== summary =====");
        EG_CORE_INFO("[Test] images rendered : {0}", count);
        EG_CORE_INFO("[Test] avg render time : {0:.2f} ms", avgRenderMs);
        EG_CORE_INFO("[Test] avg FPS         : {0:.2f}  (仅每张图片渲染耗时, 不含加载/回读/写盘)", avgFps);
        EG_CORE_INFO("[Test] avg dump time   : {0:.2f} ms (回读 + 写盘, 未计入 FPS)", avgIOMs);
        if (metricFrames > 0)
        {
            EG_CORE_INFO("[Test] avg PSNR        : {0:.2f} dB ({1} image(s))",
                         totalPsnr / (double)metricFrames, metricFrames);
            EG_CORE_INFO("[Test] avg SSIM        : {0:.4f}",
                         totalSsim / (double)metricFrames);
        }
        else
            EG_CORE_INFO("[Test] avg PSNR/SSIM   : skipped (GroundTruthDir empty)");
        if (gpuFrames > 0)
            EG_CORE_INFO("[Test] avg GPU stages  : {0}", gpuLine.str());
        else
            EG_CORE_INFO("[Test] avg GPU stages  : unavailable (timestamp query not supported)");
        EG_CORE_INFO("[Test] avg GPU total   : {0:.2f} ms", gpuTotal);

        Shutdown();
    }
};
