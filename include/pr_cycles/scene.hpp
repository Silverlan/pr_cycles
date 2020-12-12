/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#ifndef __PR_CYCLES_SCENE_HPP__
#define __PR_CYCLES_SCENE_HPP__

#include <util_raytracing/scene.hpp>
#include <util_raytracing/renderer.hpp>
#include <sharedutils/util_weak_handle.hpp>
#include <sharedutils/util_parallel_job.hpp>
#include <mathutil/transform.hpp>
#include <memory>
#include <mathutil/uvec.h>
#include <functional>
#include <optional>
#include <atomic>
#include <thread>
#include <pragma/entities/baseentity_handle.h>
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
namespace unirender
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
		struct MeshData
		{
			std::vector<Vertex> vertices;
			std::vector<int32_t> triangles;

			std::optional<std::vector<float>> alphas {};
			std::optional<std::vector<float>> wrinkles {};

			unirender::PShader shader = nullptr;
		};
		Cache(unirender::Scene::RenderMode renderMode);
		void AddParticleSystem(pragma::CParticleSystemComponent &ptc,const Vector3 &camPos,const Mat4 &vp,float nearZ,float farZ);
		unirender::PObject AddEntity(
			BaseEntity &ent,std::vector<ModelSubMesh*> *optOutTargetMeshes=nullptr,
			const std::function<bool(ModelMesh&,const umath::ScaledTransform&)> &meshFilter=nullptr,const std::function<bool(ModelSubMesh&,const umath::ScaledTransform&)> &subMeshFilter=nullptr,
			const std::string &nameSuffix=""
		);
		std::vector<std::shared_ptr<MeshData>> AddEntityMesh(
			BaseEntity &ent,std::vector<ModelSubMesh*> *optOutTargetMeshes=nullptr,
			const std::function<bool(ModelMesh&,const umath::ScaledTransform&)> &meshFilter=nullptr,const std::function<bool(ModelSubMesh&,const umath::ScaledTransform&)> &subMeshFilter=nullptr,
			const std::string &nameSuffix="",const std::optional<umath::ScaledTransform> &pose={}
		);
		std::vector<std::shared_ptr<MeshData>> AddModel(
			Model &mdl,const std::string &meshName,BaseEntity *optEnt=nullptr,const std::optional<umath::ScaledTransform> &pose={},uint32_t skinId=0,
			CModelComponent *optMdlC=nullptr,CAnimatedComponent *optAnimC=nullptr,
			const std::function<bool(ModelMesh&,const umath::ScaledTransform&)> &optMeshFilter=nullptr,
			const std::function<bool(ModelSubMesh&,const umath::ScaledTransform&)> &optSubMeshFilter=nullptr,
			const std::function<void(ModelSubMesh&)> &optOnMeshAdded=nullptr
		);
		std::vector<std::shared_ptr<MeshData>> AddMeshList(
			Model &mdl,const std::vector<std::shared_ptr<ModelMesh>> &meshList,const std::string &meshName,BaseEntity *optEnt=nullptr,const std::optional<umath::ScaledTransform> &pose={},uint32_t skinId=0,
			CModelComponent *optMdlC=nullptr,CAnimatedComponent *optAnimC=nullptr,
			const std::function<bool(ModelMesh&,const umath::ScaledTransform&)> &optMeshFilter=nullptr,
			const std::function<bool(ModelSubMesh&,const umath::ScaledTransform&)> &optSubMeshFilter=nullptr,
			const std::function<void(ModelSubMesh&)> &optOnMeshAdded=nullptr
		);
		unirender::PMesh BuildMesh(const std::string &meshName,const std::vector<std::shared_ptr<MeshData>> &meshDatas,const std::optional<umath::ScaledTransform> &pose={}) const;
		void AddAOBakeTarget(BaseEntity &ent,uint32_t matIndex,std::shared_ptr<unirender::Object> &oAo,std::shared_ptr<unirender::Object> &oEnv);
		void AddAOBakeTarget(Model &mdl,uint32_t matIndex,std::shared_ptr<unirender::Object> &oAo,std::shared_ptr<unirender::Object> &oEnv);
		unirender::ModelCache &GetModelCache() const {return *m_mdlCache;}
		unirender::ShaderCache &GetShaderCache() const {return *m_shaderCache;}
		std::unordered_map<unirender::Shader*,std::shared_ptr<Shader>> &GetRTShaderToShaderTable() const {return m_rtShaderToShader;}
	private:
		void AddAOBakeTarget(BaseEntity *optEnt,Model &mdl,uint32_t matIndex,std::shared_ptr<unirender::Object> &oAo,std::shared_ptr<unirender::Object> &oEnv);
		struct ModelCacheInstance
		{
			unirender::PMesh mesh = nullptr;
			uint32_t skin = 0;
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
		void AddMeshDataToMesh(unirender::Mesh &mesh,const MeshData &meshData,const std::optional<umath::ScaledTransform> &pose={}) const;
		void AddMesh(Model &mdl,unirender::Mesh &mesh,ModelSubMesh &mdlMesh,pragma::CModelComponent *optMdlC=nullptr,pragma::CAnimatedComponent *optAnimC=nullptr);
		std::string GetUniqueName() {return "internal" +std::to_string(m_uniqueNameIndex++);};
		std::shared_ptr<MeshData> CalcMeshData(Model &mdl,ModelSubMesh &mdlMesh,bool includeAlphas,bool includeWrinkles,pragma::CModelComponent *optMdlC=nullptr,pragma::CAnimatedComponent *optAnimC=nullptr);
		unirender::PShader CreateShader(Material &mat,const std::string &meshName,const ShaderInfo &shaderInfo={}) const;
		unirender::PShader CreateShader(const std::string &meshName,Model &mdl,ModelSubMesh &subMesh,BaseEntity *optEnt=nullptr,uint32_t skinId=0) const;
		uint32_t m_uniqueNameIndex = 0;
		std::unordered_map<std::string,std::vector<ModelCacheInstance>> m_modelCache;
		mutable std::unordered_map<Material*,size_t> m_materialToShader;
		std::optional<std::string> m_sky {};
		std::shared_ptr<unirender::ModelCache> m_mdlCache = nullptr;
		std::shared_ptr<unirender::ShaderCache> m_shaderCache = nullptr;
		mutable std::unordered_map<unirender::Shader*,std::shared_ptr<Shader>> m_rtShaderToShader {};
		unirender::Scene::RenderMode m_renderMode = unirender::Scene::RenderMode::RenderImage;
	};

	class Scene
		: public std::enable_shared_from_this<Scene>
	{
	public:
		Scene(unirender::Scene &rtScene);
		void AddSkybox(const std::string &texture);
		void Add3DSkybox(pragma::CSkyCameraComponent &skyCam,const Vector3 &camPos);
		void SetAOBakeTarget(Model &mdl,uint32_t matIndex);
		void SetAOBakeTarget(BaseEntity &ent,uint32_t matIndex);
		void AddLightmapBakeTarget(BaseEntity &ent);
		void Finalize();

		Cache &GetCache();

		unirender::Scene &operator*() {return *m_rtScene;};
		const unirender::Scene &operator*() const {return *m_rtScene;};

		unirender::Scene *operator->() {return m_rtScene.get();};
		const unirender::Scene *operator->() const {return m_rtScene.get();};
	private:
		void AddRoughnessMapImageTextureNode(unirender::ShaderModuleRoughness &shader,Material &mat,float defaultRoughness) const;
		void BuildLightMapObject();

		std::vector<EntityHandle> m_lightMapTargets {};
		std::shared_ptr<Cache> m_cache = nullptr;
		std::shared_ptr<unirender::Scene> m_rtScene = nullptr;
	};

	class Renderer
		: public std::enable_shared_from_this<Renderer>
	{
	public:
		Renderer(Scene &scene,unirender::Renderer &renderer);
		void ReloadShaders();

		Scene &GetScene() {return *m_scene;}
		const Scene &GetScene() const {return const_cast<Renderer*>(this)->GetScene();}

		unirender::Renderer *operator->() {return m_renderer.get();}
		const unirender::Renderer *operator->() const {return const_cast<Renderer*>(this)->operator->();}
		unirender::Renderer &operator*() {return *operator->();}
		const unirender::Renderer &operator*() const {return const_cast<Renderer*>(this)->operator*();}
	private:
		std::shared_ptr<Scene> m_scene = nullptr;
		std::shared_ptr<unirender::Renderer> m_renderer = nullptr;
	};
	unirender::NodeManager &get_node_manager();
};

#endif
