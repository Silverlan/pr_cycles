/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#ifndef __PR_CYCLES_SCENE_HPP__
#define __PR_CYCLES_SCENE_HPP__

#include <util_raytracing/scene.hpp>
#include <sharedutils/util_weak_handle.hpp>
#include <sharedutils/util_parallel_job.hpp>
#include <memory>
#include <mathutil/uvec.h>
#include <functional>
#include <optional>
#include <atomic>
#include <thread>
#include <pragma/model/vertex.h>

#define ENABLE_TEST_AMBIENT_OCCLUSION

class BaseEntity;

namespace pragma
{
	class CAnimatedComponent;
	class CLightMapComponent;
	class CParticleSystemComponent;
	class CSkyCameraComponent;
	class CModelComponent;
};
namespace umath {class Transform; class ScaledTransform;};
namespace raytracing
{
	class Scene;
};
namespace util::bsp {struct LightMapInfo;};
namespace uimg {class ImageBuffer;};
class Model;
class ModelMesh;
class ModelSubMesh;
class Material;
class CParticle;
class DataStream;
namespace pragma::modules::cycles
{
	class Shader;
	util::ParallelJob<std::shared_ptr<uimg::ImageBuffer>> denoise(uimg::ImageBuffer &imgBuffer);
	class Cache
	{
	public:
		Cache(raytracing::Scene::RenderMode renderMode);
		void AddParticleSystem(pragma::CParticleSystemComponent &ptc,const Vector3 &camPos,const Mat4 &vp,float nearZ,float farZ);
		raytracing::PObject AddEntity(
			BaseEntity &ent,std::vector<ModelSubMesh*> *optOutTargetMeshes=nullptr,
			const std::function<bool(ModelMesh&,const Vector3&,const Quat&)> &meshFilter=nullptr,const std::function<bool(ModelSubMesh&,const Vector3&,const Quat&)> &subMeshFilter=nullptr,
			const std::string &nameSuffix=""
		);
		raytracing::PMesh AddModel(
			Model &mdl,const std::string &meshName,BaseEntity *optEnt=nullptr,uint32_t skinId=0,
			CModelComponent *optMdlC=nullptr,CAnimatedComponent *optAnimC=nullptr,
			const std::function<bool(ModelMesh&,const Vector3&,const Quat&)> &optMeshFilter=nullptr,
			const std::function<bool(ModelSubMesh&,const Vector3&,const Quat&)> &optSubMeshFilter=nullptr
		);
		raytracing::PMesh AddMeshList(
			Model &mdl,const std::vector<std::shared_ptr<ModelMesh>> &meshList,const std::string &meshName,BaseEntity *optEnt=nullptr,uint32_t skinId=0,
			CModelComponent *optMdlC=nullptr,CAnimatedComponent *optAnimC=nullptr,
			const std::function<bool(ModelMesh&,const Vector3&,const Quat&)> &optMeshFilter=nullptr,
			const std::function<bool(ModelSubMesh&,const Vector3&,const Quat&)> &optSubMeshFilter=nullptr
		);
		void AddAOBakeTarget(Model &mdl,uint32_t matIndex,std::shared_ptr<raytracing::Object> &oAo,std::shared_ptr<raytracing::Object> &oEnv);
		raytracing::ModelCache &GetModelCache() const {return *m_mdlCache;}
		raytracing::ShaderCache &GetShaderCache() const {return *m_shaderCache;}
		std::unordered_map<raytracing::Shader*,std::shared_ptr<Shader>> &GetRTShaderToShaderTable() const {return m_rtShaderToShader;}
	private:
		struct ModelCacheInstance
		{
			raytracing::PMesh mesh = nullptr;
			uint32_t skin = 0;
		};
		struct MeshData
		{
			std::vector<Vertex> vertices;
			std::vector<int32_t> triangles;

			std::optional<std::vector<float>> alphas {};
			std::optional<std::vector<float>> wrinkles {};

