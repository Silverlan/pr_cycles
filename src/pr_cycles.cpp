/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#include "pr_cycles/pr_cycles.hpp"
#include <render/buffers.h>
#include <render/scene.h>
#include <render/session.h>
#include <render/shader.h>
#include <render/camera.h>
#include <render/light.h>
#include <render/mesh.h>
#include <render/graph.h>
#include <render/nodes.h>
#include <render/object.h>
#include <render/background.h>
#include <render/svm.h>

namespace pragma::asset {class WorldData; class EntityData;};
#include <pragma/lua/luaapi.h>
#include <prosper_context.hpp>
#include <pragma/c_engine.h>
#include <pragma/game/c_game.h>
#include <pragma/entities/baseentity.h>
#include <pragma/model/model.h>
#include <pragma/model/c_modelmesh.h>
#include <pragma/entities/components/c_player_component.hpp>
#include <pragma/entities/components/c_color_component.hpp>
#include <pragma/entities/components/c_model_component.hpp>
#include <pragma/entities/components/c_render_component.hpp>
#include <pragma/entities/components/c_toggle_component.hpp>
#include <pragma/entities/environment/effects/c_env_particle_system.h>
#include <pragma/entities/environment/c_sky_camera.hpp>
#include <pragma/entities/environment/c_env_camera.h>
#include <pragma/entities/entity_iterator.hpp>
#include <pragma/entities/environment/lights/c_env_light.h>
#include <pragma/entities/environment/lights/c_env_light_spot.h>
#include <pragma/entities/environment/lights/c_env_light_point.h>
#include <pragma/entities/environment/lights/c_env_light_directional.h>
#include <pragma/entities/entity_component_system_t.hpp>
#include <pragma/util/util_game.hpp>
#include <pragma/rendering/renderers/rasterization_renderer.hpp>
#include <pragma/rendering/occlusion_culling/occlusion_culling_handler_bsp.hpp>
#include <pragma/lua/classes/ldef_entity.h>

#undef __SCENE_H__
#include <pragma/rendering/scene/scene.h>

#include <luainterface.hpp>
#include <pragma/lua/lua_entity_component.hpp>
#include <pragma/lua/lua_call.hpp>

// Already defined b
#undef __UTIL_STRING_H__
#include <sharedutils/util_string.h>

#include <sharedutils/datastream.h>
#include <sharedutils/util.h>
#include "pr_cycles/scene.hpp"
#include "util_raytracing/shader.hpp"
#include "util_raytracing/camera.hpp"
#include "util_raytracing/light.hpp"
#include "util_raytracing/mesh.hpp"
#include "util_raytracing/object.hpp"

#pragma optimize("",off)

extern DLLCLIENT CGame *c_game;

using namespace pragma::modules;

static void setup_light_sources(cycles::Scene &scene,const std::function<bool(BaseEntity&)> &lightFilter=nullptr)
{
	EntityIterator entIt {*c_game};
	entIt.AttachFilter<TEntityIteratorFilterComponent<pragma::CLightComponent>>();
	for(auto *ent : entIt)
	{
		auto lightC = ent->GetComponent<pragma::CLightComponent>();
		auto toggleC = ent->GetComponent<pragma::CToggleComponent>();
		if(toggleC.valid() && toggleC->IsTurnedOn() == false || (lightFilter && lightFilter(*ent) == false))
			continue;
		auto colorC = ent->GetComponent<pragma::CColorComponent>();
		auto color = Color::White;
		if(colorC.valid())
			color = colorC->GetColor();
		auto hLightSpot = ent->GetComponent<pragma::CLightSpotComponent>();
		if(hLightSpot.valid())
		{
			auto light = raytracing::Light::Create(*scene);
			if(light)
			{
				light->SetType(raytracing::Light::Type::Spot);
				light->SetPos(ent->GetPosition());
				light->SetRotation(ent->GetRotation());
				light->SetConeAngles(umath::deg_to_rad(hLightSpot->GetInnerCutoffAngle()) *2.f,umath::deg_to_rad(hLightSpot->GetOuterCutoffAngle()) *2.f);
				light->SetColor(color);
				light->SetIntensity(lightC->GetLightIntensityLumen());
			}
			continue;
		}
		auto hLightPoint = ent->GetComponent<pragma::CLightPointComponent>();
		if(hLightPoint.valid())
		{
			auto light = raytracing::Light::Create(*scene);
			if(light)
			{
				light->SetType(raytracing::Light::Type::Point);
				light->SetPos(ent->GetPosition());
				light->SetRotation(ent->GetRotation());
				light->SetColor(color);
				light->SetIntensity(lightC->GetLightIntensityLumen());
			}
			continue;
		}
		auto hLightDirectional = ent->GetComponent<pragma::CLightDirectionalComponent>();
		if(hLightDirectional.valid())
		{
			auto light = raytracing::Light::Create(*scene);
			if(light)
			{
				light->SetType(raytracing::Light::Type::Directional);
				light->SetPos(ent->GetPosition());
				light->SetRotation(ent->GetRotation());
				light->SetColor(color);
				light->SetIntensity(lightC->GetLightIntensity());
			}
		}
	}
}

static std::shared_ptr<cycles::Scene> setup_scene(raytracing::Scene::RenderMode renderMode,uint32_t width,uint32_t height,uint32_t sampleCount,bool hdrOutput,bool denoise,raytracing::Scene::DeviceType deviceType=raytracing::Scene::DeviceType::CPU)
{
	raytracing::Scene::CreateInfo createInfo {};
	createInfo.denoise = denoise;
	createInfo.hdrOutput = hdrOutput;
	createInfo.samples = sampleCount;
	createInfo.deviceType = deviceType;
	auto scene = raytracing::Scene::Create(renderMode,createInfo);
	if(scene == nullptr)
		return nullptr;
#ifdef ENABLE_MOTION_BLUR_TEST
	scene->SetMotionBlurStrength(1.f);
#endif
	auto &cam = scene->GetCamera();
	cam.SetResolution(width,height);
	return std::make_shared<cycles::Scene>(*scene);
}

enum class SceneFlags : uint8_t
{
	None = 0u,
	CullObjectsOutsidePvs = 1u,
	CullObjectsOutsideCameraFrustum = CullObjectsOutsidePvs<<1u
};
REGISTER_BASIC_BITWISE_OPERATORS(SceneFlags)

