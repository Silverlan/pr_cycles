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

#include <pragma/lua/luaapi.h>
#include <prosper_context.hpp>
#include <pragma/c_engine.h>
#include <pragma/game/c_game.h>
#include <pragma/entities/baseentity.h>
#include <pragma/model/model.h>
#include <pragma/model/modelmesh.h>
#include <pragma/entities/components/c_player_component.hpp>
#include <pragma/entities/components/c_color_component.hpp>
#include <pragma/entities/components/c_model_component.hpp>
#include <pragma/entities/components/c_render_component.hpp>
#include <pragma/entities/components/c_toggle_component.hpp>
#include <pragma/entities/environment/c_env_camera.h>
#include <pragma/entities/entity_iterator.hpp>
#include <pragma/entities/environment/lights/c_env_light.h>
#include <pragma/entities/environment/lights/c_env_light_spot.h>
#include <pragma/entities/environment/lights/c_env_light_point.h>
#include <pragma/entities/environment/lights/c_env_light_directional.h>
#include <pragma/entities/entity_component_system_t.hpp>
#include <pragma/util/util_game.hpp>
#include <pragma/lua/classes/ldef_entity.h>
#include <luainterface.hpp>
#include <pragma/lua/lua_entity_component.hpp>

#include "pr_cycles/scene.hpp"
#include "pr_cycles/shader.hpp"
#include "pr_cycles/camera.hpp"
#include "pr_cycles/light.hpp"
#include "pr_cycles/mesh.hpp"
#include "pr_cycles/object.hpp"

#pragma optimize("",off)

extern DLLCLIENT CGame *c_game;

using namespace pragma::modules;

static cycles::PShader setup_skybox(cycles::Scene &scene)
{
	auto shaderSkybox = cycles::Shader::Create(scene,*scene->default_background);
	shaderSkybox->AddNode("sky_texture","tex");
	auto nodeBgShader = shaderSkybox->AddNode("background_shader","bg");
	nodeBgShader->SetInputArgument<float>("strength",8.f);
	shaderSkybox->Link("bg","background","output","surface");
	shaderSkybox->Link("tex","color","bg","color");
	return shaderSkybox;
}

static void setup_light_sources(cycles::Scene &scene)
{
	EntityIterator entIt {*c_game};
	entIt.AttachFilter<TEntityIteratorFilterComponent<pragma::CLightComponent>>();
	for(auto *ent : entIt)
	{
		auto toggleC = ent->GetComponent<pragma::CToggleComponent>();
		if(toggleC.valid() && toggleC->IsTurnedOn() == false)
			continue;
		auto colorC = ent->GetComponent<pragma::CColorComponent>();
		auto color = Color::White;
		if(colorC.valid())
			color = colorC->GetColor();
		auto hLightSpot = ent->GetComponent<pragma::CLightSpotComponent>();
		if(hLightSpot.valid())
		{
			auto light = cycles::Light::Create(scene);
			if(light)
			{
				light->SetType(cycles::Light::Type::Spot);
				light->SetPos(ent->GetPosition());
				light->SetRotation(ent->GetRotation());
				light->SetConeAngle(umath::deg_to_rad(hLightSpot->GetOuterCutoffAngle()) *2.f);
				light->SetColor(color);
			}
			continue;
		}
		auto hLightPoint = ent->GetComponent<pragma::CLightPointComponent>();
		if(hLightPoint.valid())
		{
			auto light = cycles::Light::Create(scene);
			if(light)
			{
				light->SetType(cycles::Light::Type::Point);
				light->SetPos(ent->GetPosition());
				light->SetRotation(ent->GetRotation());
				light->SetColor(color);
			}
			continue;
		}
		auto hLightDirectional = ent->GetComponent<pragma::CLightDirectionalComponent>();
		if(hLightDirectional.valid())
		{
			auto light = cycles::Light::Create(scene);
			if(light)
			{
				light->SetType(cycles::Light::Type::Directional);
				light->SetPos(ent->GetPosition());
				light->SetRotation(ent->GetRotation());
				light->SetColor(color);
			}
		}
	}
}

static util::ParallelJob<std::shared_ptr<util::ImageBuffer>> setup_scene(cycles::Scene::RenderMode renderMode,uint32_t width,uint32_t height,uint32_t sampleCount,bool hdrOutput,bool denoise)
{
	auto job = cycles::Scene::Create(renderMode,sampleCount,hdrOutput,denoise);
	if(job.IsValid() == false)
		return {};
	auto &scene = static_cast<cycles::Scene&>(job.GetWorker());
	auto &cam = scene.GetCamera();
	cam.SetResolution(width,height);
	return job;
}

