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
#include <luainterface.hpp>

#include "pr_cycles/scene.hpp"
#include "pr_cycles/shader.hpp"
#include "pr_cycles/camera.hpp"
#include "pr_cycles/light.hpp"

#pragma optimize("",off)

extern DLLCLIENT CGame *c_game;

using namespace pragma::modules;

static cycles::PShader create_skybox_shader(cycles::Scene &scene)
{
	auto shaderSkybox = cycles::Shader::Create(scene,*scene->default_background);
	shaderSkybox->AddNode("sky_texture","tex");
	auto nodeBgShader = shaderSkybox->AddNode("background_shader","bg");
	nodeBgShader->SetInputArgument<float>("strength",8.f);
	shaderSkybox->Link("bg","background","output","surface");
	shaderSkybox->Link("tex","color","bg","color");
	return shaderSkybox;
}

static void capture_raytraced_scene()
{
	auto scene = cycles::Scene::Create();
	auto &cam = scene->GetCamera();
	cam.SetResolution(1920,1280);

	auto *camGame = c_game->GetRenderCamera();
	auto &entCam = camGame->GetEntity();
	auto pos = entCam.GetPosition();
	auto rot = entCam.GetRotation();
	cam.SetPos(pos);
	cam.SetRotation(rot);
	cam.SetNearZ(camGame->GetNearZ());
	cam.SetFarZ(camGame->GetFarZ());
	cam.SetFOV(camGame->GetFOVRad());

	auto shaderSkybox = create_skybox_shader(*scene);

	// All entities
	EntityIterator entIt {*c_game};
	entIt.AttachFilter<TEntityIteratorFilterComponent<pragma::CRenderComponent>>();
	entIt.AttachFilter<TEntityIteratorFilterComponent<pragma::CModelComponent>>();
	for(auto *ent : entIt)
	{
		auto renderC = ent->GetComponent<pragma::CRenderComponent>();
		if(renderC->GetRenderMode() != RenderMode::World || renderC->ShouldDraw(pos) == false)
			continue;
		scene->AddEntity(*ent);
	}

	{



		/*EulerAngles ang {-90.f,0.f,0.f};
		auto rotLight = uquat::create(ang);
		auto dir = uquat::forward(rotLight);
		auto light = cycles::Light::Create(*scene);
		light->SetType(cycles::Light::Type::Spot);
		light->SetPos(Vector3{0.f,0.f,0.f});
		light->SetRotation(rotLight);
		light->SetConeAngle(umath::deg_to_rad(25.f));
		light->SetColor(Color{255,255,255,1'0000});*/

		/*auto light = cycles::Light::Create(*scene);
		light->SetType(cycles::Light::Type::Point);
		light->SetPos(Vector3{0.f,0.f,0.f});
		light->SetColor(Color{255,255,255,20'000});*/

		/*EulerAngles ang {-90.f,0.f,0.f};
		auto rotLight = uquat::create(ang);
		auto dir = uquat::forward(rotLight);
		auto light = cycles::Light::Create(*scene);
		light->SetType(cycles::Light::Type::Directional);
		light->SetPos(Vector3{0.f,0.f,0.f});
		light->SetColor(Color{255,255,255,1'0000});
		light->SetRotation(rot);*/
	}
	// Light sources
	entIt = {*c_game};
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
			auto light = cycles::Light::Create(*scene);
			light->SetType(cycles::Light::Type::Spot);
			light->SetPos(ent->GetPosition());
			light->SetRotation(ent->GetRotation());
			light->SetConeAngle(umath::deg_to_rad(hLightSpot->GetOuterCutoffAngle()) *2.f);
			light->SetColor(color);
			continue;
		}
		auto hLightPoint = ent->GetComponent<pragma::CLightPointComponent>();
		if(hLightPoint.valid())
		{
			auto light = cycles::Light::Create(*scene);
			light->SetType(cycles::Light::Type::Point);
			light->SetPos(ent->GetPosition());
			light->SetRotation(ent->GetRotation());
			light->SetColor(color);
			continue;
		}
		auto hLightDirectional = ent->GetComponent<pragma::CLightDirectionalComponent>();
		if(hLightDirectional.valid())
		{
			auto light = cycles::Light::Create(*scene);
			light->SetType(cycles::Light::Type::Directional);
			light->SetPos(ent->GetPosition());
			light->SetRotation(ent->GetRotation());
			light->SetColor(color);
		}
	}
	
	scene->Start();
}

int module_run(int argc, const char **argv);
extern "C"
{
	PRAGMA_EXPORT bool pragma_attach(std::string &outErr)
	{
		std::cout<<"Running cycles..."<<std::endl;
		std::vector<const char*> args = {
			"E:/projects/pragma/build_winx64/output/modules/cycles/examples/",
			"--output",
			"E:/projects/pragma/build_winx64/output/modules/cycles/examples/monkey.jpg",
			"E:/projects/pragma/build_winx64/output/modules/cycles/examples/scene_world_volume.xml"
		};
		//std::cout<<"Result: "<<module_run(args.size(),args.data())<<std::endl;
		//init_scene();
		return true;
	}
	//PRAGMA_EXPORT void pr_gpl_calc_geometry_data(const std::vector<Vector3> &verts,const std::vector<uint16_t> &indices,std::vector<float> *optOutAmbientOcclusion,std::vector<Vector3> *optOutNormals,uint32_t sampleCount)
	//{
	//	calc_geometry_data(verts,indices,optOutAmbientOcclusion,optOutNormals,sampleCount);
	//}
	PRAGMA_EXPORT void pragma_initialize_lua(Lua::Interface &l)
	{
		Lua::RegisterLibrary(l.GetState(),"cycles",{
			{"capture_raytraced_scene",static_cast<int32_t(*)(lua_State*)>([](lua_State *l) -> int32_t {
				/*std::cout<<"Running cycles..."<<std::endl;
				std::vector<const char*> args = {
					"E:/projects/pragma/build_winx64/output/modules/cycles/examples/",
					"--output",
					"E:/projects/pragma/build_winx64/output/modules/cycles/examples/monkey.jpg",
					"E:/projects/pragma/build_winx64/output/modules/cycles/examples/scene_world_volume.xml"
				};
				std::cout<<"Result: "<<module_run(args.size(),args.data())<<std::endl;*/
				capture_raytraced_scene();
				return 0;
			})}
		});
	}
};
#pragma optimize("",on)