static void initialize_cycles_scene_from_game_scene(
	Scene &gameScene,cycles::Scene &scene,const Vector3 &camPos,const Quat &camRot,const Mat4 &vp,float nearZ,float farZ,float fov,float aspectRatio,SceneFlags sceneFlags,
	const std::function<bool(BaseEntity&)> &entFilter=nullptr,const std::function<bool(BaseEntity&)> &lightFilter=nullptr
)
{
	auto enableFrustumCulling = umath::is_flag_set(sceneFlags,SceneFlags::CullObjectsOutsideCameraFrustum);
	auto cullObjectsOutsidePvs = umath::is_flag_set(sceneFlags,SceneFlags::CullObjectsOutsidePvs);
	std::vector<Plane> planes {};
	auto forward = uquat::forward(camRot);
	auto up = uquat::up(camRot);
	pragma::BaseEnvCameraComponent::GetFrustumPlanes(planes,nearZ,farZ,fov,aspectRatio,camPos,forward,up);

	auto entSceneFilterEx = [&gameScene,&camPos,&planes,enableFrustumCulling](BaseEntity &ent,bool useFrustumCullingIfEnabled) -> bool {
		if(static_cast<CBaseEntity&>(ent).IsInScene(gameScene) == false)
			return false;
		if(useFrustumCullingIfEnabled == false || enableFrustumCulling == false)
			return true;
		auto renderC = ent.GetComponent<pragma::CRenderComponent>();
		if(renderC.expired())
			return false;
		if(renderC->IsExemptFromOcclusionCulling())
			return true;
		if(renderC->ShouldDraw(camPos) == false)
			return false;
		auto sphere = renderC->GetRenderSphereBounds();
		umath::Transform pose;
		ent.GetPose(pose);
		auto pos = pose.GetOrigin();
		if(Intersection::SphereInPlaneMesh(pos +sphere.pos,sphere.radius,planes,true) == INTERSECT_OUTSIDE)
			return false;
		return true;
		/* // TODO: Take rotation into account
		Vector3 min;
		Vector3 max;
		renderC->GetRenderBounds(&min,&max);
		min = pose *min;
		max = pose *max;
		uvec::to_min_max(min,max);
		return Intersection::AABBInPlaneMesh(min,max,planes) != INTERSECT_OUTSIDE;*/
	};
	auto entSceneFilter = [&entSceneFilterEx](BaseEntity &ent) -> bool {return entSceneFilterEx(ent,true);};
	setup_light_sources(scene,[&entSceneFilterEx,&lightFilter](BaseEntity &ent) -> bool {
		return entSceneFilterEx(ent,false) && (lightFilter == nullptr || lightFilter(ent));
	});
	auto &cam = scene->GetCamera();
	cam.SetPos(camPos);
	cam.SetRotation(camRot);
	cam.SetNearZ(nearZ);
	cam.SetFarZ(farZ);
	cam.SetFOV(umath::deg_to_rad(fov));

	util::BSPTree *bspTree = nullptr;
	util::BSPTree::Node *node = nullptr;
	if(cullObjectsOutsidePvs)
	{
		EntityIterator entItWorld {*c_game};
		entItWorld.AttachFilter<TEntityIteratorFilterComponent<pragma::CWorldComponent>>();
		auto it = entItWorld.begin();
		auto *entWorld = (it != entItWorld.end()) ? *it : nullptr;
		if(entWorld)
		{
			auto worldC = entWorld->GetComponent<pragma::CWorldComponent>();
			bspTree = worldC.valid() ? worldC->GetBSPTree().get() : nullptr;
			node = bspTree ? bspTree->FindLeafNode(camPos) : nullptr;
		}
	}

	// All entities
	EntityIterator entIt {*c_game};
	entIt.AttachFilter<TEntityIteratorFilterComponent<pragma::CRenderComponent>>();
	entIt.AttachFilter<TEntityIteratorFilterComponent<pragma::CModelComponent>>();
	entIt.AttachFilter<EntityIteratorFilterUser>(entSceneFilter);
	for(auto *ent : entIt)
	{
		auto renderC = ent->GetComponent<pragma::CRenderComponent>();
		auto renderMode = renderC->GetRenderMode();
		if((renderMode != RenderMode::World && renderMode != RenderMode::Skybox) || renderC->ShouldDraw(camPos) == false || (entFilter && entFilter(*ent) == false))
			continue;
		std::function<bool(ModelMesh&,const Vector3&,const Quat&)> meshFilter = nullptr;
		if(renderC->IsExemptFromOcclusionCulling() == false)
		{
			// We'll only do per-mesh culling for world entities
			if(enableFrustumCulling  && ent->IsWorld())
			{
				meshFilter = [&planes](ModelMesh &mesh,const Vector3 &origin,const Quat &rotation) -> bool {
					Vector3 min,max;
					mesh.GetBounds(min,max);
					auto center = (min +max) /2.f;
					min -= center;
					max -= center;
					auto r = umath::max(umath::abs(min.x),umath::abs(min.y),umath::abs(min.z),umath::abs(max.x),umath::abs(max.y),umath::abs(max.z));
					center += origin;
					return (Intersection::SphereInPlaneMesh(center,r,planes) != INTERSECT_OUTSIDE) ? true : false;
				};
			}
			if(node)
			{
				auto curFilter = meshFilter;
				// Cull everything outside the camera's PVS
				if(ent->IsWorld())
				{
					auto pos = ent->GetPosition();
					meshFilter = [bspTree,node,pos,curFilter](ModelMesh &mesh,const Vector3 &origin,const Quat &rot) -> bool {
						if(curFilter && curFilter(mesh,origin,rot) == false)
							return false;
						if(node == nullptr)
							return false;
						auto clusterIndex = mesh.GetReferenceId();
						if(clusterIndex == std::numeric_limits<uint32_t>::max())
						{
							// Probably not a world mesh
							return true;
						}
						return bspTree->IsClusterVisible(node->cluster,clusterIndex);
					};
				}
				else
				{
#if 0
					auto pos = ent->GetPosition();
					meshFilter = [bspTree,node,pos,&renderC,curFilter](ModelMesh &mesh,const Vector3 &origin,const Quat &rot) -> bool {
						if(curFilter && curFilter(mesh,origin,rot) == false)
							return false;
						if(node == nullptr)
							return false;
						Vector3 min;
						Vector3 max;
						renderC->GetRenderBounds(&min,&max);
						min += pos;
						max += pos;
						return Intersection::AABBAABB(min,max,node->minVisible,node->maxVisible);
					};
#endif
				}
			}
		}
		scene.AddEntity(*ent,nullptr,meshFilter);
	}

	// Particle Systems
#if 0
	EntityIterator entItPt {*c_game};
	entItPt.AttachFilter<TEntityIteratorFilterComponent<pragma::CParticleSystemComponent>>();
	entItPt.AttachFilter<EntityIteratorFilterUser>(entSceneFilter);
	for(auto *ent : entItPt)
	{
		auto ptc = ent->GetComponent<pragma::CParticleSystemComponent>();
		scene.AddParticleSystem(*ptc,camPos,vp,nearZ,farZ);
	}
#endif

	// 3D Sky
	EntityIterator entIt3dSky {*c_game};
	entIt3dSky.AttachFilter<TEntityIteratorFilterComponent<pragma::CSkyCameraComponent>>();
	for(auto *ent : entIt3dSky)
	{
		auto skyc = ent->GetComponent<pragma::CSkyCameraComponent>();
		scene.Add3DSkybox(*skyc,camPos);
	}
}

