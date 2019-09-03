#ifndef __PR_CYCLES_SCENE_HPP__
#define __PR_CYCLES_SCENE_HPP__

#include <memory>
#include <mathutil/uvec.h>

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
};
namespace OpenImageIO_v2_1
{
	class ustring;
};
namespace pragma::physics {class Transform;};
class Model;
class ModelSubMesh;
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
	{
	public:
		static PScene Create();
		static ccl::float3 ToCyclesPosition(const Vector3 &pos);
		static ccl::float3 ToCyclesNormal(const Vector3 &n);
		static ccl::float2 ToCyclesUV(const Vector2 &uv);
		static ccl::Transform ToCyclesTransform(const pragma::physics::Transform &t);
		static float ToCyclesLength(float len);

		~Scene();
		void AddEntity(BaseEntity &ent);
		Camera &GetCamera();
		void Start();
		ccl::Scene *operator->();
		ccl::Scene *operator*();
	private:
		friend Shader;
		friend Object;
		friend Light;
		Scene(std::unique_ptr<ccl::Session> session,ccl::Scene &scene);
		static ccl::ShaderOutput *FindShaderNodeOutput(ccl::ShaderNode &node,const std::string &output);
		static ccl::ShaderNode *FindShaderNode(ccl::ShaderGraph &graph,const std::string &nodeName);
		static ccl::ShaderNode *FindShaderNode(ccl::ShaderGraph &graph,const OpenImageIO_v2_1::ustring &name);
		PShader CreateShader(const std::string &meshName,Model &mdl,ModelSubMesh &subMesh);
		bool WriteRender(const uint8_t *pixels,int w,int h,int channels);
		void SessionPrint(const std::string &str);
		void SessionPrintStatus();
		std::vector<PShader> m_shaders = {};
		std::vector<PObject> m_objects = {};
		std::vector<PLight> m_lights = {};
		std::unique_ptr<ccl::Session> m_session = nullptr;
		ccl::Scene &m_scene;
		PCamera m_camera = nullptr;
	};
};

#endif
