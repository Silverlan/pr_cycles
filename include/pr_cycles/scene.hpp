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
	class Scene
		: public std::enable_shared_from_this<Scene>
	{
	public:
		Scene(raytracing::Scene &rtScene);
		raytracing::PObject AddEntity(
			BaseEntity &ent,std::vector<ModelSubMesh*> *optOutTargetMeshes=nullptr,
			const std::function<bool(ModelMesh&,const Vector3&,const Quat&)> &meshFilter=nullptr,const std::function<bool(ModelSubMesh&,const Vector3&,const Quat&)> &subMeshFilter=nullptr,
			const std::string &nameSuffix=""
		);
		void AddParticleSystem(pragma::CParticleSystemComponent &ptc,const Vector3 &camPos,const Mat4 &vp,float nearZ,float farZ);
		void AddSkybox(const std::string &texture);
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
		void Add3DSkybox(pragma::CSkyCameraComponent &skyCam,const Vector3 &camPos);
		void SetAOBakeTarget(Model &mdl,uint32_t matIndex);
		void SetLightmapBakeTarget(BaseEntity &ent);

		raytracing::Scene &operator*() {return *m_rtScene;};
		const raytracing::Scene &operator*() const {return *m_rtScene;};

		raytracing::Scene *operator->() {return m_rtScene.get();};
		const raytracing::Scene *operator->() const {return m_rtScene.get();};
	private:
		struct ModelCacheInstance
		{
			raytracing::PMesh mesh = nullptr;
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
		void AddMesh(Model &mdl,raytracing::Mesh &mesh,ModelSubMesh &mdlMesh,pragma::CModelComponent *optMdlC=nullptr,pragma::CAnimatedComponent *optAnimC=nullptr,BaseEntity *optEnt=nullptr,uint32_t skinId=0);
		void AddRoughnessMapImageTextureNode(raytracing::ShaderModuleRoughness &shader,Material &mat,float defaultRoughness) const;
		raytracing::PShader CreateShader(Material &mat,const std::string &meshName,const ShaderInfo &shaderInfo={});
		raytracing::PShader CreateShader(raytracing::Mesh &mesh,Model &mdl,ModelSubMesh &subMesh,BaseEntity *optEnt=nullptr,uint32_t skinId=0);
		Material *GetMaterial(Model &mdl,ModelSubMesh &subMesh,uint32_t skinId) const;
		Material *GetMaterial(pragma::CModelComponent &mdlC,ModelSubMesh &subMesh,uint32_t skinId) const;
		Material *GetMaterial(BaseEntity &ent,ModelSubMesh &subMesh,uint32_t skinId) const;

		std::unordered_map<std::string,std::vector<ModelCacheInstance>> m_modelCache;
		std::shared_ptr<raytracing::Scene> m_rtScene = nullptr;
		util::WeakHandle<pragma::CLightMapComponent> m_lightmapTargetComponent = {};
	};
};

#endif