			raytracing::PShader shader = nullptr;
		};
		struct ShaderInfo
		{
			// These are only required if the shader is used for eyeballs
			std::optional<BaseEntity*> entity = {};
			std::optional<ModelSubMesh*> subMesh = {};

			std::optional<pragma::CParticleSystemComponent*> particleSystem = {};
			std::optional<const void*> particle = {};
		};
		Material *GetMaterial(Model &mdl,ModelSubMesh &subMesh,uint32_t skinId) const;
		Material *GetMaterial(pragma::CModelComponent &mdlC,ModelSubMesh &subMesh,uint32_t skinId) const;
		Material *GetMaterial(BaseEntity &ent,ModelSubMesh &subMesh,uint32_t skinId) const;
		void AddMeshDataToMesh(raytracing::Mesh &mesh,const MeshData &meshData) const;
		void AddMesh(Model &mdl,raytracing::Mesh &mesh,ModelSubMesh &mdlMesh,pragma::CModelComponent *optMdlC=nullptr,pragma::CAnimatedComponent *optAnimC=nullptr);
		std::string GetUniqueName() {return "internal" +std::to_string(m_uniqueNameIndex++);};
		raytracing::PMesh BuildMesh(const std::string &meshName,const std::vector<std::shared_ptr<MeshData>> &meshDatas) const;
		std::shared_ptr<MeshData> CalcMeshData(Model &mdl,ModelSubMesh &mdlMesh,bool includeAlphas,bool includeWrinkles,pragma::CModelComponent *optMdlC=nullptr,pragma::CAnimatedComponent *optAnimC=nullptr);
		raytracing::PShader CreateShader(Material &mat,const std::string &meshName,const ShaderInfo &shaderInfo={}) const;
		raytracing::PShader CreateShader(const std::string &meshName,Model &mdl,ModelSubMesh &subMesh,BaseEntity *optEnt=nullptr,uint32_t skinId=0) const;
		uint32_t m_uniqueNameIndex = 0;
		std::unordered_map<std::string,std::vector<ModelCacheInstance>> m_modelCache;
		mutable std::unordered_map<Material*,size_t> m_materialToShader;
		std::optional<std::string> m_sky {};
		std::shared_ptr<raytracing::ModelCache> m_mdlCache = nullptr;
		std::shared_ptr<raytracing::ShaderCache> m_shaderCache = nullptr;
		mutable std::unordered_map<raytracing::Shader*,std::shared_ptr<Shader>> m_rtShaderToShader {};
		raytracing::Scene::RenderMode m_renderMode = raytracing::Scene::RenderMode::RenderImage;
	};

	class Scene
		: public std::enable_shared_from_this<Scene>
	{
	public:
		Scene(raytracing::Scene &rtScene);
		void AddSkybox(const std::string &texture);
		void Add3DSkybox(pragma::CSkyCameraComponent &skyCam,const Vector3 &camPos);
		void SetAOBakeTarget(Model &mdl,uint32_t matIndex);
		void SetLightmapBakeTarget(BaseEntity &ent);
		void Finalize();

		Cache &GetCache();
		void ReloadShaders();

		raytracing::Scene &operator*() {return *m_rtScene;};
		const raytracing::Scene &operator*() const {return *m_rtScene;};

		raytracing::Scene *operator->() {return m_rtScene.get();};
		const raytracing::Scene *operator->() const {return m_rtScene.get();};
	private:
		void AddRoughnessMapImageTextureNode(raytracing::ShaderModuleRoughness &shader,Material &mat,float defaultRoughness) const;

		std::shared_ptr<Cache> m_cache = nullptr;
		std::shared_ptr<raytracing::Scene> m_rtScene = nullptr;
		util::WeakHandle<pragma::CLightMapComponent> m_lightmapTargetComponent = {};
	};
	raytracing::NodeManager &get_node_manager();
};

#endif