extern "C"
{
	PRAGMA_EXPORT void pr_cycles_render_image(
		uint32_t width,uint32_t height,uint32_t sampleCount,bool hdrOutput,bool denoise,
		const Vector3 &camPos,const Quat &camRot,float nearZ,float farZ,umath::Degree fov,
		const std::function<bool(BaseEntity&)> &entFilter,util::ParallelJob<std::shared_ptr<util::ImageBuffer>> &outJob
	)
	{
		outJob = {};
		auto job = setup_scene(cycles::Scene::RenderMode::RenderImage,width,height,sampleCount,hdrOutput,denoise);
		if(job.IsValid() == false)
			return;
		auto &scene = static_cast<cycles::Scene&>(job.GetWorker());
		setup_skybox(scene);
		setup_light_sources(scene);
		auto &cam = scene.GetCamera();
		cam.SetPos(camPos);
		cam.SetRotation(camRot);
		cam.SetNearZ(nearZ);
		cam.SetFarZ(farZ);
		cam.SetFOV(umath::deg_to_rad(fov));

		// All entities
		EntityIterator entIt {*c_game};
		entIt.AttachFilter<TEntityIteratorFilterComponent<pragma::CRenderComponent>>();
		entIt.AttachFilter<TEntityIteratorFilterComponent<pragma::CModelComponent>>();
		for(auto *ent : entIt)
		{
			auto renderC = ent->GetComponent<pragma::CRenderComponent>();
			if(renderC->GetRenderMode() != RenderMode::World || renderC->ShouldDraw(camPos) == false || entFilter(*ent) == false)
				continue;
			scene.AddEntity(*ent);
		}
		outJob = job;
	}
	PRAGMA_EXPORT void pr_cycles_bake_ao(
		Model &mdl,uint32_t materialIndex,uint32_t width,uint32_t height,uint32_t sampleCount,bool hdrOutput,bool denoise,util::ParallelJob<std::shared_ptr<util::ImageBuffer>> &outJob
	)
	{
		outJob = {};
		auto job = setup_scene(cycles::Scene::RenderMode::BakeAmbientOcclusion,width,height,sampleCount,hdrOutput,denoise);
		if(job.IsValid() == false)
			return;
		static_cast<cycles::Scene&>(job.GetWorker()).SetAOBakeTarget(mdl,materialIndex);
		outJob = job;
	}
	PRAGMA_EXPORT void pr_cycles_bake_lightmaps(
		BaseEntity &entTarget,uint32_t width,uint32_t height,uint32_t sampleCount,bool hdrOutput,bool denoise,util::ParallelJob<std::shared_ptr<util::ImageBuffer>> &outJob
	)
	{
		outJob = {};
		auto job = setup_scene(cycles::Scene::RenderMode::BakeDiffuseLighting,width,height,sampleCount,hdrOutput,denoise);
		if(job.IsValid() == false)
			return;
		auto &scene = static_cast<cycles::Scene&>(job.GetWorker());
		setup_skybox(scene);
		setup_light_sources(scene);
		scene.SetLightmapBakeTarget(entTarget);
		outJob = job;
	}

	void PRAGMA_EXPORT pragma_initialize_lua(Lua::Interface &l)
	{
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
				Lua::Push<cycles::PScene>(l,scene);
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

		auto defSceneObject = luabind::class_<cycles::SceneObject>("SceneObject");
		defSceneObject.def("GetScene",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::SceneObject>&)>([](lua_State *l,util::WeakHandle<cycles::SceneObject> &sceneObject) {
			pragma::Lua::check_component(l,sceneObject);
			Lua::Push(l,sceneObject->GetScene().GetHandle());
		}));
		modConvert[defSceneObject];

		auto defWorldObject = luabind::class_<cycles::WorldObject,cycles::SceneObject>("WorldObject");
		defWorldObject.def("SetPos",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::WorldObject>&,const Vector3&)>([](lua_State *l,util::WeakHandle<cycles::WorldObject> &worldObject,const Vector3 &pos) {
			pragma::Lua::check_component(l,worldObject);
			worldObject->SetPos(pos);
		}));
		defWorldObject.def("GetPos",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::WorldObject>&)>([](lua_State *l,util::WeakHandle<cycles::WorldObject> &worldObject) {
			pragma::Lua::check_component(l,worldObject);
			Lua::Push<Vector3>(l,worldObject->GetPos());
		}));
		defWorldObject.def("SetRotation",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::WorldObject>&,const Quat&)>([](lua_State *l,util::WeakHandle<cycles::WorldObject> &worldObject,const Quat &rot) {
			pragma::Lua::check_component(l,worldObject);
			worldObject->SetRotation(rot);
		}));
		defWorldObject.def("GetRotation",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::WorldObject>&)>([](lua_State *l,util::WeakHandle<cycles::WorldObject> &worldObject) {
			pragma::Lua::check_component(l,worldObject);
			Lua::Push<Quat>(l,worldObject->GetRotation());
		}));
		defWorldObject.def("GetPose",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::WorldObject>&)>([](lua_State *l,util::WeakHandle<cycles::WorldObject> &worldObject) {
			pragma::Lua::check_component(l,worldObject);
			Lua::Push<pragma::physics::Transform>(l,worldObject->GetPose());
		}));
		modConvert[defWorldObject];

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

		auto defLight = luabind::class_<cycles::Light,luabind::bases<cycles::WorldObject,cycles::SceneObject>>("Light");
		defLight.add_static_constant("TYPE_POINT",umath::to_integral(cycles::Light::Type::Point));
		defLight.add_static_constant("TYPE_SPOT",umath::to_integral(cycles::Light::Type::Spot));
		defLight.add_static_constant("TYPE_DIRECTIONAL",umath::to_integral(cycles::Light::Type::Directional));
		defLight.add_static_constant("TYPE_AREA",umath::to_integral(cycles::Light::Type::Area));
		defLight.add_static_constant("TYPE_BACKGROUND",umath::to_integral(cycles::Light::Type::Background));
		defLight.add_static_constant("TYPE_TRIANGLE",umath::to_integral(cycles::Light::Type::Triangle));
		defLight.def("SetType",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Light>&,uint32_t)>([](lua_State *l,util::WeakHandle<cycles::Light> &light,uint32_t type) {
			pragma::Lua::check_component(l,light);
			light->SetType(static_cast<cycles::Light::Type>(type));
		}));
		defLight.def("SetConeAngle",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Light>&,float)>([](lua_State *l,util::WeakHandle<cycles::Light> &light,float coneAngle) {
			pragma::Lua::check_component(l,light);
			light->SetConeAngle(umath::deg_to_rad(coneAngle));
		}));
		defLight.def("SetColor",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Light>&,const Color&)>([](lua_State *l,util::WeakHandle<cycles::Light> &light,const Color &color) {
			pragma::Lua::check_component(l,light);
			light->SetColor(color);
		}));
		defLight.def("SetIntensity",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Light>&,float)>([](lua_State *l,util::WeakHandle<cycles::Light> &light,float intensity) {
			pragma::Lua::check_component(l,light);
			light->SetIntensity(intensity);
		}));
		defLight.def("SetSize",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Light>&,float)>([](lua_State *l,util::WeakHandle<cycles::Light> &light,float size) {
			pragma::Lua::check_component(l,light);
			light->SetSize(size);
		}));
		defLight.def("SetAxisU",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Light>&,const Vector3&)>([](lua_State *l,util::WeakHandle<cycles::Light> &light,const Vector3 &axisU) {
			pragma::Lua::check_component(l,light);
			light->SetAxisU(axisU);
		}));
		defLight.def("SetAxisV",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Light>&,const Vector3&)>([](lua_State *l,util::WeakHandle<cycles::Light> &light,const Vector3 &axisV) {
			pragma::Lua::check_component(l,light);
			light->SetAxisV(axisV);
		}));
		defLight.def("SetSizeU",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Light>&,float)>([](lua_State *l,util::WeakHandle<cycles::Light> &light,float sizeU) {
			pragma::Lua::check_component(l,light);
			light->SetSizeU(sizeU);
		}));
		defLight.def("SetSizeV",static_cast<void(*)(lua_State*,util::WeakHandle<cycles::Light>&,float)>([](lua_State *l,util::WeakHandle<cycles::Light> &light,float sizeV) {
			pragma::Lua::check_component(l,light);
			light->SetSizeV(sizeV);
		}));

		modConvert[defLight];

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