static void initialize_from_game_scene(
	lua_State *l,Scene &gameScene,cycles::Scene &scene,const Vector3 &camPos,const Quat &camRot,const Mat4 &vp,
	float nearZ,float farZ,float fov,SceneFlags sceneFlags,luabind::object *optEntFilter,luabind::object *optLightFilter
)
{
	std::function<bool(BaseEntity&)> entFilter = nullptr;
	Lua::CheckFunction(l,10);
	entFilter = [l,optEntFilter](BaseEntity &ent) -> bool {
		if(optEntFilter == nullptr)
			return true;
		auto r = Lua::CallFunction(l,[optEntFilter,&ent](lua_State *l) {
			optEntFilter->push(l);

			ent.GetLuaObject()->push(l);
			return Lua::StatusCode::Ok;
			},1);
		if(r == Lua::StatusCode::Ok)
		{
			if(Lua::IsSet(l,-1) == false)
				return false;
			return Lua::CheckBool(l,-1);
		}
		return false;
	};

	std::function<bool(BaseEntity&)> lightFilter = nullptr;
	Lua::CheckFunction(l,11);
	lightFilter = [l,optLightFilter](BaseEntity &ent) -> bool {
		if(optLightFilter == nullptr)
			return true;
		auto r = Lua::CallFunction(l,[optLightFilter,&ent](lua_State *l) {
			optLightFilter->push(l);

			ent.GetLuaObject()->push(l);
			return Lua::StatusCode::Ok;
			},1);
		if(r == Lua::StatusCode::Ok)
		{
			if(Lua::IsSet(l,-1) == false)
				return false;
			return Lua::CheckBool(l,-1);
		}
		return false;
	};
	auto aspectRatio = gameScene.GetWidth() /static_cast<float>(gameScene.GetHeight());
	initialize_cycles_scene_from_game_scene(gameScene,scene,camPos,camRot,vp,nearZ,farZ,fov,aspectRatio,sceneFlags,entFilter,lightFilter);
}

