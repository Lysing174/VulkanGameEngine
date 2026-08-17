#include "pch.h"
#include "EnvironmentImage.h"
#include "MipmapGenerator.h"

//#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "Platform/Vulkan/VulkanCubeTexture.h"
#include "Platform/Vulkan/VulkanImage.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanBuffer.h"

#include <glm/gtc/constants.hpp>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <filesystem>
#include <limits>

namespace Engine {

// ===================================================================
// Constants
// ===================================================================
static constexpr float PI = glm::pi<float>();
static constexpr float INV_PI = 1.0f / PI;

// ===================================================================
// Constructor / Destructor
// ===================================================================
EnvironmentImage::EnvironmentImage(const std::string& hdrPath)
	: m_HdrPath(hdrPath)
	, m_MipmapGenerator(std::make_unique<MipmapGenerator>())
{
}

EnvironmentImage::~EnvironmentImage() = default;

// ===================================================================
// Initialize
// ===================================================================
void EnvironmentImage::Initialize()
{
	if (m_Loaded) return;

	EG_CORE_INFO("EnvironmentImage: loading {0}...", m_HdrPath);

	// 1. Load HDR from disk
	LoadHDR();

	// 2. Convert equirectangular to cubemap base faces (CPU)
	EquirectToCubemap();

	// 3. Upload base level to GPU, then generate GGX pre-filtered mip-chain
	UploadCubemapToGPU();

	// 4. Compute spherical harmonics from CPU cubemap base level
	ComputeSH();

	m_Loaded = true;
	EG_CORE_INFO("EnvironmentImage: initialized ({0}x{0} cubemap, {1} mips, GGX prefilter)",
		m_FaceSize, m_MipLevels);
}

// ===================================================================
// 1. LoadHDR — read equirectangular .hdr via stb_image
// ===================================================================
void EnvironmentImage::LoadHDR()
{
	int w, h, ch;
	float* raw = stbi_loadf(m_HdrPath.c_str(), &w, &h, &ch, 4); // force RGBA
	if (!raw)
	{
		EG_CORE_ERROR("EnvironmentImage: failed to load HDR: {0}", m_HdrPath);
		throw std::runtime_error("Failed to load HDR: " + m_HdrPath);
	}

	m_HdrWidth  = w;
	m_HdrHeight = h;
	size_t pixelCount = static_cast<size_t>(w) * h * 4;
	m_HdrPixels.assign(raw, raw + pixelCount);
	stbi_image_free(raw);

	EG_CORE_INFO("EnvironmentImage: HDR loaded ({0}x{1})", w, h);
}

// ===================================================================
// DirToUV — cubemap face index → local UV  (face 0..5)
// ===================================================================
/* static */ glm::vec2 EnvironmentImage::DirToUV(int face, const glm::vec3& dir)
{
	float u, v;
	float absX = std::abs(dir.x);
	float absY = std::abs(dir.y);
	float absZ = std::abs(dir.z);

	switch (face)
	{
	case 0: // +X  (right)
		u = -dir.z / absX;
		v = -dir.y / absX;
		break;
	case 1: // -X  (left)
		u =  dir.z / absX;
		v = -dir.y / absX;
		break;
	case 2: // +Y  (top)
		u =  dir.x / absY;
		v =  dir.z / absY;
		break;
	case 3: // -Y  (bottom)
		u =  dir.x / absY;
		v = -dir.z / absY;
		break;
	case 4: // +Z  (front)
		u =  dir.x / absZ;
		v = -dir.y / absZ;
		break;
	case 5: // -Z  (back)
	default:
		u = -dir.x / absZ;
		v = -dir.y / absZ;
		break;
	}

	// Map from [-1,1] → [0,1]
	return { u * 0.5f + 0.5f, v * 0.5f + 0.5f };
}

// ===================================================================
// SampleEquirect — bilinear sampling on HDR equirectangular map
// ===================================================================
/* static */ glm::vec3 EnvironmentImage::SampleEquirect(
	const float* data, int w, int h, glm::vec2 uv)
{
	// Clamp to avoid edge bleeding
	uv.x = glm::clamp(uv.x, 0.0f, 1.0f);
	uv.y = glm::clamp(uv.y, 0.0f, 1.0f);

	float fx = uv.x * w - 0.5f;
	float fy = uv.y * h - 0.5f;

	int x0 = static_cast<int>(std::floor(fx));
	int y0 = static_cast<int>(std::floor(fy));
	int x1 = x0 + 1;
	int y1 = y0 + 1;

	// Wrap (equirectangular wraps horizontally)
	x0 = (x0 % w + w) % w;
	x1 = (x1 % w + w) % w;
	y0 = glm::clamp(y0, 0, h - 1);
	y1 = glm::clamp(y1, 0, h - 1);

	float tx = fx - std::floor(fx);
	float ty = fy - std::floor(fy);

	auto sample = [&](int ix, int iy) -> glm::vec3 {
		const float* p = data + (iy * w + ix) * 4;
		return { p[0], p[1], p[2] };
	};

	glm::vec3 s00 = sample(x0, y0);
	glm::vec3 s10 = sample(x1, y0);
	glm::vec3 s01 = sample(x0, y1);
	glm::vec3 s11 = sample(x1, y1);

	return glm::mix(glm::mix(s00, s10, tx), glm::mix(s01, s11, tx), ty);
}

// ===================================================================
// 2. EquirectToCubemap — resample HDR to 6 cubemap faces (base only)
// ===================================================================
void EnvironmentImage::EquirectToCubemap()
{
	int s = m_FaceSize;
	m_MipLevels = static_cast<int>(std::floor(std::log2(s))) + 1;

	// Only allocate base face data — mip-chain generated on GPU
	size_t basePixels = static_cast<size_t>(s) * s;
	m_FaceBaseData.resize(basePixels * 6 * 4, 0.0f);

	const float* src = m_HdrPixels.data();
	int sw = m_HdrWidth, sh = m_HdrHeight;

	for (int face = 0; face < 6; ++face)
	{
		// Base face output pointer (row-major RGBA)
		float* dst = m_FaceBaseData.data() + static_cast<size_t>(face) * basePixels * 4;

		for (int y = 0; y < s; ++y)
		{
			for (int x = 0; x < s; ++x)
			{
				// Normalized device coordinates on face [-1, 1]
				float u = (x + 0.5f) / s * 2.0f - 1.0f;
				float v = (y + 0.5f) / s * 2.0f - 1.0f;

				glm::vec3 dir;
				switch (face)
				{
				case 0: dir = {  1.0f, -v, -u }; break; // +X
				case 1: dir = { -1.0f, -v,  u }; break; // -X
				case 2: dir = {  u,    1.0f, v }; break; // +Y
				case 3: dir = {  u,   -1.0f, -v }; break; // -Y
				case 4: dir = {  u,   -v,   1.0f }; break; // +Z
				case 5: dir = { -u,   -v,  -1.0f }; break; // -Z
				}

				dir = glm::normalize(dir);

				// Equirectangular UV from direction
				float theta = std::atan2(dir.x, dir.z);  // [-π, π]
				float phi   = std::asin(dir.y);          // [-π/2, π/2]
				float eqU   = theta / (2.0f * PI) + 0.5f;
				float eqV   = 0.5f - phi / PI;//phi   / PI + 0.5f;

				glm::vec3 color = SampleEquirect(src, sw, sh, { eqU, eqV });

				size_t idx = (static_cast<size_t>(y) * s + x) * 4;
				dst[idx + 0] = color.r;
				dst[idx + 1] = color.g;
				dst[idx + 2] = color.b;
				dst[idx + 3] = 1.0f; // alpha
			}
		}
	}
	EG_CORE_INFO("EnvironmentImage: cubemap faces generated (base {0}x{0})", s);
}

// ===================================================================
// 3. UploadCubemapToGPU + GGX Prefilter via MipmapGenerator
// ===================================================================
void EnvironmentImage::UploadCubemapToGPU()
{
	auto* ctx = VulkanContext::Get();
	auto device = ctx->GetDevice();

	// Create cubemap texture with STORAGE_BIT for compute shader writes.
	// Initial layout is GENERAL (set by VulkanCubeTexture constructor).
	m_CubeMap = std::make_shared<VulkanCubeTexture>(
		static_cast<uint32_t>(m_FaceSize),
		static_cast<uint32_t>(m_FaceSize),
		static_cast<uint32_t>(m_MipLevels),
		VK_FORMAT_R16G16B16A16_SFLOAT,
		VK_IMAGE_USAGE_STORAGE_BIT // required by MipmapGenerator
	);

	// Transition GENERAL → TRANSFER_DST_OPTIMAL for upload
	m_CubeMap->GetImage().TransitionLayout(
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
	);

	// ------ Upload base level only (mip 0, all 6 faces) ------

	size_t basePixels = static_cast<size_t>(m_FaceSize) * m_FaceSize;
	VkDeviceSize layerSize = basePixels * 8; // float16 = 2 bytes × 4 ch
	VkDeviceSize totalSize = layerSize * 6;  // 6 faces

	std::vector<VkBufferImageCopy> copyRegions;
	for (int face = 0; face < 6; ++face)
	{
		VkBufferImageCopy region{};
		region.bufferOffset      = layerSize * face;
		region.bufferRowLength   = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel       = 0;
		region.imageSubresource.baseArrayLayer = static_cast<uint32_t>(face);
		region.imageSubresource.layerCount     = 1;
		region.imageOffset = { 0, 0, 0 };
		region.imageExtent = {
			static_cast<uint32_t>(m_FaceSize),
			static_cast<uint32_t>(m_FaceSize),
			1
		};
		copyRegions.push_back(region);
	}

	// Create staging buffer
	VulkanBuffer staging(totalSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VkDeviceMemory stagingMemory = staging.GetMemory();

	// Pack float32 base face data → float16 in staging buffer
	void* mapped;
	vkMapMemory(device, stagingMemory, 0, totalSize, 0, &mapped);
	uint8_t* dst = static_cast<uint8_t*>(mapped);

	auto f32tof16 = [](float f) -> uint16_t {
		uint32_t x = *reinterpret_cast<uint32_t*>(&f);
		uint32_t sign = (x >> 16) & 0x8000;
		int32_t exp  = static_cast<int32_t>((x >> 23) & 0xFF) - 127 + 15;
		uint32_t mant = (x >> 13) & 0x3FF;
		if (exp <= 0)   return static_cast<uint16_t>(sign);
		if (exp >= 31)  return static_cast<uint16_t>(sign | 0x7C00 | ((exp > 31) ? 0 : (mant & 0x3FF)));
		return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | mant);
	};

	for (int face = 0; face < 6; ++face)
	{
		const float* src = m_FaceBaseData.data()
			+ static_cast<size_t>(face) * basePixels * 4;

		for (size_t i = 0; i < basePixels; ++i)
		{
			uint16_t hR = f32tof16(src[i * 4 + 0]);
			uint16_t hG = f32tof16(src[i * 4 + 1]);
			uint16_t hB = f32tof16(src[i * 4 + 2]);
			uint16_t hA = f32tof16(src[i * 4 + 3]);

			memcpy(dst, &hR, 2); dst += 2;
			memcpy(dst, &hG, 2); dst += 2;
			memcpy(dst, &hB, 2); dst += 2;
			memcpy(dst, &hA, 2); dst += 2;
		}
	}
	vkUnmapMemory(device, stagingMemory);

	// Upload base level
	VkCommandBuffer cmd = ctx->BeginSingleTimeCommands();
	vkCmdCopyBufferToImage(cmd, staging.GetBuffer(),
		m_CubeMap->GetImage().GetImage(),
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		static_cast<uint32_t>(copyRegions.size()), copyRegions.data());
	ctx->EndSingleTimeCommands(cmd);

	// staging buffer 在作用域结束时自动销毁

	EG_CORE_INFO("EnvironmentImage: base level uploaded to GPU");

	// Transition back to GENERAL for compute shader access
	m_CubeMap->GetImage().TransitionLayout(
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_GENERAL
	);

	// ------ Generate GGX pre-filtered mip chain via MipmapGenerator ------
	m_MipmapGenerator->Generate(m_CubeMap.get());

	// ------ Transition to SHADER_READ_ONLY_OPTIMAL for sampling ------
	m_CubeMap->GetImage().TransitionLayout(
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);

	// Update descriptor layout
	m_CubeMap->SetDescriptorLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	EG_CORE_INFO("EnvironmentImage: cubemap ready for rendering");
}

// float16 -> float32 decode
static float f16tof32(uint16_t h)
{
	uint32_t sign = (h >> 15) & 1;
	int32_t  exp  = (h >> 10) & 0x1F;
	uint32_t mant = h & 0x3FF;

	if (exp == 0)
	{
		if (mant == 0) return sign ? -0.0f : 0.0f;
		float f = std::ldexp(static_cast<float>(mant) / 1024.0f, -14);
		return sign ? -f : f;
	}
	if (exp == 31)
	{
		return mant ? std::numeric_limits<float>::quiet_NaN()
		            : (sign ? -std::numeric_limits<float>::infinity()
		                    :  std::numeric_limits<float>::infinity());
	}
	float f = 1.0f + static_cast<float>(mant) / 1024.0f;
	f = std::ldexp(f, exp - 15);
	return sign ? -f : f;
}

// ===================================================================
// 5. ComputeSH — spherical harmonics from base cubemap faces (CPU)
// ===================================================================
void EnvironmentImage::ComputeSH()
{
	// Reset
	m_SH = {};

	int s = m_FaceSize;
	size_t basePixels = static_cast<size_t>(s) * s;

	// Real SH basis functions for band l=0,1,2 (normalized)
	const float A0  = 0.5f * std::sqrt(1.0f / PI);                     // Y00
	const float A1  = 0.5f * std::sqrt(3.0f / PI);                     // Y1,-1 / Y1,0 / Y1,1
	const float A2  = 0.5f * std::sqrt(15.0f / PI);                    // Y2,-2 / Y2,-1 / Y2,1 / Y2,2
	const float A20 = 0.25f * std::sqrt(5.0f / PI);                    // Y2,0

	// Solid angle per pixel (equal-area approximation)
	float solidAngle = (4.0f * PI) / (6.0f * s * s);

	for (int face = 0; face < 6; ++face)
	{
		const float* faceData = m_FaceBaseData.data()
			+ static_cast<size_t>(face) * basePixels * 4;

		for (int y = 0; y < s; ++y)
		{
			for (int x = 0; x < s; ++x)
			{
				float u = (x + 0.5f) / s * 2.0f - 1.0f;
				float v = (y + 0.5f) / s * 2.0f - 1.0f;

				glm::vec3 dir;
				switch (face)
				{
				case 0: dir = {  1.0f, -v, -u }; break;
				case 1: dir = { -1.0f, -v,  u }; break;
				case 2: dir = {  u,    1.0f, v }; break;
				case 3: dir = {  u,   -1.0f, -v }; break;
				case 4: dir = {  u,   -v,   1.0f }; break;
				case 5: dir = { -u,   -v,  -1.0f }; break;
				}

				dir = glm::normalize(dir);
				float sx = dir.x, sy = dir.y, sz = dir.z;

				size_t px = (static_cast<size_t>(y) * s + x) * 4;
				glm::vec3 color(faceData[px], faceData[px+1], faceData[px+2]);
				glm::vec3 wc = color * solidAngle;

				// --- Band 0 (1 coefficient) ---
				m_SH.L00 += wc * A0;

				// --- Band 1 (3 coefficients) ---
				m_SH.L1m1 += wc * (A1 * sy);              // Y1,-1 ∝ y
				m_SH.L10  += wc * (A1 * sz);              // Y1, 0 ∝ z
				m_SH.L1p1 += wc * (A1 * sx);              // Y1, 1 ∝ x

				// --- Band 2 (5 coefficients) ---
				float xx = sx * sx;
				float yy = sy * sy;
				float zz = sz * sz;

				m_SH.L2m2 += wc * (A2 * sx * sy);                         // Y2,-2 ∝ xy
				m_SH.L2m1 += wc * (A2 * sy * sz);                         // Y2,-1 ∝ yz
				m_SH.L20  += wc * (A20 * (3.0f * zz - 1.0f));            // Y2, 0 ∝ 3z²-1
				m_SH.L2p1 += wc * (A2 * sx * sz);                         // Y2, 1 ∝ xz
				m_SH.L2p2 += wc * (A2 * 0.5f * (xx - yy));               // Y2, 2 ∝ x²-y²
			}
		}
	}

	EG_CORE_INFO("EnvironmentImage: SH coefficients computed");
	EG_CORE_INFO("  L00  = ({0:.3f}, {1:.3f}, {2:.3f})",
		m_SH.L00.r, m_SH.L00.g, m_SH.L00.b);
}

// ===================================================================
// GetSHData — GPU-friendly 9×vec4 layout
// ===================================================================
std::vector<glm::vec4> EnvironmentImage::GetSHData() const
{
	return {
		glm::vec4(m_SH.L00,  0.0f),
		glm::vec4(m_SH.L1m1, 0.0f),
		glm::vec4(m_SH.L10,  0.0f),
		glm::vec4(m_SH.L1p1, 0.0f),
		glm::vec4(m_SH.L2m2, 0.0f),
		glm::vec4(m_SH.L2m1, 0.0f),
		glm::vec4(m_SH.L20,  0.0f),
		glm::vec4(m_SH.L2p1, 0.0f),
		glm::vec4(m_SH.L2p2, 0.0f),
	};
}

} // namespace Engine
