#ifndef __PR_CYCLES_SCENE_HPP__
#define __PR_CYCLES_SCENE_HPP__

#include <sharedutils/util_weak_handle.hpp>
#include <memory>
#include <mathutil/uvec.h>
#include <functional>

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
namespace pragma::physics {class Transform;};
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
	class Scene
		: public std::enable_shared_from_this<Scene>
	{
	public:
		struct DenoiseInfo
		{
			uint32_t numThreads = 16;
			uint32_t width = 0;
			uint32_t height = 0;
			bool hdr = false;
		};
		static PScene Create(const std::function<void(const uint8_t*,int,int,int)> &outputHandler,uint32_t sampleCount=1'024,bool hdrOutput=false,bool denoise=false);
		util::WeakHandle<Scene> GetHandle();
		//
		static ccl::float3 ToCyclesPosition(const Vector3 &pos);
		static ccl::float3 ToCyclesNormal(const Vector3 &n);
		static ccl::float2 ToCyclesUV(const Vector2 &uv);
		static ccl::Transform ToCyclesTransform(const pragma::physics::Transform &t);
		static float ToCyclesLength(float len);

		static constexpr uint32_t INPUT_CHANNEL_COUNT = 4u;
		static constexpr uint32_t OUTPUT_CHANNEL_COUNT = 4u;

		~Scene();
		void AddEntity(BaseEntity &ent);
		Camera &GetCamera();
		float GetProgress() const;
		bool IsComplete() const;
		bool IsCancelled() const;
		void Start();
		void Cancel();
		void Wait();
		void SetProgressCallback(const std::function<void(float)> &progressCallback);
		ccl::Scene *operator->();
		ccl::Scene *operator*();

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
		Scene(std::unique_ptr<ccl::Session> session,ccl::Scene &scene);
		static ccl::ShaderOutput *FindShaderNodeOutput(ccl::ShaderNode &node,const std::string &output);
		static ccl::ShaderNode *FindShaderNode(ccl::ShaderGraph &graph,const std::string &nodeName);
		static ccl::ShaderNode *FindShaderNode(ccl::ShaderGraph &graph,const OpenImageIO_v2_1::ustring &name);
		void LinkNormalMap(Shader &shader,Material &mat,const std::string &meshName,const std::string &toNodeName,const std::string &toSocketName);
		PShader CreateShader(const std::string &meshName,Model &mdl,ModelSubMesh &subMesh);
		bool Denoise(const DenoiseInfo &denoise,const void *imgData,std::vector<uint8_t> &outData);
		bool IsValidTexture(const std::string &filePath) const;
		ccl::ImageTextureNode *AssignTexture(Shader &shader,const std::string &texIdentifier,const std::string &texFilePath) const;
		std::vector<PShader> m_shaders = {};
		std::vector<PObject> m_objects = {};
		std::vector<PLight> m_lights = {};
		std::unique_ptr<ccl::Session> m_session = nullptr;
		std::function<void(float)> m_progressCallback = nullptr;
		ccl::Scene &m_scene;
		PCamera m_camera = nullptr;
		bool m_bCancelled = false;
	};
};

#endif