extern "C"
{
	PRAGMA_EXPORT void pr_cycles_render_image(
		uint32_t width,uint32_t height,uint32_t sampleCount,bool hdrOutput,bool denoise,
		const Vector3 &camPos,const Quat &camRot,const Mat4 &vp,float nearZ,float farZ,umath::Degree fov,
		SceneFlags sceneFlags,std::string skyOverride,EulerAngles skyAngles,float skyStrength,
		float maxTransparencyBounces,const std::function<bool(BaseEntity&)> &entFilter,util::ParallelJob<std::shared_ptr<uimg::ImageBuffer>> &outJob
	)
	{
		outJob = {};
		auto scene = setup_scene(raytracing::Scene::RenderMode::RenderImage,width,height,sampleCount,hdrOutput,denoise);
		if(scene == nullptr)
			return;
		auto aspectRatio = width /static_cast<float>(height);
		initialize_cycles_scene_from_game_scene(*c_game->GetScene(),*scene,camPos,camRot,vp,nearZ,farZ,fov,aspectRatio,sceneFlags,entFilter);
		if(skyOverride.empty() == false)
			(*scene)->SetSky(skyOverride);
		(*scene)->SetSkyAngles(skyAngles);
		(*scene)->SetSkyStrength(skyStrength);
		outJob = (*scene)->Finalize();
	}
	PRAGMA_EXPORT void pr_cycles_bake_ao(
		Model &mdl,uint32_t materialIndex,uint32_t width,uint32_t height,uint32_t sampleCount,bool hdrOutput,bool denoise,const std::string &deviceType,util::ParallelJob<std::shared_ptr<uimg::ImageBuffer>> &outJob
	)
	{
		outJob = {};
		auto eDeviceType = ustring::compare(deviceType,"gpu",false) ? raytracing::Scene::DeviceType::GPU : raytracing::Scene::DeviceType::CPU;
		auto scene = setup_scene(raytracing::Scene::RenderMode::BakeAmbientOcclusion,width,height,sampleCount,hdrOutput,denoise,eDeviceType);
		if(scene == nullptr)
			return;
		scene->SetAOBakeTarget(mdl,materialIndex);
		outJob = (*scene)->Finalize();
	}
	PRAGMA_EXPORT void pr_cycles_bake_lightmaps(
		BaseEntity &entTarget,uint32_t width,uint32_t height,uint32_t sampleCount,bool hdrOutput,bool denoise,
		std::string skyOverride,EulerAngles skyAngles,float skyStrength,
		util::ParallelJob<std::shared_ptr<uimg::ImageBuffer>> &outJob
	)
	{
		outJob = {};
		auto scene = setup_scene(raytracing::Scene::RenderMode::BakeDiffuseLighting,width,height,sampleCount,hdrOutput,denoise);
		if(scene == nullptr)
			return;
		auto &gameScene = *c_game->GetScene();
		setup_light_sources(*scene,[&gameScene](BaseEntity &ent) -> bool {
			return static_cast<CBaseEntity&>(ent).IsInScene(gameScene);
		});
		scene->SetLightmapBakeTarget(entTarget);
		if(skyOverride.empty() == false)
			(*scene)->SetSky(skyOverride);
		(*scene)->SetSkyAngles(skyAngles);
		(*scene)->SetSkyStrength(skyStrength);
		outJob = (*scene)->Finalize();
	}

	bool PRAGMA_EXPORT pragma_attach(std::string &errMsg)
	{
		raytracing::Scene::SetKernelPath(util::get_program_path() +"/modules/cycles");
		return true;
	}

	void PRAGMA_EXPORT pragma_initialize_lua(Lua::Interface &l)
	{
		auto &modCycles = l.RegisterLibrary("cycles",std::unordered_map<std::string,int32_t(*)(lua_State*)>{
			{"create_scene",static_cast<int32_t(*)(lua_State*)>([](lua_State *l) -> int32_t {
				auto renderMode = static_cast<raytracing::Scene::RenderMode>(Lua::CheckInt(l,1));
				auto &createInfo = Lua::Check<raytracing::Scene::CreateInfo>(l,2);
				auto scene = raytracing::Scene::Create(renderMode,createInfo);
				if(scene == nullptr)
					return 0;
#ifdef ENABLE_MOTION_BLUR_TEST
				scene->SetMotionBlurStrength(1.f);
#endif
				auto cyclesScene = std::make_shared<cycles::Scene>(*scene);
				Lua::Push(l,cyclesScene);
				return 1;
			})},
			{"bake_ambient_occlusion",static_cast<int32_t(*)(lua_State*)>([](lua_State *l) -> int32_t {
				auto &mdl = Lua::Check<Model>(l,1);
				auto materialIndex = Lua::CheckInt(l,2);

				uint32_t width = 512;
				uint32_t height = 512;
				uint32_t sampleCount = 20;
				auto deviceType = raytracing::Scene::DeviceType::CPU;
				if(Lua::IsSet(l,3))
					width = Lua::CheckInt(l,3);
				if(Lua::IsSet(l,4))
					height = Lua::CheckInt(l,4);
				if(Lua::IsSet(l,5))
					sampleCount = Lua::CheckInt(l,5);
				if(Lua::IsSet(l,6))
					deviceType = static_cast<raytracing::Scene::DeviceType>(Lua::CheckInt(l,6));
				auto hdrOutput = false;
				auto denoise = true;
				auto scene = setup_scene(raytracing::Scene::RenderMode::BakeAmbientOcclusion,width,height,sampleCount,hdrOutput,denoise,deviceType);
				if(scene == nullptr)
					return 0;
				scene->SetAOBakeTarget(mdl,materialIndex);
				auto job = (*scene)->Finalize();
				Lua::Push(l,job);
				return 1;
			})}
		});

		auto defCamera = luabind::class_<raytracing::Camera>("Camera");
		defCamera.add_static_constant("TYPE_PERSPECTIVE",umath::to_integral(raytracing::Camera::CameraType::Perspective));
		defCamera.add_static_constant("TYPE_ORTHOGRAPHIC",umath::to_integral(raytracing::Camera::CameraType::Orthographic));
		defCamera.add_static_constant("TYPE_PANORAMA",umath::to_integral(raytracing::Camera::CameraType::Panorama));

		defCamera.add_static_constant("PANORAMA_TYPE_EQUIRECTANGULAR",umath::to_integral(raytracing::Camera::PanoramaType::Equirectangular));
		defCamera.add_static_constant("PANORAMA_TYPE_FISHEYE_EQUIDISTANT",umath::to_integral(raytracing::Camera::PanoramaType::FisheyeEquidistant));
		defCamera.add_static_constant("PANORAMA_TYPE_FISHEYE_EQUISOLID",umath::to_integral(raytracing::Camera::PanoramaType::FisheyeEquisolid));
		defCamera.add_static_constant("PANORAMA_TYPE_MIRRORBALL",umath::to_integral(raytracing::Camera::PanoramaType::Mirrorball));
		defCamera.def("SetInterocularDistance",static_cast<void(*)(lua_State*,util::WeakHandle<raytracing::Camera>&,float)>([](lua_State *l,util::WeakHandle<raytracing::Camera> &cam,float interocularDistance) {
			pragma::Lua::check_component(l,cam);
			cam->SetInterocularDistance(interocularDistance);
		}));
		defCamera.def("SetEquirectangularHorizontalRange",static_cast<void(*)(lua_State*,util::WeakHandle<raytracing::Camera>&,float)>([](lua_State *l,util::WeakHandle<raytracing::Camera> &cam,float range) {
			pragma::Lua::check_component(l,cam);
			cam->SetEquirectangularHorizontalRange(range);
		}));
		defCamera.def("SetEquirectangularVerticalRange",static_cast<void(*)(lua_State*,util::WeakHandle<raytracing::Camera>&,float)>([](lua_State *l,util::WeakHandle<raytracing::Camera> &cam,float range) {
			pragma::Lua::check_component(l,cam);
			cam->SetEquirectangularVerticalRange(range);
		}));
		defCamera.def("SetStereoscopic",static_cast<void(*)(lua_State*,util::WeakHandle<raytracing::Camera>&,bool)>([](lua_State *l,util::WeakHandle<raytracing::Camera> &cam,bool stereoscopic) {
			pragma::Lua::check_component(l,cam);
			cam->SetStereoscopic(stereoscopic);
		}));
		defCamera.def("SetResolution",static_cast<void(*)(lua_State*,util::WeakHandle<raytracing::Camera>&,uint32_t,uint32_t)>([](lua_State *l,util::WeakHandle<raytracing::Camera> &cam,uint32_t width,uint32_t height) {
			pragma::Lua::check_component(l,cam);
			cam->SetResolution(width,height);
		}));
		defCamera.def("SetFarZ",static_cast<void(*)(lua_State*,util::WeakHandle<raytracing::Camera>&,float)>([](lua_State *l,util::WeakHandle<raytracing::Camera> &cam,float farZ) {
			pragma::Lua::check_component(l,cam);
			cam->SetFarZ(farZ);
		}));
		defCamera.def("SetNearZ",static_cast<void(*)(lua_State*,util::WeakHandle<raytracing::Camera>&,float)>([](lua_State *l,util::WeakHandle<raytracing::Camera> &cam,float nearZ) {
			pragma::Lua::check_component(l,cam);
			cam->SetNearZ(nearZ);
		}));
		defCamera.def("SetFOV",static_cast<void(*)(lua_State*,util::WeakHandle<raytracing::Camera>&,float)>([](lua_State *l,util::WeakHandle<raytracing::Camera> &cam,float fov) {
			pragma::Lua::check_component(l,cam);
			cam->SetFOV(umath::deg_to_rad(fov));
		}));
		defCamera.def("SetCameraType",static_cast<void(*)(lua_State*,util::WeakHandle<raytracing::Camera>&,uint32_t)>([](lua_State *l,util::WeakHandle<raytracing::Camera> &cam,uint32_t camType) {
			pragma::Lua::check_component(l,cam);
			cam->SetCameraType(static_cast<raytracing::Camera::CameraType>(camType));
		}));
		defCamera.def("SetPanoramaType",static_cast<void(*)(lua_State*,util::WeakHandle<raytracing::Camera>&,uint32_t)>([](lua_State *l,util::WeakHandle<raytracing::Camera> &cam,uint32_t panoramaType) {
			pragma::Lua::check_component(l,cam);
			cam->SetPanoramaType(static_cast<raytracing::Camera::PanoramaType>(panoramaType));
		}));
		defCamera.def("SetFocalDistance",static_cast<void(*)(lua_State*,util::WeakHandle<raytracing::Camera>&,float)>([](lua_State *l,util::WeakHandle<raytracing::Camera> &cam,float focalDistance) {
			pragma::Lua::check_component(l,cam);
			cam->SetFocalDistance(focalDistance);
		}));
		defCamera.def("SetApertureSize",static_cast<void(*)(lua_State*,util::WeakHandle<raytracing::Camera>&,float)>([](lua_State *l,util::WeakHandle<raytracing::Camera> &cam,float size) {
			pragma::Lua::check_component(l,cam);
			cam->SetApertureSize(size);
		}));
		defCamera.def("SetApertureSizeFromFStop",static_cast<void(*)(lua_State*,util::WeakHandle<raytracing::Camera>&,float,umath::Millimeter)>([](lua_State *l,util::WeakHandle<raytracing::Camera> &cam,float fstop,umath::Millimeter focalLength) {
			pragma::Lua::check_component(l,cam);
			cam->SetApertureSizeFromFStop(fstop,focalLength);
		}));
		defCamera.def("SetFOVFromFocalLength",static_cast<void(*)(lua_State*,util::WeakHandle<raytracing::Camera>&,umath::Millimeter,umath::Millimeter)>([](lua_State *l,util::WeakHandle<raytracing::Camera> &cam,umath::Millimeter focalLength,umath::Millimeter sensorSize) {
			pragma::Lua::check_component(l,cam);
			cam->SetFOVFromFocalLength(focalLength,sensorSize);
		}));
		defCamera.def("SetBokehRatio",static_cast<void(*)(lua_State*,util::WeakHandle<raytracing::Camera>&,float)>([](lua_State *l,util::WeakHandle<raytracing::Camera> &cam,float ratio) {
			pragma::Lua::check_component(l,cam);
			cam->SetBokehRatio(ratio);
		}));
		defCamera.def("SetBladeCount",static_cast<void(*)(lua_State*,util::WeakHandle<raytracing::Camera>&,uint32_t)>([](lua_State *l,util::WeakHandle<raytracing::Camera> &cam,uint32_t numBlades) {
			pragma::Lua::check_component(l,cam);
			cam->SetBladeCount(numBlades);
		}));
		defCamera.def("SetBladesRotation",static_cast<void(*)(lua_State*,util::WeakHandle<raytracing::Camera>&,float)>([](lua_State *l,util::WeakHandle<raytracing::Camera> &cam,float rotation) {
			pragma::Lua::check_component(l,cam);
			cam->SetBladesRotation(rotation);
		}));
		defCamera.def("SetDepthOfFieldEnabled",static_cast<void(*)(lua_State*,util::WeakHandle<raytracing::Camera>&,bool)>([](lua_State *l,util::WeakHandle<raytracing::Camera> &cam,bool enabled) {
			pragma::Lua::check_component(l,cam);
			cam->SetDepthOfFieldEnabled(enabled);
		}));
		modCycles[defCamera];

		auto defScene = luabind::class_<cycles::Scene>("Scene");

		auto defSerializationData = luabind::class_<raytracing::Scene::SerializationData>("SerializationData");
		defSerializationData.def(luabind::constructor<>());
		defSerializationData.def_readwrite("outputFileName",&raytracing::Scene::SerializationData::outputFileName);
		defScene.scope[defSerializationData];

		defScene.add_static_constant("RENDER_MODE_COMBINED",umath::to_integral(raytracing::Scene::RenderMode::RenderImage));
		defScene.add_static_constant("RENDER_MODE_BAKE_AMBIENT_OCCLUSION",umath::to_integral(raytracing::Scene::RenderMode::BakeAmbientOcclusion));
		defScene.add_static_constant("RENDER_MODE_BAKE_NORMALS",umath::to_integral(raytracing::Scene::RenderMode::BakeNormals));
		defScene.add_static_constant("RENDER_MODE_BAKE_DIFFUSE_LIGHTING",umath::to_integral(raytracing::Scene::RenderMode::BakeDiffuseLighting));
		defScene.add_static_constant("RENDER_MODE_ALBEDO",umath::to_integral(raytracing::Scene::RenderMode::SceneAlbedo));
		defScene.add_static_constant("RENDER_MODE_NORMALS",umath::to_integral(raytracing::Scene::RenderMode::SceneNormals));
		defScene.add_static_constant("RENDER_MODE_DEPTH",umath::to_integral(raytracing::Scene::RenderMode::SceneDepth));

		defScene.add_static_constant("DEVICE_TYPE_CPU",umath::to_integral(raytracing::Scene::DeviceType::CPU));
		defScene.add_static_constant("DEVICE_TYPE_GPU",umath::to_integral(raytracing::Scene::DeviceType::GPU));

		defScene.add_static_constant("SCENE_FLAG_NONE",umath::to_integral(SceneFlags::None));
		defScene.add_static_constant("SCENE_FLAG_BIT_CULL_OBJECTS_OUTSIDE_CAMERA_FRUSTUM",umath::to_integral(SceneFlags::CullObjectsOutsideCameraFrustum));
		defScene.add_static_constant("SCENE_FLAG_BIT_CULL_OBJECTS_OUTSIDE_PVS",umath::to_integral(SceneFlags::CullObjectsOutsidePvs));
		defScene.def("InitializeFromGameScene",static_cast<void(*)(lua_State*,cycles::Scene&,Scene&,const Vector3&,const Quat&,const Mat4&,float,float,float,uint32_t,luabind::object,luabind::object)>([](lua_State *l,cycles::Scene &scene,Scene &gameScene,const Vector3 &camPos,const Quat &camRot,const Mat4 &vp,float nearZ,float farZ,float fov,uint32_t sceneFlags,luabind::object entFilter,luabind::object lightFilter) {
			initialize_from_game_scene(l,gameScene,scene,camPos,camRot,vp,nearZ,farZ,fov,static_cast<SceneFlags>(sceneFlags),&entFilter,&lightFilter);
		}));
		defScene.def("InitializeFromGameScene",static_cast<void(*)(lua_State*,cycles::Scene&,Scene&,const Vector3&,const Quat&,const Mat4&,float,float,float,uint32_t,luabind::object)>([](lua_State *l,cycles::Scene &scene,Scene &gameScene,const Vector3 &camPos,const Quat &camRot,const Mat4 &vp,float nearZ,float farZ,float fov,uint32_t sceneFlags,luabind::object entFilter) {
			initialize_from_game_scene(l,gameScene,scene,camPos,camRot,vp,nearZ,farZ,fov,static_cast<SceneFlags>(sceneFlags),&entFilter,nullptr);
		}));
		defScene.def("InitializeFromGameScene",static_cast<void(*)(lua_State*,cycles::Scene&,Scene&,const Vector3&,const Quat&,const Mat4&,float,float,float,uint32_t)>([](lua_State *l,cycles::Scene &scene,Scene &gameScene,const Vector3 &camPos,const Quat &camRot,const Mat4 &vp,float nearZ,float farZ,float fov,uint32_t sceneFlags) {
			initialize_from_game_scene(l,gameScene,scene,camPos,camRot,vp,nearZ,farZ,fov,static_cast<SceneFlags>(sceneFlags),nullptr,nullptr);
		}));
		defScene.def("SetSky",static_cast<void(*)(lua_State*,cycles::Scene&,const std::string&)>([](lua_State *l,cycles::Scene &scene,const std::string &skyPath) {
			scene->SetSky(skyPath);
		}));
		defScene.def("SetSkyAngles",static_cast<void(*)(lua_State*,cycles::Scene&,const EulerAngles&)>([](lua_State *l,cycles::Scene &scene,const EulerAngles &skyAngles) {
			scene->SetSkyAngles(skyAngles);
		}));
		defScene.def("SetSkyStrength",static_cast<void(*)(lua_State*,cycles::Scene&,float)>([](lua_State *l,cycles::Scene &scene,float skyStrength) {
			scene->SetSkyStrength(skyStrength);
		}));
		defScene.def("SetEmissionStrength",static_cast<void(*)(lua_State*,cycles::Scene&,float)>([](lua_State *l,cycles::Scene &scene,float emissionStrength) {
			scene->SetEmissionStrength(emissionStrength);
		}));
		defScene.def("SetMaxTransparencyBounces",static_cast<void(*)(lua_State*,cycles::Scene&,uint32_t)>([](lua_State *l,cycles::Scene &scene,uint32_t bounces) {
			scene->SetMaxTransparencyBounces(bounces);
		}));
		defScene.def("SetMaxBounces",static_cast<void(*)(lua_State*,cycles::Scene&,uint32_t)>([](lua_State *l,cycles::Scene &scene,uint32_t bounces) {
			scene->SetMaxBounces(bounces);
		}));
		defScene.def("SetMaxDiffuseBounces",static_cast<void(*)(lua_State*,cycles::Scene&,uint32_t)>([](lua_State *l,cycles::Scene &scene,uint32_t bounces) {
			scene->SetMaxDiffuseBounces(bounces);
		}));
		defScene.def("SetMaxGlossyBounces",static_cast<void(*)(lua_State*,cycles::Scene&,uint32_t)>([](lua_State *l,cycles::Scene &scene,uint32_t bounces) {
			scene->SetMaxGlossyBounces(bounces);
		}));
		defScene.def("SetMaxTransmissionBounces",static_cast<void(*)(lua_State*,cycles::Scene&,uint32_t)>([](lua_State *l,cycles::Scene &scene,uint32_t bounces) {
			scene->SetMaxTransmissionBounces(bounces);
		}));
		defScene.def("SetLightIntensityFactor",static_cast<void(*)(lua_State*,cycles::Scene&,float)>([](lua_State *l,cycles::Scene &scene,float factor) {
			scene->SetLightIntensityFactor(factor);
		}));
		defScene.def("CreateRenderJob",static_cast<void(*)(lua_State*,cycles::Scene&)>([](lua_State *l,cycles::Scene &scene) {
			auto job = scene->Finalize();
			if(job.IsValid() == false)
				return;
			Lua::Push(l,job);
		}));
		defScene.def("SetResolution",static_cast<void(*)(lua_State*,cycles::Scene&,uint32_t,uint32_t)>([](lua_State *l,cycles::Scene &scene,uint32_t width,uint32_t height) {
			scene->GetCamera().SetResolution(width,height);
		}));
		defScene.def("GetCamera",static_cast<void(*)(lua_State*,cycles::Scene&)>([](lua_State *l,cycles::Scene &scene) {
			Lua::Push(l,scene->GetCamera().GetHandle());
		}));
		defScene.def("GetLightSources",static_cast<void(*)(lua_State*,cycles::Scene&)>([](lua_State *l,cycles::Scene &scene) {
			auto t = Lua::CreateTable(l);
			auto &lights = scene->GetLights();
			uint32_t idx = 1;
			for(auto &light : lights)
			{
				Lua::PushInt(l,idx++);
				Lua::Push(l,light.get());
				Lua::SetTableValue(l,t);
			}
		}));
		defScene.def("AddLightSource",static_cast<void(*)(lua_State*,cycles::Scene&,uint32_t,const Vector3&)>([](lua_State *l,cycles::Scene &scene,uint32_t type,const Vector3 &pos) {
			auto light = raytracing::Light::Create(*scene);
			if(light == nullptr)
				return;
			light->SetType(static_cast<raytracing::Light::Type>(type));
			light->SetPos(pos);
			Lua::Push(l,light.get());
		}));
		defScene.def("Serialize",static_cast<void(*)(lua_State*,cycles::Scene&,DataStream&,const raytracing::Scene::SerializationData&)>([](lua_State *l,cycles::Scene &scene,DataStream &ds,const raytracing::Scene::SerializationData &serializationData) {
			scene->Serialize(ds,serializationData);
		}));
		defScene.def("Deserialize",static_cast<void(*)(lua_State*,cycles::Scene&,DataStream&)>([](lua_State *l,cycles::Scene &scene,DataStream &ds) {
			scene->Deserialize(ds);
		}));

		auto defSceneCreateInfo = luabind::class_<raytracing::Scene::CreateInfo>("CreateInfo");
		defSceneCreateInfo.def(luabind::constructor<>());
		defSceneCreateInfo.def_readwrite("hdrOutput",&raytracing::Scene::CreateInfo::hdrOutput);
		defSceneCreateInfo.def_readwrite("denoise",&raytracing::Scene::CreateInfo::denoise);
		defSceneCreateInfo.def_readwrite("deviceType",reinterpret_cast<uint32_t raytracing::Scene::CreateInfo::*>(&raytracing::Scene::CreateInfo::deviceType));
		defSceneCreateInfo.def("SetSamplesPerPixel",static_cast<void(*)(lua_State*,raytracing::Scene::CreateInfo&,uint32_t)>([](lua_State *l,raytracing::Scene::CreateInfo &createInfo,uint32_t samples) {
			createInfo.samples = samples;
		}));
		defScene.scope[defSceneCreateInfo];

		auto defSceneObject = luabind::class_<raytracing::SceneObject>("SceneObject");
		defSceneObject.def("GetScene",static_cast<void(*)(lua_State*,raytracing::SceneObject&)>([](lua_State *l,raytracing::SceneObject &sceneObject) {
			Lua::Push(l,sceneObject.GetScene().shared_from_this());
			}));
		modCycles[defSceneObject];

		auto defWorldObject = luabind::class_<raytracing::WorldObject,raytracing::SceneObject>("WorldObject");
		defWorldObject.def("SetPos",static_cast<void(*)(lua_State*,raytracing::WorldObject&,const Vector3&)>([](lua_State *l,raytracing::WorldObject &worldObject,const Vector3 &pos) {
			worldObject.SetPos(pos);
			}));
		defWorldObject.def("GetPos",static_cast<void(*)(lua_State*,raytracing::WorldObject&)>([](lua_State *l,raytracing::WorldObject &worldObject) {
			Lua::Push<Vector3>(l,worldObject.GetPos());
			}));
		defWorldObject.def("SetRotation",static_cast<void(*)(lua_State*,raytracing::WorldObject&,const Quat&)>([](lua_State *l,raytracing::WorldObject &worldObject,const Quat &rot) {
			worldObject.SetRotation(rot);
			}));
		defWorldObject.def("GetRotation",static_cast<void(*)(lua_State*,raytracing::WorldObject&)>([](lua_State *l,raytracing::WorldObject &worldObject) {
			Lua::Push<Quat>(l,worldObject.GetRotation());
			}));
		defWorldObject.def("GetPose",static_cast<void(*)(lua_State*,raytracing::WorldObject&)>([](lua_State *l,raytracing::WorldObject &worldObject) {
			Lua::Push<umath::Transform>(l,worldObject.GetPose());
			}));
		modCycles[defWorldObject];

		auto defLight = luabind::class_<raytracing::Light,luabind::bases<raytracing::WorldObject,raytracing::SceneObject>>("LightSource");
		defLight.add_static_constant("TYPE_POINT",umath::to_integral(raytracing::Light::Type::Point));
		defLight.add_static_constant("TYPE_SPOT",umath::to_integral(raytracing::Light::Type::Spot));
		defLight.add_static_constant("TYPE_DIRECTIONAL",umath::to_integral(raytracing::Light::Type::Directional));
		defLight.add_static_constant("TYPE_AREA",umath::to_integral(raytracing::Light::Type::Area));
		defLight.add_static_constant("TYPE_BACKGROUND",umath::to_integral(raytracing::Light::Type::Background));
		defLight.add_static_constant("TYPE_TRIANGLE",umath::to_integral(raytracing::Light::Type::Triangle));
		defLight.def("SetType",static_cast<void(*)(lua_State*,raytracing::Light&,uint32_t)>([](lua_State *l,raytracing::Light &light,uint32_t type) {
			light.SetType(static_cast<raytracing::Light::Type>(type));
			}));
		defLight.def("SetConeAngles",static_cast<void(*)(lua_State*,raytracing::Light&,float,float)>([](lua_State *l,raytracing::Light &light,float innerAngle,float outerAngle) {
			light.SetConeAngles(umath::deg_to_rad(innerAngle),umath::deg_to_rad(outerAngle));
			}));
		defLight.def("SetColor",static_cast<void(*)(lua_State*,raytracing::Light&,const Color&)>([](lua_State *l,raytracing::Light &light,const Color &color) {
			light.SetColor(color);
			}));
		defLight.def("SetIntensity",static_cast<void(*)(lua_State*,raytracing::Light&,float)>([](lua_State *l,raytracing::Light &light,float intensity) {
			light.SetIntensity(intensity);
			}));
		defLight.def("SetSize",static_cast<void(*)(lua_State*,raytracing::Light&,float)>([](lua_State *l,raytracing::Light &light,float size) {
			light.SetSize(size);
			}));
		defLight.def("SetAxisU",static_cast<void(*)(lua_State*,raytracing::Light&,const Vector3&)>([](lua_State *l,raytracing::Light &light,const Vector3 &axisU) {
			light.SetAxisU(axisU);
			}));
		defLight.def("SetAxisV",static_cast<void(*)(lua_State*,raytracing::Light&,const Vector3&)>([](lua_State *l,raytracing::Light &light,const Vector3 &axisV) {
			light.SetAxisV(axisV);
			}));
		defLight.def("SetSizeU",static_cast<void(*)(lua_State*,raytracing::Light&,float)>([](lua_State *l,raytracing::Light &light,float sizeU) {
			light.SetSizeU(sizeU);
			}));
		defLight.def("SetSizeV",static_cast<void(*)(lua_State*,raytracing::Light&,float)>([](lua_State *l,raytracing::Light &light,float sizeV) {
			light.SetSizeV(sizeV);
			}));

		modCycles[defLight];

		modCycles[defScene];
#if 0
		auto &modConvert = l.RegisterLibrary("cycles",std::unordered_map<std::string,int32_t(*)(lua_State*)>{
			{"render_image",static_cast<int32_t(*)(lua_State*)>([](lua_State *l) -> int32_t {

				pr_cycles_render_image(width,height,sampleCount,hdrOutput,denoise,camPos,camRot,nearZ,farZ,fov,entFilter,outputHandler,outScene);

				auto scene = create_cycles_scene_from_game_scene([](BaseEntity &ent) -> bool {
					return true;
				},[](const uint8_t *data,int width,int height,int channels) {
				
				});
				if(scene == nullptr)
					return 0;
				Lua::Push<raytracing::PScene>(l,scene);
				return 1;
			})},
			{"bake_ambient_occlusion",static_cast<int32_t(*)(lua_State*)>([](lua_State *l) -> int32_t {

			})},
			{"bake_lightmaps",static_cast<int32_t(*)(lua_State*)>([](lua_State *l) -> int32_t {

			})},
			{"create_scene",static_cast<int32_t(*)(lua_State*)>([](lua_State *l) -> int32_t {
				uint32_t sampleCount = 1'024;
				auto hdrOutput = false;
				auto denoise = true;
				int32_t argIdx = 1;
				if(Lua::IsSet(l,argIdx))
					sampleCount = Lua::CheckInt(l,argIdx);
				++argIdx;
				if(Lua::IsSet(l,argIdx))
					hdrOutput = Lua::CheckBool(l,argIdx);
				++argIdx;
				if(Lua::IsSet(l,argIdx))
					denoise = Lua::CheckBool(l,argIdx);
				// TODO
				auto scene = cycles::Scene::Create(pragma::modules::cycles::Scene::RenderMode::BakeDiffuseLighting,[](const uint8_t *data,int width,int height,int channels) {
					
				},sampleCount,hdrOutput,denoise);
				if(scene == nullptr)
					return 0;
				Lua::Push<cycles::PScene>(l,scene);
				return 1;
			})}
		});
		//

		auto defCamera = luabind::class_<cycles::Camera,luabind::bases<cycles::WorldObject,cycles::SceneObject>>("Camera");
		defCamera.def("SetResolution",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Camera>&,uint32_t,uint32_t)>([](lua_State *l,util::WeakHandle<cycles::Camera> &cam,uint32_t width,uint32_t height) {
			pragma::Lua::check_component(l,cam);
			cam->SetResolution(width,height);
		}));
		defCamera.def("SetFarZ",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Camera>&,float)>([](lua_State *l,util::WeakHandle<cycles::Camera> &cam,float farZ) {
			pragma::Lua::check_component(l,cam);
			cam->SetFarZ(farZ);
		}));
		defCamera.def("SetNearZ",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Camera>&,float)>([](lua_State *l,util::WeakHandle<cycles::Camera> &cam,float nearZ) {
			pragma::Lua::check_component(l,cam);
			cam->SetNearZ(nearZ);
		}));
		defCamera.def("SetFOV",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Camera>&,float)>([](lua_State *l,util::WeakHandle<cycles::Camera> &cam,float fov) {
			pragma::Lua::check_component(l,cam);
			cam->SetFOV(umath::deg_to_rad(fov));
		}));
		modConvert[defCamera];

		auto defMesh = luabind::class_<cycles::Mesh,cycles::SceneObject>("Mesh");
		defMesh.def("AddVertex",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Mesh>&,const Vector3&,const Vector3&,const Vector2&)>([](lua_State *l,util::WeakHandle<cycles::Mesh> &mesh,const Vector3 &pos,const Vector3 &n,const Vector2 &uv) {
			pragma::Lua::check_component(l,mesh);
			Lua::PushBool(l,mesh->AddVertex(pos,n,Vector3{},uv));
		}));
		defMesh.def("AddTriangle",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Mesh>&,uint32_t,uint32_t,uint32_t,uint32_t)>([](lua_State *l,util::WeakHandle<cycles::Mesh> &mesh,uint32_t idx0,uint32_t idx1,uint32_t idx2,uint32_t shaderIndex) {
			pragma::Lua::check_component(l,mesh);
			Lua::PushBool(l,mesh->AddTriangle(idx0,idx1,idx2,shaderIndex));
		}));
		defMesh.def("AddShader",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Mesh>&,util::WeakHandle<cycles::Shader>&)>([](lua_State *l,util::WeakHandle<cycles::Mesh> &mesh,util::WeakHandle<cycles::Shader> &shader) {
			pragma::Lua::check_component(l,mesh);
			pragma::Lua::check_component(l,shader);
			Lua::PushInt(l,mesh->AddShader(*shader));
		}));
		modConvert[defMesh];

		auto defObject = luabind::class_<cycles::Object,luabind::bases<cycles::WorldObject,cycles::SceneObject>>("Object");
		modConvert[defObject];

		auto defScene = luabind::class_<cycles::Scene>("Scene");
		defMesh.def("AddEntity",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Scene>&,EntityHandle&)>([](lua_State *l,util::WeakHandle<cycles::Scene> &scene,EntityHandle &hEnt) {
			LUA_CHECK_ENTITY(l,hEnt);
			pragma::Lua::check_component(l,scene);
			scene->AddEntity(*hEnt.get());
		}));
		defMesh.def("AddLight",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Scene>&)>([](lua_State *l,util::WeakHandle<cycles::Scene> &scene) {
			pragma::Lua::check_component(l,scene);
			auto light = cycles::Light::Create(*scene);
			Lua::Push(l,light->GetHandle());
		}));
		defMesh.def("AddMesh",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Scene>&,const std::string&,uint32_t,uint32_t)>([](lua_State *l,util::WeakHandle<cycles::Scene> &scene,const std::string &name,uint32_t numVerts,uint32_t numTris) {
			pragma::Lua::check_component(l,scene);
			auto mesh = cycles::Mesh::Create(*scene,name,numVerts,numTris);
			Lua::Push(l,mesh->GetHandle());
		}));
		defMesh.def("AddObject",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Scene>&,util::WeakHandle<cycles::Mesh>&)>([](lua_State *l,util::WeakHandle<cycles::Scene> &scene,util::WeakHandle<cycles::Mesh> &mesh) {
			pragma::Lua::check_component(l,scene);
			pragma::Lua::check_component(l,mesh);
			auto object = cycles::Object::Create(*scene,*mesh);
			Lua::Push(l,mesh->GetHandle());
		}));
		defMesh.def("AddShader",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Scene>&,const std::string&)>([](lua_State *l,util::WeakHandle<cycles::Scene> &scene,const std::string &name) {
			pragma::Lua::check_component(l,scene);
			auto shader = cycles::Shader::Create(*scene,name);
			Lua::Push(l,shader->GetHandle());
		}));
		defMesh.def("GetShaders",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Scene>&)>([](lua_State *l,util::WeakHandle<cycles::Scene> &scene) {
			pragma::Lua::check_component(l,scene);
			auto &shaders = scene->GetShaders();
			auto t = Lua::CreateTable(l);
			for(auto i=decltype(shaders.size()){0u};i<shaders.size();++i)
			{
				auto &shader = shaders.at(i);
				Lua::PushInt(l,i +1);
				Lua::Push(l,shader->GetHandle());
				Lua::SetTableValue(l,t);
			}
		}));
		defMesh.def("GetObjects",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Scene>&)>([](lua_State *l,util::WeakHandle<cycles::Scene> &scene) {
			pragma::Lua::check_component(l,scene);
			auto &objects = scene->GetObjects();
			auto t = Lua::CreateTable(l);
			for(auto i=decltype(objects.size()){0u};i<objects.size();++i)
			{
				auto &o = objects.at(i);
				Lua::PushInt(l,i +1);
				Lua::Push(l,o->GetHandle());
				Lua::SetTableValue(l,t);
			}
		}));
		defMesh.def("GetLights",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Scene>&)>([](lua_State *l,util::WeakHandle<cycles::Scene> &scene) {
			pragma::Lua::check_component(l,scene);
			auto &lights = scene->GetLights();
			auto t = Lua::CreateTable(l);
			for(auto i=decltype(lights.size()){0u};i<lights.size();++i)
			{
				auto &light = lights.at(i);
				Lua::PushInt(l,i +1);
				Lua::Push(l,light->GetHandle());
				Lua::SetTableValue(l,t);
			}
		}));
		defMesh.def("GetProgress",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Scene>&)>([](lua_State *l,util::WeakHandle<cycles::Scene> &scene) {
			pragma::Lua::check_component(l,scene);
			Lua::PushNumber(l,scene->GetProgress());
		}));
		defMesh.def("IsComplete",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Scene>&)>([](lua_State *l,util::WeakHandle<cycles::Scene> &scene) {
			pragma::Lua::check_component(l,scene);
			Lua::PushBool(l,scene->IsComplete());
		}));
		defMesh.def("IsCancelled",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Scene>&)>([](lua_State *l,util::WeakHandle<cycles::Scene> &scene) {
			pragma::Lua::check_component(l,scene);
			Lua::PushBool(l,scene->IsCancelled());
		}));
		defMesh.def("Start",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Scene>&)>([](lua_State *l,util::WeakHandle<cycles::Scene> &scene) {
			pragma::Lua::check_component(l,scene);
			scene->Start();
		}));
		defMesh.def("Cancel",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Scene>&)>([](lua_State *l,util::WeakHandle<cycles::Scene> &scene) {
			pragma::Lua::check_component(l,scene);
			scene->Cancel();
		}));
		defMesh.def("Wait",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Scene>&)>([](lua_State *l,util::WeakHandle<cycles::Scene> &scene) {
			pragma::Lua::check_component(l,scene);
			scene->Wait();
		}));
		defMesh.def("GetCamera",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Scene>&)>([](lua_State *l,util::WeakHandle<cycles::Scene> &scene) {
			pragma::Lua::check_component(l,scene);
			Lua::Push(l,scene->GetCamera().GetHandle());
		}));
		//defMesh.def("SetProgressCallback",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Scene&)>([](lua_State *l,util::WeakHandle<cycles::Scene> &scene) {
			// pragma::Lua::check_component(l,scene);
			//	scene.SetProgressCallback(); // TODO
		//}));
		modConvert[defScene];

		auto defDenoiseInfo = luabind::class_<cycles::Scene::DenoiseInfo>("DenoiseInfo");
		defDenoiseInfo.def_readwrite("numThreads",&cycles::Scene::DenoiseInfo::numThreads);
		defDenoiseInfo.def_readwrite("width",&cycles::Scene::DenoiseInfo::width);
		defDenoiseInfo.def_readwrite("height",&cycles::Scene::DenoiseInfo::height);
		defDenoiseInfo.def_readwrite("hdr",&cycles::Scene::DenoiseInfo::hdr);
		defScene.scope[defDenoiseInfo];

		auto defShaderNode = luabind::class_<cycles::ShaderNode>("ShaderNode");
		modConvert[defShaderNode];

		auto defShader = luabind::class_<cycles::Shader,cycles::SceneObject>("Shader");
		defShader.def("AddNode",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Shader>&,const std::string&,const std::string&)>([](lua_State *l,util::WeakHandle<cycles::Shader> &shader,const std::string &type,const std::string &name) {
			pragma::Lua::check_component(l,shader);
			auto node = shader->AddNode(type,name);
			Lua::Push(l,node->GetHandle());
		}));
		defShader.def("FindNode",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Shader>&,const std::string&)>([](lua_State *l,util::WeakHandle<cycles::Shader> &shader,const std::string &name) {
			pragma::Lua::check_component(l,shader);
			auto node = shader->FindNode(name);
			Lua::Push(l,node->GetHandle());
		}));
		defShader.def("Link",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Shader>&,const std::string&,const std::string&,const std::string&,const std::string&)>([](lua_State *l,util::WeakHandle<cycles::Shader> &shader,const std::string &fromNodeName,const std::string &fromSocketName,const std::string &toNodeName,const std::string &toSocketName) {
			pragma::Lua::check_component(l,shader);
			Lua::PushBool(l,shader->Link(fromNodeName,fromSocketName,toNodeName,toSocketName));
		}));
		modConvert[defShader];
#endif
	}
};
#pragma optimize("",on)
