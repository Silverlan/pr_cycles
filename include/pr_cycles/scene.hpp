#ifndef __PR_CYCLES_SCENE_HPP__
#define __PR_CYCLES_SCENE_HPP__

#include <sharedutils/util_weak_handle.hpp>
#include <sharedutils/util_parallel_job.hpp>
#include <memory>
#include <mathutil/uvec.h>
#include <functional>
#include <optional>
#include <atomic>
#include <thread>

#define ENABLE_TEST_AMBIENT_OCCLUSION

class BaseEntity;

namespace ccl
{
	class Session;
	class Scene;
	class ShaderInput;
	class ShaderNode;
	class ShaderOutput;
	class ShaderGraph;
	struct float3;
	struct float2;
	struct Transform;
	class ImageTextureNode;
};
namespace OpenImageIO_v2_1
{
	class ustring;
};
namespace pragma
{
	class CAnimatedComponent;
	class CLightMapComponent;
	namespace physics {class Transform;};
};
namespace util::bsp {struct LightMapInfo;};
namespace util {class ImageBuffer;};
class Model;
class ModelSubMesh;
class Material;
namespace pragma::modules::cycles
{
	class SceneObject;
	class Scene;
	class Shader;
	using PShader = std::shared_ptr<Shader>;
	class Object;
	using PObject = std::shared_ptr<Object>;
	class Light;
	using PLight = std::shared_ptr<Light>;
	class Camera;
	using PCamera = std::shared_ptr<Camera>;
	using PScene = std::shared_ptr<Scene>;
	class Mesh;
	using PMesh = std::shared_ptr<Mesh>;
	class Scene
		: public util::ParallelWorker<std::shared_ptr<util::ImageBuffer>>
	{
	public:
		struct DenoiseInfo
		{
			uint32_t numThreads = 16;
			uint32_t width = 0;
			uint32_t height = 0;
			bool hdr = false;
		};
		enum class ColorSpace : uint8_t
		{
			SRGB = 0,
			Raw
		};
		enum class RenderMode : uint8_t
		{
			RenderImage = 0u,
			BakeAmbientOcclusion,
			BakeNormals,
			BakeDiffuseLighting
		};
		static util::ParallelJob<std::shared_ptr<util::ImageBuffer>> Create(RenderMode renderMode,std::optional<uint32_t> sampleCount={},bool hdrOutput=false,bool denoise=false);
		//
		static ccl::float3 ToCyclesPosition(const Vector3 &pos);
		static ccl::float3 ToCyclesNormal(const Vector3 &n);
		static ccl::float2 ToCyclesUV(const Vector2 &uv);
		static ccl::Transform ToCyclesTransform(const pragma::physics::Transform &t);
		static float ToCyclesLength(float len);
		static bool Denoise(const DenoiseInfo &denoise,float *inOutData,const std::function<bool(float)> &fProgressCallback=nullptr);

		static constexpr uint32_t INPUT_CHANNEL_COUNT = 4u;
		static constexpr uint32_t OUTPUT_CHANNEL_COUNT = 4u;

		~Scene();
		PObject AddEntity(BaseEntity &ent,std::vector<ModelSubMesh*> *optOutTargetMeshes=nullptr);
		PMesh AddModel(Model &mdl,const std::string &meshName,uint32_t skinId=0,CAnimatedComponent *optAnimC=nullptr,const std::function<bool(ModelSubMesh&)> &optMeshFilter=nullptr);
		void SetAOBakeTarget(Model &mdl,uint32_t matIndex);
		void SetLightmapBakeTarget(BaseEntity &ent);
		Camera &GetCamera();
		float GetProgress() const;
		RenderMode GetRenderMode() const;
		ccl::Scene *operator->();
		ccl::Scene *operator*();

		virtual void Start() override;
		virtual void Cancel(const std::string &resultMsg) override;
		virtual void Wait() override;
		virtual std::shared_ptr<util::ImageBuffer> GetResult() override;

		const std::vector<PShader> &GetShaders() const;
		std::vector<PShader> &GetShaders();
		const std::vector<PObject> &GetObjects() const;
		std::vector<PObject> &GetObjects();
		const std::vector<PLight> &GetLights() const;
		std::vector<PLight> &GetLights();

		ccl::Session *GetCCLSession();
	private:
		friend Shader;
		friend Object;
		friend Light;
		template<typename TJob,typename... TARGS>
			friend util::ParallelJob<typename TJob::RESULT_TYPE> util::create_parallel_job(TARGS&& ...args);
		Scene(std::unique_ptr<ccl::Session> session,ccl::Scene &scene,RenderMode renderMode);
		static ccl::ShaderOutput *FindShaderNodeOutput(ccl::ShaderNode &node,const std::string &output);
		static ccl::ShaderNode *FindShaderNode(ccl::ShaderGraph &graph,const std::string &nodeName);
		static ccl::ShaderNode *FindShaderNode(ccl::ShaderGraph &graph,const OpenImageIO_v2_1::ustring &name);
		void ApplyPostProcessing(util::ImageBuffer &imgBuffer,cycles::Scene::RenderMode renderMode,bool denoise);
		void DenoiseHDRImageArea(util::ImageBuffer &imgBuffer,uint32_t imgWidth,uint32_t imgHeight,uint32_t x,uint32_t y,uint32_t w,uint32_t h) const;
		void AddMesh(Model &mdl,Mesh &mesh,ModelSubMesh &mdlMesh,pragma::CAnimatedComponent *optAnimC=nullptr,uint32_t skinId=0);
		void LinkNormalMap(Shader &shader,Material &mat,const std::string &meshName,const std::string &toNodeName,const std::string &toSocketName);
		PShader CreateShader(const std::string &meshName,Model &mdl,ModelSubMesh &subMesh,uint32_t skinId=0);
		bool Denoise(const DenoiseInfo &denoise,util::ImageBuffer &imgBuffer,const std::function<bool(float)> &fProgressCallback=nullptr) const;
		bool IsValidTexture(const std::string &filePath) const;
		ccl::ImageTextureNode *AssignTexture(Shader &shader,const std::string &texIdentifier,const std::string &texFilePath,ColorSpace colorSpace=ColorSpace::SRGB) const;
		void ClearCyclesScene();

		std::vector<PShader> m_shaders = {};
		std::vector<PObject> m_objects = {};
		std::vector<PLight> m_lights = {};
		std::unique_ptr<ccl::Session> m_session = nullptr;
		ccl::Scene &m_scene;
		PCamera m_camera = nullptr;
		bool m_bDenoise = false;
		bool m_bHDROutput = false;
		RenderMode m_renderMode = RenderMode::RenderImage;
		std::weak_ptr<Object> m_bakeTarget = {};
		util::WeakHandle<pragma::CLightMapComponent> m_lightmapTargetComponent = {};
		std::shared_ptr<util::ImageBuffer> m_resultImageBuffer = nullptr;
	};
};

#endif
