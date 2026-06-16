#pragma once
#include <glm/glm.hpp>
#include <map>
#include <Engine/Renderer/EditorCamera.h>
#include <Engine/Scene/Components.h>
#include <Engine/Renderer/Shader.h>
#include <Engine/Renderer/Buffer.h>

namespace Engine {

	struct MeshRenderCommandRequest
	{
		glm::mat4 Transform;
		std::shared_ptr<Mesh> Mesh;
		std::shared_ptr<Material> Material;
		int EntityID;

		uint32_t SubmeshIndexCount;
		uint32_t SubmeshFirstIndex;
		uint32_t SubmeshFirstVertex;

	};

	struct GaussianRenderCommandRequest
	{
		glm::mat4 Transform;
		std::shared_ptr<GaussianModel> Model;
		int EntityID;
	};

	// Frame info for 3DGS splat rendering (std140 compatible)
	struct GaussianFrameInfo
	{
		glm::mat4 viewMatrix           = glm::mat4(1.0f);
		glm::mat4 projectionMatrix     = glm::mat4(1.0f);
		glm::vec4 cameraPosAndScale    = glm::vec4(0.0f); // xyz = camera position, w = splatScale
		glm::vec4 focal                = glm::vec4(0.0f); // xy = focal length, zw = unused
		glm::vec4 viewportInfo         = glm::vec4(0.0f); // xy = viewport size, zw = basisViewport
		glm::vec4 extraInfo            = glm::vec4(0.0f); // x = alphaCullThreshold, yzw = unused
	};

	class Renderer
	{
	public:
		enum class API
		{
			None = 0, Vulkan = 1
		};

	public:
		static void Init();
		// static void Shutdown();
		//
		// static void OnWindowResize(uint32_t width, uint32_t height);
		//
		// static void BeginScene(const Camera& camera, const glm::mat4& transform);
		static void BeginScene(const EditorCamera& camera);
		static void EndScene();       // 录制场景命令 (Mesh + Gaussian)，不提交
		static void PresentFrame();   // 绘制 ImGui + 提交命令缓冲 + 呈现
		static void OnSwapchainRecreated(); // Swapchain 重建后更新深度纹理 descriptor

		static void SubmitMesh(const glm::mat4& transform, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<MeshRendererComponent>& rendererComponent, int entityID = -1);
		static void SubmitGaussian(const glm::mat4& transform, const std::shared_ptr<GaussianModel>& model, int entityID = -1);

		static API GetAPI() { return s_API; }
		static std::shared_ptr<ShaderLibrary> GetShaderLibrary() { return s_ShaderLibrary; }

		static Renderer* Create();

		// Register a GaussianModel into the global SSBO (assigns offset)
		static void RegisterGaussianModel(const std::shared_ptr<GaussianModel>& model);

	private:
		static void DrawMesh(const MeshRenderCommandRequest& request);
		static void DrawGaussian(const GaussianRenderCommandRequest& request);
		static void FlushMeshPass();
		static void FlushGaussianPass();
		static void RebuildGlobalGaussianSSBO();
		static void CreateGaussianDescriptorSet();

		// 3DGS: GPU sorting
		static void SortGaussiansOnGPU();
		static void CreateGaussianSplatBuffers();
		static void CreateGaussianSortPipelines();
		static void CreateGaussianSortDescriptorSets();
		static void DestroyGaussianSortResources();

	private:
		static API s_API;

		struct SceneData
		{
			glm::mat4 ViewProjectionMatrix;
			glm::mat4 ViewMatrix;
			glm::mat4 ProjectionMatrix;
			glm::vec3 CameraPosition;
			glm::vec3 CameraForward;
		};
		static std::vector<MeshRenderCommandRequest> s_MeshRenderQueue;
		static std::vector<GaussianRenderCommandRequest> s_GaussianRenderQueue;
		static std::shared_ptr<ShaderLibrary> s_ShaderLibrary;
		static SceneData s_SceneData;

		// Global Gaussian SSBO: all models' data in one buffer
		static std::shared_ptr<ShaderStorageBuffer> s_GlobalGaussianSSBO;
		static uint32_t s_TotalGaussianCount;
		static bool s_GaussianSSBODirty;
		static std::vector<std::shared_ptr<GaussianModel>> s_RegisteredGaussianModels;

		// Gaussian DescriptorSet (depth texture + global SSBO, bind once per frame)
		static VkDescriptorSet s_GaussianDescriptorSet;

		// 3DGS Splat: FrameInfo UBO + sorted indices SSBO
		static VkBuffer s_FrameInfoUBO;
		static VkDeviceMemory s_FrameInfoUBOMemory;
		static GaussianFrameInfo s_FrameInfoData;
		static VkDescriptorSet s_GaussianSplatDescriptorSet;

		// 3DGS GPU Sort: distance/keys + indices/values (ping-pong)
		static VkBuffer s_DistancesBuffer[2];
		static VkDeviceMemory s_DistancesBufferMemory[2];
		static VkBuffer s_IndicesBuffer[2];
		static VkDeviceMemory s_IndicesBufferMemory[2];

		// 3DGS GPU Sort: histogram buffers
		static VkBuffer s_GlobalHistogramBuffer;
		static VkDeviceMemory s_GlobalHistogramBufferMemory;
		static VkBuffer s_PartitionHistogramBuffer;
		static VkDeviceMemory s_PartitionHistogramBufferMemory;

		// 3DGS GPU Sort: compute pipelines + descriptor sets
		static VkPipeline s_DistComputePipeline;
		static VkPipelineLayout s_DistComputePipelineLayout;
		static VkDescriptorSetLayout s_DistDescriptorSetLayout;
		static VkDescriptorSet s_DistDescriptorSets[2]; // per frame in flight

		static VkPipeline s_UpsweepPipeline;
		static VkPipelineLayout s_UpsweepPipelineLayout;
		static VkDescriptorSetLayout s_UpsweepDescriptorSetLayout;
		static VkDescriptorSet s_UpsweepDescriptorSets[2]; // [0]=read buf0, [1]=read buf1

		static VkPipeline s_SpinePipeline;
		static VkPipelineLayout s_SpinePipelineLayout;
		static VkDescriptorSetLayout s_SpineDescriptorSetLayout;
		static VkDescriptorSet s_SpineDescriptorSet;

		static VkPipeline s_DownsweepPipeline;
		static VkPipelineLayout s_DownsweepPipelineLayout;
		static VkDescriptorSetLayout s_DownsweepDescriptorSetLayout;
		static VkDescriptorSet s_DownsweepDescriptorSets[2]; // [0]=in buf0 out buf1, [1]=in buf1 out buf0

		// Sorted indices SSBO (for GaussianSplat.vert to read)
		// After sorting, the "values" buffer contains the sorted indices
		// We point the GaussianSplat descriptor to the correct indices buffer
		static uint32_t s_CurrentSortBuffer; // 0 or 1 (ping-pong)

        // Model transform SSBO: one mat4 per registered model, updated every frame
        static VkBuffer s_ModelTransformSSBO;
        static VkDeviceMemory s_ModelTransformSSBOMemory;

        // Sort cache: skip re-sorting when nothing changed
        static bool s_SortCacheValid;
        static glm::mat4 s_PrevViewMatrix;
        static glm::mat4 s_PrevProjectionMatrix;
        static std::vector<glm::mat4> s_PrevModelTransforms;
    };
}
