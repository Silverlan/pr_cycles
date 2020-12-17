/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#include "pr_cycles/pr_cycles.hpp"
#include "pr_cycles/shader.hpp"
#include "pr_cycles/texture.hpp"
#include "pr_cycles/progressive_refinement.hpp"
#include <util_raytracing/cycles/renderer.hpp>
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
#include <pragma/entities/components/c_light_map_receiver_component.hpp>
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
#include <pragma/lua/libraries/lfile.h>
#include <pragma/lua/c_lentity_handles.hpp>
#include <util_image_buffer.hpp>
#include <pragma/entities/components/c_scene_component.hpp>

#include <luainterface.hpp>
#include <pragma/lua/lua_entity_component.hpp>
#include <pragma/lua/lua_call.hpp>

// Already defined b
#undef __UTIL_STRING_H__
#include <sharedutils/util_string.h>

#include <sharedutils/datastream.h>
#include <sharedutils/util.h>
#include "pr_cycles/scene.hpp"
#include <util_raytracing/shader.hpp>
#include <util_raytracing/camera.hpp>
#include <util_raytracing/light.hpp>
#include <util_raytracing/mesh.hpp>
#include <util_raytracing/object.hpp>
#include <util_raytracing/ccl_shader.hpp>
#include <util_raytracing/exception.hpp>
#include <util_raytracing/model_cache.hpp>
#include <util_raytracing/color_management.hpp>
#include <sharedutils/util_path.hpp>

#define ENABLE_BAKE_DEBUGGING_INTERFACE 1

#if ENABLE_BAKE_DEBUGGING_INTERFACE == 1
#include <wgui/wgui.h>
#include <wgui/types/wirect.h>
#endif

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
			auto light = unirender::Light::Create();
			if(light)
			{
				light->SetType(unirender::Light::Type::Spot);
				light->SetPos(ent->GetPosition());
				light->SetRotation(ent->GetRotation());
				light->SetConeAngles(hLightSpot->GetInnerCutoffAngle() *2.f,hLightSpot->GetOuterCutoffAngle() *2.f);
				light->SetColor(color);
				light->SetIntensity(lightC->GetLightIntensityLumen());
				scene->AddLight(*light);
			}
			continue;
		}
		auto hLightPoint = ent->GetComponent<pragma::CLightPointComponent>();
		if(hLightPoint.valid())
		{
			auto light = unirender::Light::Create();
			if(light)
			{
				light->SetType(unirender::Light::Type::Point);
				light->SetPos(ent->GetPosition());
				light->SetRotation(ent->GetRotation());
				light->SetColor(color);
				light->SetIntensity(lightC->GetLightIntensityLumen());
				scene->AddLight(*light);
			}
			continue;
		}
		auto hLightDirectional = ent->GetComponent<pragma::CLightDirectionalComponent>();
		if(hLightDirectional.valid())
		{
			auto light = unirender::Light::Create();
			if(light)
			{
				light->SetType(unirender::Light::Type::Directional);
				light->SetPos(ent->GetPosition());
				light->SetRotation(ent->GetRotation());
				light->SetColor(color);
				light->SetIntensity(lightC->GetLightIntensity());
				scene->AddLight(*light);
			}
		}
	}
}

static std::shared_ptr<unirender::NodeManager> g_nodeManager = nullptr;
static std::shared_ptr<pragma::modules::cycles::ShaderManager> g_shaderManager = nullptr;
unirender::NodeManager &pragma::modules::cycles::get_node_manager()
{
	if(g_nodeManager == nullptr)
		g_nodeManager = unirender::NodeManager::Create();
	return *g_nodeManager;
}
pragma::modules::cycles::ShaderManager &pragma::modules::cycles::get_shader_manager()
{
	if(g_shaderManager == nullptr)
		g_shaderManager = pragma::modules::cycles::ShaderManager::Create();
	return *g_shaderManager;
}
static std::shared_ptr<cycles::Scene> setup_scene(
	unirender::Scene::RenderMode renderMode,uint32_t width,uint32_t height,uint32_t sampleCount,bool hdrOutput,unirender::Scene::DenoiseMode denoiseMode,
	unirender::Scene::DeviceType deviceType=unirender::Scene::DeviceType::CPU,float exposure=1.f,const std::optional<unirender::Scene::ColorTransformInfo> &colorTransform={}
)
{
	unirender::Scene::CreateInfo createInfo {};
	createInfo.denoiseMode = denoiseMode;
	createInfo.hdrOutput = hdrOutput;
	createInfo.samples = sampleCount;
	createInfo.deviceType = deviceType;
	createInfo.colorTransform = colorTransform;
	createInfo.exposure = exposure;
	auto scene = unirender::Scene::Create(pragma::modules::cycles::get_node_manager(),renderMode,createInfo);
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

struct CameraData
{
	Vector3 position;
	Quat rotation;
	Mat4 viewProjection;
	float nearZ = 0.f;
	float farZ = 0.f;
	float fov = 0.f;
	float aspectRatio = 0.f;
};
static void initialize_cycles_geometry(
	pragma::CSceneComponent &gameScene,pragma::modules::cycles::Cache &cache,const std::optional<CameraData> &camData,SceneFlags sceneFlags,
	const std::function<bool(BaseEntity&)> &entFilter=nullptr,const std::function<bool(BaseEntity&)> &lightFilter=nullptr
)
{
	auto enableFrustumCulling = umath::is_flag_set(sceneFlags,SceneFlags::CullObjectsOutsideCameraFrustum);
	auto cullObjectsOutsidePvs = umath::is_flag_set(sceneFlags,SceneFlags::CullObjectsOutsidePvs);
	std::vector<umath::Plane> planes {};
	if(camData.has_value())
	{
		auto forward = uquat::forward(camData->rotation);
		auto up = uquat::up(camData->rotation);
		pragma::BaseEnvCameraComponent::GetFrustumPlanes(planes,camData->nearZ,camData->farZ,camData->fov,camData->aspectRatio,camData->position,forward,up);
	}
	auto entSceneFilterEx = [&gameScene,&camData,&planes,enableFrustumCulling](BaseEntity &ent,bool useFrustumCullingIfEnabled) -> bool {
		if(static_cast<CBaseEntity&>(ent).IsInScene(gameScene) == false)
			return false;
		if(useFrustumCullingIfEnabled == false || enableFrustumCulling == false || camData.has_value() == false)
			return true;
		auto renderC = ent.GetComponent<pragma::CRenderComponent>();
		if(renderC.expired())
			return false;
		if(renderC->IsExemptFromOcclusionCulling())
			return true;
		if(renderC->ShouldDraw() == false)
			return false;
		auto sphere = renderC->GetAbsoluteRenderSphere();
		if(umath::intersection::sphere_in_plane_mesh(sphere.pos,sphere.radius,planes,true) == umath::intersection::Intersect::Outside)
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

	util::BSPTree *bspTree = nullptr;
	util::BSPTree::Node *node = nullptr;
	if(cullObjectsOutsidePvs && camData.has_value())
	{
		EntityIterator entItWorld {*c_game};
		entItWorld.AttachFilter<TEntityIteratorFilterComponent<pragma::CWorldComponent>>();
		auto it = entItWorld.begin();
		auto *entWorld = (it != entItWorld.end()) ? *it : nullptr;
		if(entWorld)
		{
			auto worldC = entWorld->GetComponent<pragma::CWorldComponent>();
			bspTree = worldC.valid() ? worldC->GetBSPTree().get() : nullptr;
			node = bspTree ? bspTree->FindLeafNode(camData->position) : nullptr;
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
		if((renderMode != RenderMode::World && renderMode != RenderMode::Skybox) || (camData.has_value() && renderC->ShouldDraw() == false) || (entFilter && entFilter(*ent) == false))
			continue;
		std::function<bool(ModelMesh&,const umath::ScaledTransform&)> meshFilter = nullptr;
		if(renderC->IsExemptFromOcclusionCulling() == false)
		{
			// We'll only do per-mesh culling for world entities
			if(enableFrustumCulling  && ent->IsWorld())
			{
				meshFilter = [&planes](ModelMesh &mesh,const umath::ScaledTransform &pose) -> bool {
					Vector3 min,max;
					mesh.GetBounds(min,max);
					auto center = (min +max) /2.f;
					min -= center;
					max -= center;
					auto r = umath::max(umath::abs(min.x),umath::abs(min.y),umath::abs(min.z),umath::abs(max.x),umath::abs(max.y),umath::abs(max.z));
					center += pose.GetOrigin();
					return (umath::intersection::sphere_in_plane_mesh(center,r,planes) != umath::intersection::Intersect::Outside) ? true : false;
				};
			}
			if(node)
			{
				auto curFilter = meshFilter;
				// Cull everything outside the camera's PVS
				if(ent->IsWorld())
				{
					auto pos = ent->GetPosition();
					meshFilter = [bspTree,node,pos,curFilter](ModelMesh &mesh,const umath::ScaledTransform &pose) -> bool {
						if(curFilter && curFilter(mesh,pose) == false)
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
						return umath::intersection::aabb_aabb(min,max,node->minVisible,node->maxVisible);
					};
#endif
				}
			}
		}
		cache.AddEntity(*ent,nullptr,meshFilter);
	}

	// Particle Systems
#if 0
	EntityIterator entItPt {*c_game};
	entItPt.AttachFilter<TEntityIteratorFilterComponent<pragma::CParticleSystemComponent>>();
	entItPt.AttachFilter<EntityIteratorFilterUser>(entSceneFilter);
	for(auto *ent : entItPt)
	{
		auto ptc = ent->GetComponent<pragma::CParticleSystemComponent>();
		cache.AddParticleSystem(*ptc,camPos,vp,nearZ,farZ);
	}
#endif
}

static void initialize_cycles_scene_from_game_scene(
	pragma::CSceneComponent &gameScene,pragma::modules::cycles::Scene &scene,const Vector3 &camPos,const Quat &camRot,bool equirect,const Mat4 &vp,float nearZ,float farZ,float fov,float aspectRatio,SceneFlags sceneFlags,
	const std::function<bool(BaseEntity&)> &entFilter=nullptr,const std::function<bool(BaseEntity&)> &lightFilter=nullptr
)
{
	CameraData camData {};
	camData.position = camPos;
	camData.rotation = camRot;
	camData.viewProjection = vp;
	camData.nearZ = nearZ;
	camData.farZ = farZ;
	camData.fov = fov;
	camData.aspectRatio = aspectRatio;
	initialize_cycles_geometry(gameScene,scene.GetCache(),camData,sceneFlags,entFilter,lightFilter);
	setup_light_sources(scene,[&gameScene,&lightFilter](BaseEntity &ent) -> bool {
		if(static_cast<CBaseEntity&>(ent).IsInScene(gameScene) == false)
			return false;
		return (lightFilter == nullptr || lightFilter(ent));
	});

	auto &cam = scene->GetCamera();
	cam.SetPos(camPos);
	cam.SetRotation(camRot);
	cam.SetNearZ(unirender::Scene::ToCyclesLength(nearZ));
	cam.SetFarZ(unirender::Scene::ToCyclesLength(farZ));
	cam.SetFOV(fov);

	if(equirect)
	{
		cam.SetCameraType(unirender::Camera::CameraType::Panorama);
		cam.SetPanoramaType(unirender::Camera::PanoramaType::Equirectangular);
	}

	// 3D Sky
	EntityIterator entIt3dSky {*c_game};
	entIt3dSky.AttachFilter<TEntityIteratorFilterComponent<pragma::CSkyCameraComponent>>();
	for(auto *ent : entIt3dSky)
	{
		auto skyc = ent->GetComponent<pragma::CSkyCameraComponent>();
		scene.Add3DSkybox(*skyc,camPos);
	}
}

inline std::function<bool(BaseEntity&)> to_entity_filter(lua_State *l,luabind::object *optEntFilter,uint32_t idx)
{
	Lua::CheckFunction(l,idx);
	return [l,optEntFilter](BaseEntity &ent) -> bool {
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
}

static void initialize_from_game_scene(
	lua_State *l,pragma::CSceneComponent &gameScene,cycles::Scene &scene,const Vector3 &camPos,const Quat &camRot,const Mat4 &vp,
	float nearZ,float farZ,float fov,SceneFlags sceneFlags,luabind::object *optEntFilter,luabind::object *optLightFilter
)
{
	auto entFilter = to_entity_filter(l,optEntFilter,10);
	auto lightFilter = to_entity_filter(l,optLightFilter,11);
	auto aspectRatio = gameScene.GetWidth() /static_cast<float>(gameScene.GetHeight());
	initialize_cycles_scene_from_game_scene(gameScene,scene,camPos,camRot,false,vp,nearZ,farZ,fov,aspectRatio,sceneFlags,entFilter,lightFilter);
	scene.Finalize();
}

static luabind::object get_node_lua_object(lua_State *l,unirender::NodeDesc &node)
{
	if(node.IsGroupNode())
		return luabind::object{l,std::static_pointer_cast<unirender::GroupNodeDesc>(node.shared_from_this())};
	return luabind::object{l,node.shared_from_this()};
}

static unirender::NodeTypeId register_node(lua_State *l,const std::string &typeName,luabind::object function)
{
	return pragma::modules::cycles::get_node_manager().RegisterNodeType(typeName,[l,function](unirender::GroupNodeDesc *parent) mutable -> std::shared_ptr<unirender::NodeDesc> {
		auto node = unirender::GroupNodeDesc::Create(pragma::modules::cycles::get_node_manager(),parent);
		function(get_node_lua_object(l,*node));
		return node;
	});
}

static void register_shader(lua_State *l,const std::string &name,luabind::object shaderClass)
{
	auto &sm = pragma::modules::cycles::get_shader_manager();
	sm.RegisterShader(name,shaderClass);
}

template<typename T,unirender::SocketType srcType,unirender::SocketIO ioType>
	static unirender::Socket register_input(lua_State *l,unirender::GroupNodeDesc &node,unirender::SocketType st,const std::string &name,const T &defaultValue)
{
	auto value = unirender::convert(&defaultValue,srcType,st);
	if(value.has_value() == false)
		Lua::Error(l,"Default value is incompatible with socket type " +unirender::to_string(st) +"!");
	return node.RegisterSocket(name,*value,ioType);
}

static unirender::Socket register_output(lua_State *l,unirender::GroupNodeDesc &node,unirender::SocketType st,const std::string &name)
{
	return node.RegisterSocket(name,unirender::DataValue{st,nullptr},unirender::SocketIO::Out);
}

template<unirender::SocketIO ioType>
	static void register_socket_methods(luabind::class_<unirender::GroupNodeDesc,luabind::bases<unirender::NodeDesc>> &defNode)
{
	// TODO: Move 'name' to template parameters once C++-20 is available
	const char *name;
	switch(ioType)
	{
	case unirender::SocketIO::In:
		name = "RegisterInput";
		break;
	case unirender::SocketIO::Out:
		name = "RegisterOutput";
		break;
	case unirender::SocketIO::None:
		name = "RegisterProperty";
		break;
	}
	if constexpr(ioType != unirender::SocketIO::Out)
	{
		defNode.def(name,register_input<bool,unirender::SocketType::Bool,ioType>);
		defNode.def(name,register_input<float,unirender::SocketType::Float,ioType>);
		defNode.def(name,register_input<Vector3,unirender::SocketType::Vector,ioType>);
		defNode.def(name,register_input<Vector2,unirender::SocketType::Point2,ioType>);
		defNode.def(name,register_input<std::string,unirender::SocketType::String,ioType>);
		defNode.def(name,register_input<Mat4x3,unirender::SocketType::Transform,ioType>);
	}
	else
		defNode.def(name,register_output);
}

namespace luabind
{
	// These have to be in the luabind namespace for whatever reason
	unirender::Socket operator+(float f,const unirender::Socket &socket) {return unirender::Socket{f} +socket;}
	unirender::Socket operator-(float f,const unirender::Socket &socket) {return unirender::Socket{f} -socket;}
	unirender::Socket operator*(float f,const unirender::Socket &socket) {return unirender::Socket{f} *socket;}
	unirender::Socket operator/(float f,const unirender::Socket &socket) {return unirender::Socket{f} /socket;}
	unirender::Socket operator%(float f,const unirender::Socket &socket) {return unirender::Socket{f} %socket;}
	unirender::Socket operator^(float f,const unirender::Socket &socket) {return unirender::Socket{f} ^socket;}
	
	unirender::Socket operator<(float f,const unirender::Socket &socket) {return unirender::Socket{f} < socket;}
	unirender::Socket operator<=(float f,const unirender::Socket &socket) {return unirender::Socket{f} <= socket;}

	unirender::Socket operator+(const Vector3 &v,const unirender::Socket &socket) {return unirender::Socket{v} +socket;}
	unirender::Socket operator-(const Vector3 &v,const unirender::Socket &socket) {return unirender::Socket{v} -socket;}
	unirender::Socket operator*(const Vector3 &v,const unirender::Socket &socket) {return unirender::Socket{v} *socket;}
	unirender::Socket operator/(const Vector3 &v,const unirender::Socket &socket) {return unirender::Socket{v} /socket;}
	unirender::Socket operator%(const Vector3 &v,const unirender::Socket &socket) {return unirender::Socket{v} %socket;}
};

static unirender::Socket get_socket(const luabind::object &o)
{
	auto type = luabind::type(o);
	switch(type)
	{
	case LUA_TBOOLEAN:
		return unirender::Socket{luabind::object_cast<bool>(o) ? 1.f : 0.f};
	case LUA_TNUMBER:
		return unirender::Socket{luabind::object_cast<float>(o)};
	default:
	{
		try
		{
			auto v = luabind::object_cast<Vector3>(o);
			return unirender::Socket{v};
		}
		catch(const luabind::cast_failed &e) {}
		return luabind::object_cast<unirender::Socket>(o);
	}
	}
	// Unreachable
	return unirender::Socket{};
}

static unirender::GroupNodeDesc *find_socket_node(lua_State *l,const std::vector<std::reference_wrapper<unirender::Socket>> &sockets)
{
	unirender::GroupNodeDesc *node = nullptr;
	for(auto &socket : sockets)
	{
		auto *n = socket.get().GetNode();
		node = n ? n->GetParent() : nullptr;
		if(node != nullptr)
			break;
	}
	return node;
}
static unirender::GroupNodeDesc &get_socket_node(lua_State *l,const std::vector<std::reference_wrapper<unirender::Socket>> &sockets)
{
	auto *node = find_socket_node(l,sockets);
	if(node == nullptr)
		Lua::Error(l,"This operation is only supported for non-concrete socket types!");
	return *node;
}
static unirender::GroupNodeDesc *find_socket_node(lua_State *l,unirender::Socket &socket)
{
	auto *node = socket.GetNode();
	auto *parent = node ? node->GetParent() : nullptr;
	return parent;
}
static unirender::GroupNodeDesc &get_socket_node(lua_State *l,unirender::Socket &socket)
{
	auto *node = find_socket_node(l,socket);
	if(node == nullptr)
		Lua::Error(l,"This operation is only supported for non-concrete socket types!");
	return *node;
}

enum class VectorChannel : uint8_t
{
	X = 0,
	Y,
	Z
};
template<VectorChannel channel>
	static unirender::Socket get_vector_socket_component(lua_State *l,unirender::Socket &socket)
{
	auto &parent = get_socket_node(l,socket);
	auto &rgb = parent.SeparateRGB(socket);
	switch(channel)
	{
	case VectorChannel::X:
		return rgb.GetOutputSocket(unirender::nodes::separate_rgb::OUT_R);
	case VectorChannel::Y:
		return rgb.GetOutputSocket(unirender::nodes::separate_rgb::OUT_G);
	case VectorChannel::Z:
		return rgb.GetOutputSocket(unirender::nodes::separate_rgb::OUT_B);
	}
	return {};
}
template<VectorChannel channel>
	static void set_vector_socket_component(lua_State *l,unirender::Socket &socket,const unirender::Socket &other)
{
	auto &parent = get_socket_node(l,socket);
	auto &rgb = parent.SeparateRGB(socket);
	socket = parent.CombineRGB(
		(channel == VectorChannel::X) ? other : rgb.GetOutputSocket(unirender::nodes::separate_rgb::OUT_R),
		(channel == VectorChannel::Y) ? other : rgb.GetOutputSocket(unirender::nodes::separate_rgb::OUT_G),
		(channel == VectorChannel::Z) ? other : rgb.GetOutputSocket(unirender::nodes::separate_rgb::OUT_B)
	);
}

template<unirender::nodes::math::MathType type>
	static unirender::Socket socket_math_op_tri(lua_State *l,unirender::Socket &socket,luabind::object socketOther,luabind::object third)
{
	auto &parent = get_socket_node(l,socket);
	auto &result = parent.AddNode(unirender::NODE_MATH);
	result.SetProperty(unirender::nodes::math::IN_TYPE,type);
	parent.Link(socket,result.GetInputSocket(unirender::nodes::math::IN_VALUE1));
	parent.Link(get_socket(socketOther),result.GetInputSocket(unirender::nodes::math::IN_VALUE2));
	parent.Link(get_socket(third),result.GetInputSocket(unirender::nodes::math::IN_VALUE3));
	return *result.GetPrimaryOutputSocket();
}

template<unirender::nodes::math::MathType type>
	static unirender::Socket socket_math_op(lua_State *l,unirender::Socket &socket,luabind::object socketOther)
{
	auto &parent = get_socket_node(l,socket);
	return parent.AddMathNode(socket,get_socket(socketOther),type);
}

template<unirender::nodes::math::MathType type>
	static unirender::Socket socket_math_op_unary(lua_State *l,unirender::Socket &socket)
{
	auto &parent = get_socket_node(l,socket);
	return parent.AddMathNode(socket,{},type);
}

template<unirender::nodes::vector_math::MathType type,bool useVectorOutput=true>
	static unirender::Socket socket_vector_op(lua_State *l,unirender::Socket &socket,luabind::object socketOther)
{
	auto &parent = get_socket_node(l,socket);
	auto &result = parent.AddVectorMathNode(socket,get_socket(socketOther),type);
	if constexpr(useVectorOutput)
		return *result.GetPrimaryOutputSocket();
	return result.GetOutputSocket(unirender::nodes::vector_math::OUT_VALUE);
}

template<unirender::nodes::vector_math::MathType type,bool useVectorOutput=true>
	static unirender::Socket socket_vector_op_unary(lua_State *l,unirender::Socket &socket)
{
	auto &parent = get_socket_node(l,socket);
	auto &result = parent.AddVectorMathNode(socket,{},type);
	if constexpr(useVectorOutput)
		return *result.GetPrimaryOutputSocket();
	return result.GetOutputSocket(unirender::nodes::vector_math::OUT_VALUE);
}

static std::array<unirender::Socket*,3> socket_to_xyz(lua_State *l,unirender::Socket &socket)
{
	auto &node = get_socket_node(l,socket);
	std::array<unirender::Socket*,3> socketXyz;
	if(unirender::is_vector_type(socket.GetType()))
	{
		auto &nodeXyz = node.SeparateRGB(socket);
		socketXyz = {&nodeXyz.GetOutputSocket(unirender::nodes::separate_rgb::OUT_R),&nodeXyz.GetOutputSocket(unirender::nodes::separate_rgb::OUT_G),&nodeXyz.GetOutputSocket(unirender::nodes::separate_rgb::OUT_B)};
	}
	else
		socketXyz = {&socket,&socket,&socket};
	return socketXyz;
}

static unirender::Socket socket_to_vector(unirender::GroupNodeDesc &node,unirender::Socket &socket)
{
	if(unirender::is_vector_type(socket.GetType()))
		return socket;
	return node.CombineRGB(socket,socket,socket);
}

#include <sharedutils/util_library.hpp>
static void test_luxcore(cycles::Scene &scene)
{
	std::vector<std::string> additionalSearchDirectories;
	additionalSearchDirectories.push_back("E:/projects/pragma/build_winx64/output/modules/unirender/luxcorerender/");
	std::string err;
	auto lib = util::Library::Load("E:/projects/pragma/build_winx64/output/modules/unirender/luxcorerender/UniRender_LuxCoreRender",additionalSearchDirectories,&err);
	if(lib == nullptr)
	{
		std::cout<<"Err: "<<err<<std::endl;
		return;
	}
	auto *func = lib->FindSymbolAddress<std::shared_ptr<unirender::Renderer>(*)(const unirender::Scene&)>("test_luxcorerender");
	auto renderer = func(*scene);
	std::cout<<renderer.get()<<std::endl;
}

extern "C"
{
	PRAGMA_EXPORT void pr_cycles_render_image(
		uint32_t width,uint32_t height,uint32_t sampleCount,bool hdrOutput,bool denoise,
		const Vector3 &camPos,const Quat &camRot,bool equirect,const Mat4 &vp,float nearZ,float farZ,umath::Degree fov,
		SceneFlags sceneFlags,std::string skyOverride,EulerAngles skyAngles,float skyStrength,
		float maxTransparencyBounces,const std::function<bool(BaseEntity&)> &entFilter,util::ParallelJob<std::shared_ptr<uimg::ImageBuffer>> &outJob
	)
	{
		outJob = {};
		auto scene = setup_scene(unirender::Scene::RenderMode::RenderImage,width,height,sampleCount,hdrOutput,denoise ? unirender::Scene::DenoiseMode::Detailed : unirender::Scene::DenoiseMode::None);
		if(scene == nullptr)
			return;
		auto aspectRatio = width /static_cast<float>(height);
		initialize_cycles_scene_from_game_scene(*c_game->GetScene(),*scene,camPos,camRot,equirect,vp,nearZ,farZ,fov,aspectRatio,sceneFlags,entFilter);
		scene->Finalize();
		if(skyOverride.empty() == false)
			(*scene)->SetSky(skyOverride);
		(*scene)->SetSkyAngles(skyAngles);
		(*scene)->SetSkyStrength(skyStrength);
		auto renderer = unirender::cycles::Renderer::Create(**scene);
		if(renderer == nullptr)
			return;
		outJob = renderer->StartRender();
	}
	PRAGMA_EXPORT void pr_cycles_bake_ao(
		Model &mdl,uint32_t materialIndex,uint32_t width,uint32_t height,uint32_t sampleCount,bool hdrOutput,bool denoise,const std::string &deviceType,util::ParallelJob<std::shared_ptr<uimg::ImageBuffer>> &outJob
	)
	{
		outJob = {};
		auto eDeviceType = ustring::compare(deviceType,"gpu",false) ? unirender::Scene::DeviceType::GPU : unirender::Scene::DeviceType::CPU;
		auto scene = setup_scene(unirender::Scene::RenderMode::BakeAmbientOcclusion,width,height,sampleCount,hdrOutput,denoise ? unirender::Scene::DenoiseMode::Detailed : unirender::Scene::DenoiseMode::None,eDeviceType);
		if(scene == nullptr)
			return;
		scene->SetAOBakeTarget(mdl,materialIndex);
		scene->Finalize();
		auto renderer = unirender::cycles::Renderer::Create(**scene);
		if(renderer == nullptr)
			return;
#if ENABLE_BAKE_DEBUGGING_INTERFACE == 1
		{
			static std::shared_ptr<pragma::modules::cycles::ProgressiveTexture> prt = nullptr;
			prt = std::make_shared<pragma::modules::cycles::ProgressiveTexture>();
			prt->Initialize(*renderer);
			auto el = WGUI::GetInstance().Create<WITexturedRect>();
			el->SetSize(512,512);
			el->SetTexture(*prt->GetTexture());
			el->SetZPos(10000);
			el->SetName("bake_feedback");
			
		}
#endif
		outJob = renderer->StartRender();
	}
	PRAGMA_EXPORT void pr_cycles_bake_ao_ent(
		BaseEntity &ent,uint32_t materialIndex,uint32_t width,uint32_t height,uint32_t sampleCount,bool hdrOutput,bool denoise,const std::string &deviceType,util::ParallelJob<std::shared_ptr<uimg::ImageBuffer>> &outJob
	)
	{
		outJob = {};
		auto eDeviceType = ustring::compare(deviceType,"gpu",false) ? unirender::Scene::DeviceType::GPU : unirender::Scene::DeviceType::CPU;
		auto scene = setup_scene(unirender::Scene::RenderMode::BakeAmbientOcclusion,width,height,sampleCount,hdrOutput,denoise ? unirender::Scene::DenoiseMode::Detailed : unirender::Scene::DenoiseMode::None,eDeviceType);
		if(scene == nullptr)
			return;
		scene->SetAOBakeTarget(ent,materialIndex);
		scene->Finalize();
		auto renderer = unirender::cycles::Renderer::Create(**scene);
		if(renderer == nullptr)
			return;
		outJob = renderer->StartRender();
	}
	PRAGMA_EXPORT void pr_cycles_bake_lightmaps(
		uint32_t width,uint32_t height,uint32_t sampleCount,bool hdrOutput,bool denoise,
		std::string skyOverride,EulerAngles skyAngles,float skyStrength,bool externalJob,
		float exposure,const std::optional<std::string> &colorTransformConfig,const std::optional<std::string> &colorTransformLook,util::ParallelJob<std::shared_ptr<uimg::ImageBuffer>> &outJob
	)
	{
		outJob = {};
		std::optional<unirender::Scene::ColorTransformInfo> colorTransform {};
		if(colorTransformConfig.has_value() && colorTransformLook.has_value())
		{
			colorTransform = unirender::Scene::ColorTransformInfo{};
			colorTransform->config = *colorTransformConfig;
			colorTransform->lookName = *colorTransformLook;
		}
		auto scene = setup_scene(
			unirender::Scene::RenderMode::BakeDiffuseLighting,width,height,sampleCount,hdrOutput,denoise ? unirender::Scene::DenoiseMode::Detailed : unirender::Scene::DenoiseMode::None,
			unirender::Scene::DeviceType::GPU,exposure,colorTransform
		);
		if(scene == nullptr)
			return;
		auto &gameScene = *c_game->GetScene();
		setup_light_sources(*scene,[&gameScene](BaseEntity &ent) -> bool {
			return static_cast<CBaseEntity&>(ent).IsInScene(gameScene);
		});
		EntityIterator entIt {*c_game};
		entIt.AttachFilter<TEntityIteratorFilterComponent<pragma::CLightMapReceiverComponent>>();
		for(auto *ent : entIt)
			scene->AddLightmapBakeTarget(*ent);
		if(skyOverride.empty() == false)
			(*scene)->SetSky(skyOverride);
		(*scene)->SetSkyAngles(skyAngles);
		(*scene)->SetSkyStrength(skyStrength);
		scene->Finalize();
		
		if(externalJob)
		{
			std::string path = "render/lightmaps/";
			auto fileName = path +"lightmap.prt";
			auto rootPath = util::Path::CreatePath(FileManager::GetProgramPath()).GetString() +path;
			unirender::Scene::SerializationData serializationData {};
			serializationData.outputFileName = fileName;
			DataStream ds {};
			(*scene)->Save(ds,rootPath,serializationData);
			FileManager::CreatePath(path.c_str());
			auto f = FileManager::OpenFile<VFilePtrReal>(fileName.c_str(),"wb");
			if(f)
			{
				f->Write(ds->GetData(),ds->GetInternalSize());
				f = nullptr;
			}
		}
		else
		{
			auto renderer = unirender::cycles::Renderer::Create(**scene);
			if(renderer == nullptr)
				return;
#if ENABLE_BAKE_DEBUGGING_INTERFACE == 1
			{
				static std::shared_ptr<pragma::modules::cycles::ProgressiveTexture> prt = nullptr;
				prt = std::make_shared<pragma::modules::cycles::ProgressiveTexture>();
				prt->Initialize(*renderer);
				auto el = WGUI::GetInstance().Create<WITexturedRect>();
				el->SetSize(512,512);
				el->SetTexture(*prt->GetTexture());
				el->SetZPos(10000);
				el->SetName("bake_feedback");
			
		}
#endif
			outJob = renderer->StartRender();
		}
	}

	bool PRAGMA_EXPORT pragma_attach(std::string &errMsg)
	{
		unirender::Scene::SetKernelPath(util::get_program_path() +"/modules/cycles");
		return true;
	}

	void PRAGMA_EXPORT pragma_terminate_lua(Lua::Interface &l)
	{
		g_nodeManager = nullptr;
		g_shaderManager = nullptr;
	}

	void PRAGMA_EXPORT pragma_initialize_lua(Lua::Interface &l)
	{
		auto &modCycles = l.RegisterLibrary("cycles",std::unordered_map<std::string,int32_t(*)(lua_State*)>{
			{"create_scene",static_cast<int32_t(*)(lua_State*)>([](lua_State *l) -> int32_t {
				auto renderMode = static_cast<unirender::Scene::RenderMode>(Lua::CheckInt(l,1));
				auto &createInfo = Lua::Check<unirender::Scene::CreateInfo>(l,2);
				auto scene = unirender::Scene::Create(pragma::modules::cycles::get_node_manager(),renderMode,createInfo);
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
				auto deviceType = unirender::Scene::DeviceType::CPU;
				if(Lua::IsSet(l,3))
					width = Lua::CheckInt(l,3);
				if(Lua::IsSet(l,4))
					height = Lua::CheckInt(l,4);
				if(Lua::IsSet(l,5))
					sampleCount = Lua::CheckInt(l,5);
				if(Lua::IsSet(l,6))
					deviceType = static_cast<unirender::Scene::DeviceType>(Lua::CheckInt(l,6));
				auto hdrOutput = false;
				auto denoise = true;
				auto scene = setup_scene(unirender::Scene::RenderMode::BakeAmbientOcclusion,width,height,sampleCount,hdrOutput,denoise ? unirender::Scene::DenoiseMode::Detailed : unirender::Scene::DenoiseMode::None,deviceType);
				if(scene == nullptr)
					return 0;
				scene->SetAOBakeTarget(mdl,materialIndex);
				auto renderer = unirender::cycles::Renderer::Create(**scene);
				if(renderer == nullptr)
					return 0;
				auto job = renderer->StartRender();
				Lua::Push(l,job);
				return 1;
			})},
			{"create_cache",static_cast<int32_t(*)(lua_State*)>([](lua_State *l) -> int32_t {
				Lua::Push(l,std::make_shared<pragma::modules::cycles::Cache>(unirender::Scene::RenderMode::RenderImage));
				return 1;
			})},
			{"denoise_image",static_cast<int32_t(*)(lua_State*)>([](lua_State *l) -> int32_t {
				auto &imgBuf = Lua::Check<uimg::ImageBuffer>(l,1);
				Lua::Push(l,pragma::modules::cycles::denoise(imgBuf));
				return 1;
			})},
			{"create_renderer",static_cast<int32_t(*)(lua_State*)>([](lua_State *l) -> int32_t {
				auto &scene = Lua::Check<cycles::Scene>(l,1);
				test_luxcore(scene);
				std::string rendererIdentifier = Lua::CheckString(l,2);
				if(rendererIdentifier != "cycles") // TODO: Implement this properly!
					return 0;
				auto renderer = unirender::cycles::Renderer::Create(*scene);
				if(renderer == nullptr)
					return 0;
				Lua::Push<std::shared_ptr<pragma::modules::cycles::Renderer>>(l,std::make_shared<pragma::modules::cycles::Renderer>(scene,*renderer));
				return 1;
			})},
#if 0
			{"apply_color_transform",static_cast<int32_t(*)(lua_State*)>([](lua_State *l) -> int32_t {
				auto &imgBuf = Lua::Check<uimg::ImageBuffer>(l,1);

				auto ocioConfigLocation = util::Path::CreatePath(util::get_program_path());
				ocioConfigLocation += "modules/open_color_io/configs/";
				ocioConfigLocation.Canonicalize();
				
				std::string err;
				auto processor = unirender::create_color_transform_processor(unirender::ColorTransform::FilmicBlender,err);
				processor->Apply();
				auto result = unirender::create_color_transform_processor(imgBuf,unirender::ColorTransform::FilmicBlender,ocioConfigLocation.GetString(),0.f /* exposure */,2.2f /* gamma */,err);
				Lua::PushBool(l,result);
				if(result == false)
				{
					Lua::PushString(l,err);
					return 2;
				}
				return 1;
			})}
#endif
		});
		modCycles[
			luabind::def("register_node",static_cast<unirender::NodeTypeId(*)(lua_State*,const std::string&,luabind::object)>([](lua_State *l,const std::string &typeName,luabind::object function) -> unirender::NodeTypeId {
				Lua::CheckFunction(l,2);
				return register_node(l,typeName,function);
			})),
			luabind::def("register_shader",static_cast<void(*)(lua_State*,const std::string&,luabind::object)>([](lua_State *l,const std::string &className,luabind::object shaderClass) {
				Lua::CheckUserData(l,2);
				register_shader(l,className,shaderClass);
			}))
		];
#if 0
		modCycles[
			luabind::def("subdivision",static_cast<luabind::object(*)(lua_State*,luabind::table<>,luabind::table<>,uint32_t)>([](lua_State *l,luabind::table<> tVerts,luabind::table<> tTris,uint32_t subDivLevel) -> luabind::object {
				auto verts = Lua::table_to_vector<Vertex>(l,tVerts,1);
				auto tris = Lua::table_to_vector<uint16_t>(l,tTris,2);


				std::vector<OpenSubdiv::Far::Index> osdIndices {};
				osdIndices.reserve(tris.size());
				for(auto idx : tris)
					osdIndices.push_back(idx);

				std::vector<Vertex> newVerts;
				std::vector<int32_t> newTris;
				test_subdiv(verts,osdIndices,newVerts,newTris,subDivLevel);
				auto t = luabind::newtable(l);
				t[1] = Lua::vector_to_table(l,newVerts);
				t[2] = Lua::vector_to_table(l,newTris);
				return t;
			}))
		];
#endif
		
		auto defRenderer = luabind::class_<pragma::modules::cycles::Renderer>("Renderer");
		defRenderer.def("StartRender",static_cast<void(*)(lua_State*,pragma::modules::cycles::Renderer&)>([](lua_State *l,pragma::modules::cycles::Renderer &renderer) {
			auto job = renderer->StartRender();
			if(job.IsValid() == false)
				return;
			Lua::Push(l,job);
		}));
		defRenderer.def("CreateProgressiveImageHandler",static_cast<std::shared_ptr<pragma::modules::cycles::ProgressiveTexture>(*)(lua_State*,pragma::modules::cycles::Renderer&)>([](lua_State *l,pragma::modules::cycles::Renderer &renderer) -> std::shared_ptr<pragma::modules::cycles::ProgressiveTexture> {
			auto prt = std::make_shared<pragma::modules::cycles::ProgressiveTexture>();
			prt->Initialize(*renderer);
			return prt;
		}));
		defRenderer.def("Restart",static_cast<void(*)(lua_State*,pragma::modules::cycles::Renderer&)>([](lua_State *l,pragma::modules::cycles::Renderer &renderer) {
			renderer->Restart();
		}));
		defRenderer.def("Reset",static_cast<void(*)(lua_State*,pragma::modules::cycles::Renderer&)>([](lua_State *l,pragma::modules::cycles::Renderer &renderer) {
			renderer->Reset();
		}));
		defRenderer.def("StopRendering",static_cast<void(*)(lua_State*,pragma::modules::cycles::Renderer&)>([](lua_State *l,pragma::modules::cycles::Renderer &renderer) {
			renderer->StopRendering();
		}));
		defRenderer.def("ReloadShaders",static_cast<void(*)(lua_State*,pragma::modules::cycles::Renderer&)>([](lua_State *l,pragma::modules::cycles::Renderer &renderer) {
			renderer.ReloadShaders();
		}));
		defRenderer.def("HasRenderedSamplesForAllTiles",static_cast<bool(*)(lua_State*,pragma::modules::cycles::Renderer&)>([](lua_State *l,pragma::modules::cycles::Renderer &renderer) -> bool {
			return renderer->GetTileManager().AllTilesHaveRenderedSamples();
		}));
		modCycles[defRenderer];

		auto defNode = luabind::class_<unirender::NodeDesc>("Node");
		defNode.def(tostring(luabind::self));
		defNode.def(-luabind::const_self);
		defNode.def(luabind::const_self +float{});
		defNode.def(luabind::const_self -float{});
		defNode.def(luabind::const_self *float{});
		defNode.def(luabind::const_self /float{});
		defNode.def(luabind::const_self %float{});
		defNode.def(luabind::const_self ^float{});
		// defNode.def(luabind::const_self <float{});
		// defNode.def(luabind::const_self <=float{});
		defNode.def(luabind::const_self +Vector3{});
		defNode.def(luabind::const_self -Vector3{});
		defNode.def(luabind::const_self *Vector3{});
		defNode.def(luabind::const_self /Vector3{});
		defNode.def(luabind::const_self %Vector3{});
		defNode.def(luabind::const_self +unirender::Socket{});
		defNode.def(luabind::const_self -unirender::Socket{});
		defNode.def(luabind::const_self *unirender::Socket{});
		defNode.def(luabind::const_self /unirender::Socket{});
		defNode.def(luabind::const_self %unirender::Socket{});
		defNode.def(luabind::const_self ^unirender::Socket{});
		// defNode.def(luabind::const_self <unirender::Socket{});
		// defNode.def(luabind::const_self <=unirender::Socket{});
		defNode.def("GetName",static_cast<std::string(*)(unirender::NodeDesc&)>([](unirender::NodeDesc &node) -> std::string {
			return node.GetName();
		}));
		defNode.def("GetTypeName",static_cast<std::string(*)(unirender::NodeDesc&)>([](unirender::NodeDesc &node) -> std::string {
			return node.GetTypeName();
		}));
		defNode.def("IsGroupNode",static_cast<bool(*)(unirender::NodeDesc&)>([](unirender::NodeDesc &node) -> bool {
			return node.IsGroupNode();
		}));
		defNode.def("GetParent",static_cast<luabind::object(*)(lua_State*,unirender::NodeDesc&)>([](lua_State *l,unirender::NodeDesc &node) -> luabind::object {
			auto *parent = node.GetParent();
			if(parent == nullptr)
				return {};
			return luabind::object{l,parent->shared_from_this()};
		}));
		defNode.def("GetInputSocket",static_cast<luabind::object(*)(lua_State*,unirender::NodeDesc&,const std::string&)>([](lua_State *l,unirender::NodeDesc &node,const std::string &socketName) -> luabind::object {
			auto socket = node.FindInputSocket(socketName);
			if(socket.has_value() == false)
				return {};
			return luabind::object{l,*socket};
		}));
		defNode.def("GetOutputSocket",static_cast<luabind::object(*)(lua_State*,unirender::NodeDesc&,const std::string&)>([](lua_State *l,unirender::NodeDesc &node,const std::string &socketName) -> luabind::object {
			auto socket = node.FindOutputSocket(socketName);
			if(socket.has_value() == false)
				return {};
			return luabind::object{l,*socket};
		}));
		defNode.def("GetPropertySocket",static_cast<luabind::object(*)(lua_State*,unirender::NodeDesc&,const std::string&)>([](lua_State *l,unirender::NodeDesc &node,const std::string &socketName) -> luabind::object {
			auto socket = node.FindProperty(socketName);
			if(socket.has_value() == false)
				return {};
			return luabind::object{l,*socket};
		}));
		defNode.def("GetProperty",static_cast<luabind::object(*)(lua_State*,unirender::NodeDesc&,const std::string&)>([](lua_State *l,unirender::NodeDesc &node,const std::string &socketName) -> luabind::object {
			auto *desc = node.FindPropertyDesc(socketName);
			if(desc == nullptr)
				desc = node.FindInputSocketDesc(socketName);
			if(desc == nullptr || desc->dataValue.value == nullptr)
				return {};
			switch(desc->dataValue.type)
			{
			case unirender::SocketType::Bool:
				return luabind::object{l,*static_cast<unirender::STBool*>(desc->dataValue.value.get())};
			case unirender::SocketType::Float:
				return luabind::object{l,*static_cast<unirender::STFloat*>(desc->dataValue.value.get())};
			case unirender::SocketType::Int:
				return luabind::object{l,*static_cast<unirender::STInt*>(desc->dataValue.value.get())};
			case unirender::SocketType::UInt:
				return luabind::object{l,*static_cast<unirender::STUInt*>(desc->dataValue.value.get())};
			case unirender::SocketType::Color:
				return luabind::object{l,*static_cast<unirender::STColor*>(desc->dataValue.value.get())};
			case unirender::SocketType::Vector:
				return luabind::object{l,*static_cast<unirender::STVector*>(desc->dataValue.value.get())};
			case unirender::SocketType::Point:
				return luabind::object{l,*static_cast<unirender::STPoint*>(desc->dataValue.value.get())};
			case unirender::SocketType::Normal:
				return luabind::object{l,*static_cast<unirender::STNormal*>(desc->dataValue.value.get())};
			case unirender::SocketType::Point2:
				return luabind::object{l,*static_cast<unirender::STPoint2*>(desc->dataValue.value.get())};
			case unirender::SocketType::String:
				return luabind::object{l,*static_cast<unirender::STString*>(desc->dataValue.value.get())};
			case unirender::SocketType::Enum:
				return luabind::object{l,*static_cast<unirender::STEnum*>(desc->dataValue.value.get())};
			case unirender::SocketType::Transform:
				return luabind::object{l,*static_cast<unirender::STTransform*>(desc->dataValue.value.get())};
			case unirender::SocketType::FloatArray:
				return Lua::vector_to_table<unirender::STFloat>(l,*static_cast<unirender::STFloatArray*>(desc->dataValue.value.get()));
			case unirender::SocketType::ColorArray:
				return Lua::vector_to_table<unirender::STColor>(l,*static_cast<unirender::STColorArray*>(desc->dataValue.value.get()));
			}
			static_assert(umath::to_integral(unirender::SocketType::Count) == 16);
			return {};
		}));
		defNode.def("SetProperty",static_cast<void(*)(lua_State*,unirender::NodeDesc&,const std::string&,bool)>([](lua_State *l,unirender::NodeDesc &node,const std::string &propertyName,bool value) {
			node.SetProperty(propertyName,value);
		}));
		defNode.def("SetProperty",static_cast<void(*)(lua_State*,unirender::NodeDesc&,const std::string&,float)>([](lua_State *l,unirender::NodeDesc &node,const std::string &propertyName,float value) {
			node.SetProperty(propertyName,value);
		}));
		defNode.def("SetProperty",static_cast<void(*)(lua_State*,unirender::NodeDesc&,const std::string&,const Vector3&)>([](lua_State *l,unirender::NodeDesc &node,const std::string &propertyName,const Vector3 &value) {
			node.SetProperty(propertyName,value);
		}));
		defNode.def("SetProperty",static_cast<void(*)(lua_State*,unirender::NodeDesc&,const std::string&,const Vector2&)>([](lua_State *l,unirender::NodeDesc &node,const std::string &propertyName,const Vector2 &value) {
			node.SetProperty(propertyName,value);
		}));
		defNode.def("SetProperty",static_cast<void(*)(lua_State*,unirender::NodeDesc&,const std::string&,const std::string&)>([](lua_State *l,unirender::NodeDesc &node,const std::string &propertyName,const std::string &value) {
			node.SetProperty(propertyName,value);
		}));
		defNode.def("SetProperty",static_cast<void(*)(lua_State*,unirender::NodeDesc&,const std::string&,const Mat4x3&)>([](lua_State *l,unirender::NodeDesc &node,const std::string &propertyName,const Mat4x3 &value) {
			node.SetProperty(propertyName,value);
		}));
		defNode.def("SetProperty",static_cast<void(*)(lua_State*,unirender::NodeDesc&,const std::string&,luabind::table<>)>([](lua_State *l,unirender::NodeDesc &node,const std::string &propertyName,luabind::table<> value) {
			auto it = luabind::iterator{value};
			if(it == luabind::iterator{})
			{
				// Clear the property
				// TODO: This isn't pretty, do this another way?
				try
				{
					node.SetProperty(propertyName,unirender::STColorArray{});
				}
				catch(const unirender::Exception&)
				{
					node.SetProperty(propertyName,unirender::STFloatArray{});
				}
				return;
			}
			try
			{
				auto v = luabind::object_cast<unirender::STColor>(*it);
				node.SetProperty(propertyName,Lua::table_to_vector<unirender::STColor>(l,value,3));
			}
			catch(const luabind::cast_failed &e) {
				node.SetProperty(propertyName,Lua::table_to_vector<unirender::STFloat>(l,value,3));
			}
		}));
		defNode.def("GetPrimaryOutputSocket",static_cast<luabind::object(*)(lua_State*,unirender::NodeDesc&)>([](lua_State *l,unirender::NodeDesc &node) -> luabind::object {
			auto socket = node.GetPrimaryOutputSocket();
			if(socket.has_value() == false)
				return {};
			return luabind::object{l,*socket};
		}));
		defNode.def("LessThan",static_cast<unirender::Socket(*)(unirender::NodeDesc&,const unirender::NodeDesc&)>([](unirender::NodeDesc &node,const unirender::NodeDesc &nodeOther) -> unirender::Socket {
			return node < nodeOther;
		}));
		defNode.def("LessThanOrEqualTo",static_cast<unirender::Socket(*)(unirender::NodeDesc&,const unirender::NodeDesc&)>([](unirender::NodeDesc &node,const unirender::NodeDesc &nodeOther) -> unirender::Socket {
			return node <= nodeOther;
		}));
		defNode.def("GreaterThan",static_cast<unirender::Socket(*)(unirender::NodeDesc&,const unirender::NodeDesc&)>([](unirender::NodeDesc &node,const unirender::NodeDesc &nodeOther) -> unirender::Socket {
			return node > nodeOther;
		}));
		defNode.def("GreaterThanOrEqualTo",static_cast<unirender::Socket(*)(unirender::NodeDesc&,const unirender::NodeDesc&)>([](unirender::NodeDesc &node,const unirender::NodeDesc &nodeOther) -> unirender::Socket {
			return node >= nodeOther;
		}));
		modCycles[defNode];

		auto defGroupNode = luabind::class_<unirender::GroupNodeDesc,luabind::bases<unirender::NodeDesc>>("GroupNode");
		defGroupNode.def(tostring(luabind::self));
		defGroupNode.def("AddNode",static_cast<luabind::object(*)(lua_State*,unirender::GroupNodeDesc&,const std::string&)>([](lua_State *l,unirender::GroupNodeDesc &node,const std::string &typeName) -> luabind::object {
			try
			{
				auto &n = node.AddNode(typeName);
				return get_node_lua_object(l,n);
			}
			catch(const unirender::Exception &e) {std::rethrow_exception(std::current_exception());}
			return {};
		}));
		defGroupNode.def("AddNode",static_cast<luabind::object(*)(lua_State*,unirender::GroupNodeDesc&,unirender::NodeTypeId)>([](lua_State *l,unirender::GroupNodeDesc &node,unirender::NodeTypeId nodeTypeId) -> luabind::object {
			try
			{
				auto &n = node.AddNode(nodeTypeId);
				return get_node_lua_object(l,n);
			}
			catch(const unirender::Exception &e) {std::rethrow_exception(std::current_exception());}
			return {};
		}));
		defGroupNode.def("AddMathNode",static_cast<luabind::object(*)(lua_State*,unirender::GroupNodeDesc&,unirender::nodes::math::MathType,luabind::object,luabind::object)>(
			[](lua_State *l,unirender::GroupNodeDesc &node,unirender::nodes::math::MathType mathOp,luabind::object socket0,luabind::object socket1) -> luabind::object {
			try
			{
				return luabind::object{l,node.AddMathNode(get_socket(socket0),get_socket(socket1),mathOp)};
			}
			catch(const unirender::Exception &e) {std::rethrow_exception(std::current_exception());}
			return {};
		}));
		defGroupNode.def("AddVectorMathNode",static_cast<luabind::object(*)(lua_State*,unirender::GroupNodeDesc&,unirender::nodes::vector_math::MathType,luabind::object,luabind::object)>(
			[](lua_State *l,unirender::GroupNodeDesc &node,unirender::nodes::vector_math::MathType mathOp,luabind::object socket0,luabind::object socket1) -> luabind::object {
			try
			{
				auto &n = node.AddVectorMathNode(get_socket(socket0),get_socket(socket1),mathOp);
				return get_node_lua_object(l,n);
			}
			catch(const unirender::Exception &e) {std::rethrow_exception(std::current_exception());}
			return {};
		}));
		defGroupNode.def("CombineRGB",static_cast<luabind::object(*)(lua_State*,unirender::GroupNodeDesc&,luabind::object,luabind::object,luabind::object)>(
			[](lua_State *l,unirender::GroupNodeDesc &node,luabind::object r,luabind::object g,luabind::object b) -> luabind::object {
			try
			{
				return luabind::object{l,node.CombineRGB(get_socket(r),get_socket(g),get_socket(b))};
			}
			catch(const unirender::Exception &e) {std::rethrow_exception(std::current_exception());}
			return {};
		}));
		defGroupNode.def("AddTextureNode",static_cast<luabind::object(*)(lua_State*,unirender::GroupNodeDesc&,const std::string&,unirender::TextureType)>(
			[](lua_State *l,unirender::GroupNodeDesc &node,const std::string &fileName,unirender::TextureType texType) -> luabind::object {
			try
			{
				auto &n = node.AddImageTextureNode(fileName,texType);
				return get_node_lua_object(l,n);
			}
			catch(const unirender::Exception &e) {std::rethrow_exception(std::current_exception());}
			return {};
		}));
		defGroupNode.def("AddTextureNode",static_cast<luabind::object(*)(lua_State*,unirender::GroupNodeDesc&,const std::string&)>(
			[](lua_State *l,unirender::GroupNodeDesc &node,const std::string &fileName) -> luabind::object {
			try
			{
				auto &n = node.AddImageTextureNode(fileName);
				return get_node_lua_object(l,n);
			}
			catch(const unirender::Exception &e) {std::rethrow_exception(std::current_exception());}
			return {};
		}));
		defGroupNode.def("AddTextureNode",static_cast<luabind::object(*)(lua_State*,unirender::GroupNodeDesc&,const unirender::Socket&,unirender::TextureType)>(
			[](lua_State *l,unirender::GroupNodeDesc &node,const unirender::Socket &socket,unirender::TextureType texType) -> luabind::object {
			try
			{
				auto &n = node.AddImageTextureNode(socket,texType);
				return get_node_lua_object(l,n);
			}
			catch(const unirender::Exception &e) {std::rethrow_exception(std::current_exception());}
			return {};
		}));
		defGroupNode.def("AddTextureNode",static_cast<luabind::object(*)(lua_State*,unirender::GroupNodeDesc&,const unirender::Socket&)>(
			[](lua_State *l,unirender::GroupNodeDesc &node,const unirender::Socket &socket) -> luabind::object {
			try
			{
				auto &n = node.AddImageTextureNode(socket);
				return get_node_lua_object(l,n);
			}
			catch(const unirender::Exception &e) {std::rethrow_exception(std::current_exception());}
			return {};
		}));
		defGroupNode.def("AddNormalMapNode",static_cast<unirender::Socket(*)(lua_State*,unirender::GroupNodeDesc&,const std::string&,float)>(
			[](lua_State *l,unirender::GroupNodeDesc &node,const std::string &fileName,float strength) -> unirender::Socket {
			try
			{
				return node.AddNormalMapNode(fileName,{},strength);
			}
			catch(const unirender::Exception &e) {std::rethrow_exception(std::current_exception());}
			return {};
		}));
		defGroupNode.def("AddNormalMapNode",static_cast<unirender::Socket(*)(lua_State*,unirender::GroupNodeDesc&,const std::string&)>(
			[](lua_State *l,unirender::GroupNodeDesc &node,const std::string &fileName) -> unirender::Socket {
			try
			{
				return node.AddNormalMapNode(fileName,{});
			}
			catch(const unirender::Exception &e) {std::rethrow_exception(std::current_exception());}
			return {};
		}));
		defGroupNode.def("AddNormalMapNode",static_cast<unirender::Socket(*)(lua_State*,unirender::GroupNodeDesc&,const unirender::Socket&,float)>(
			[](lua_State *l,unirender::GroupNodeDesc &node,const unirender::Socket &socket,float strength) -> unirender::Socket {
			try
			{
				return node.AddNormalMapNode({},socket,strength);
			}
			catch(const unirender::Exception &e) {std::rethrow_exception(std::current_exception());}
			return {};
		}));
		defGroupNode.def("AddNormalMapNode",static_cast<unirender::Socket(*)(lua_State*,unirender::GroupNodeDesc&,const unirender::Socket&)>(
			[](lua_State *l,unirender::GroupNodeDesc &node,const unirender::Socket &socket) -> unirender::Socket {
			try
			{
				return node.AddNormalMapNode({},socket);
			}
			catch(const unirender::Exception &e) {std::rethrow_exception(std::current_exception());}
			return {};
		}));
		defGroupNode.def("AddConstantNode",static_cast<luabind::object(*)(lua_State*,unirender::GroupNodeDesc&,float)>(
			[](lua_State *l,unirender::GroupNodeDesc &node,float f) -> luabind::object {
			try
			{
				return luabind::object{l,node.AddConstantNode(f)};
			}
			catch(const unirender::Exception &e) {std::rethrow_exception(std::current_exception());}
			return {};
		}));
		defGroupNode.def("AddConstantNode",static_cast<luabind::object(*)(lua_State*,unirender::GroupNodeDesc&,const Vector3&)>(
			[](lua_State *l,unirender::GroupNodeDesc &node,const Vector3 &v) -> luabind::object {
			try
			{
				return luabind::object{l,node.AddConstantNode(v)};
			}
			catch(const unirender::Exception &e) {std::rethrow_exception(std::current_exception());}
			return {};
		}));
		defGroupNode.def("Link",static_cast<bool(*)(lua_State*,unirender::GroupNodeDesc&,luabind::object,const unirender::Socket&)>(
			[](lua_State *l,unirender::GroupNodeDesc &node,luabind::object fromSocket,const unirender::Socket &toSocket) -> bool {
			try
			{
				node.Link(get_socket(fromSocket),toSocket);
			}
			catch(const unirender::Exception &e) {std::rethrow_exception(std::current_exception());}
			return {};
		}));
		defGroupNode.def("Link",static_cast<bool(*)(lua_State*,unirender::GroupNodeDesc&,const unirender::NodeDesc&,const std::string&,const unirender::NodeDesc&,const std::string&)>(
			[](lua_State *l,unirender::GroupNodeDesc &node,const unirender::NodeDesc &nodeSrc,const std::string &socketSrc,const unirender::NodeDesc &nodeDst,const std::string &socketDst) -> bool {
			try
			{
				node.Link(const_cast<unirender::NodeDesc&>(nodeSrc),socketSrc,const_cast<unirender::NodeDesc&>(nodeDst),socketDst);
			}
			catch(const unirender::Exception &e) {std::rethrow_exception(std::current_exception());}
			return {};
		}));
		defGroupNode.def("SetPrimaryOutputSocket",static_cast<void(*)(lua_State*,unirender::GroupNodeDesc&,const std::string&)>([](lua_State *l,unirender::GroupNodeDesc &node,const std::string &name) {
			node.RegisterPrimaryOutputSocket(name);
		}));
		defGroupNode.def("SetPrimaryOutputSocket",static_cast<void(*)(lua_State*,unirender::GroupNodeDesc&,const unirender::Socket&)>([](lua_State *l,unirender::GroupNodeDesc &node,const unirender::Socket &socket) {
			std::string name;
			socket.GetNode(name);
			node.RegisterPrimaryOutputSocket(name);
		}));
		register_socket_methods<unirender::SocketIO::In>(defGroupNode);
		register_socket_methods<unirender::SocketIO::Out>(defGroupNode);
		register_socket_methods<unirender::SocketIO::None>(defGroupNode);
		modCycles[defGroupNode];

		std::unordered_map<std::string,std::string> nodeTypes {
			{"NODE_MATH",unirender::NODE_MATH},
			{"NODE_HSV",unirender::NODE_HSV},
			{"NODE_SEPARATE_XYZ",unirender::NODE_SEPARATE_XYZ},
			{"NODE_COMBINE_XYZ",unirender::NODE_COMBINE_XYZ},
			{"NODE_SEPARATE_RGB",unirender::NODE_SEPARATE_RGB},
			{"NODE_COMBINE_RGB",unirender::NODE_COMBINE_RGB},
			{"NODE_GEOMETRY",unirender::NODE_GEOMETRY},
			{"NODE_CAMERA_INFO",unirender::NODE_CAMERA_INFO},
			{"NODE_IMAGE_TEXTURE",unirender::NODE_IMAGE_TEXTURE},
			{"NODE_ENVIRONMENT_TEXTURE",unirender::NODE_ENVIRONMENT_TEXTURE},
			{"NODE_MIX_CLOSURE",unirender::NODE_MIX_CLOSURE},
			{"NODE_ADD_CLOSURE",unirender::NODE_ADD_CLOSURE},
			{"NODE_BACKGROUND_SHADER",unirender::NODE_BACKGROUND_SHADER},
			{"NODE_TEXTURE_COORDINATE",unirender::NODE_TEXTURE_COORDINATE},
			{"NODE_MAPPING",unirender::NODE_MAPPING},
			{"NODE_SCATTER_VOLUME",unirender::NODE_SCATTER_VOLUME},
			{"NODE_EMISSION",unirender::NODE_EMISSION},
			{"NODE_COLOR",unirender::NODE_COLOR},
			{"NODE_ATTRIBUTE",unirender::NODE_ATTRIBUTE},
			{"NODE_LIGHT_PATH",unirender::NODE_LIGHT_PATH},
			{"NODE_TRANSPARENT_BSDF",unirender::NODE_TRANSPARENT_BSDF},
			{"NODE_TRANSLUCENT_BSDF",unirender::NODE_TRANSLUCENT_BSDF},
			{"NODE_DIFFUSE_BSDF",unirender::NODE_DIFFUSE_BSDF},
			{"NODE_NORMAL_MAP",unirender::NODE_NORMAL_MAP},
			{"NODE_PRINCIPLED_BSDF",unirender::NODE_PRINCIPLED_BSDF},
			{"NODE_TOON_BSDF",unirender::NODE_TOON_BSDF},
			{"NODE_GLASS_BSDF",unirender::NODE_GLASS_BSDF},
			{"NODE_OUTPUT",unirender::NODE_OUTPUT},
			{"NODE_VECTOR_MATH",unirender::NODE_VECTOR_MATH},
			{"NODE_MIX",unirender::NODE_MIX},
			{"NODE_INVERT",unirender::NODE_INVERT},
			{"NODE_RGB_TO_BW",unirender::NODE_RGB_TO_BW},
			{"NODE_VECTOR_TRANSFORM",unirender::NODE_VECTOR_TRANSFORM},
			{"NODE_RGB_RAMP",unirender::NODE_RGB_RAMP},
			{"NODE_LAYER_WEIGHT",unirender::NODE_LAYER_WEIGHT}
		};
		static_assert(unirender::NODE_COUNT == 35,"Increase this number if new node types are added!");
		Lua::RegisterLibraryValues<std::string>(l.GetState(),"cycles",nodeTypes);

		Lua::RegisterLibraryValues<uint32_t>(l.GetState(),"cycles",{
			{"SUBSURFACE_SCATTERING_METHOD_CUBIC",ccl::ClosureType::CLOSURE_BSSRDF_CUBIC_ID},
			{"SUBSURFACE_SCATTERING_METHOD_GAUSSIAN",ccl::ClosureType::CLOSURE_BSSRDF_GAUSSIAN_ID},
			{"SUBSURFACE_SCATTERING_METHOD_PRINCIPLED",ccl::ClosureType::CLOSURE_BSSRDF_PRINCIPLED_ID},
			{"SUBSURFACE_SCATTERING_METHOD_BURLEY",ccl::ClosureType::CLOSURE_BSSRDF_BURLEY_ID},
			{"SUBSURFACE_SCATTERING_METHOD_RANDOM_WALK",ccl::ClosureType::CLOSURE_BSSRDF_RANDOM_WALK_ID},
			{"SUBSURFACE_SCATTERING_METHOD_PRINCIPLED_RANDOM_WALK",ccl::ClosureType::CLOSURE_BSSRDF_PRINCIPLED_RANDOM_WALK_ID}
		});

		std::unordered_map<std::string,luabind::object> nodeTypeEnums;
		luabind::object t;
		t = nodeTypeEnums[unirender::NODE_MATH] = luabind::newtable(l.GetState());
		t["IN_TYPE"] = unirender::nodes::math::IN_TYPE;
		t["IN_USE_CLAMP"] = unirender::nodes::math::IN_USE_CLAMP;
		t["IN_VALUE1"] = unirender::nodes::math::IN_VALUE1;
		t["IN_VALUE2"] = unirender::nodes::math::IN_VALUE2;
		t["IN_VALUE3"] = unirender::nodes::math::IN_VALUE3;
		t["OUT_VALUE"] = unirender::nodes::math::OUT_VALUE;

		t["TYPE_ADD"] = ccl::NodeMathType::NODE_MATH_ADD;
		t["TYPE_SUBTRACT"] = ccl::NodeMathType::NODE_MATH_SUBTRACT;
		t["TYPE_MULTIPLY"] = ccl::NodeMathType::NODE_MATH_MULTIPLY;
		t["TYPE_DIVIDE"] = ccl::NodeMathType::NODE_MATH_DIVIDE;
		t["TYPE_SINE"] = ccl::NodeMathType::NODE_MATH_SINE;
		t["TYPE_COSINE"] = ccl::NodeMathType::NODE_MATH_COSINE;
		t["TYPE_TANGENT"] = ccl::NodeMathType::NODE_MATH_TANGENT;
		t["TYPE_ARCSINE"] = ccl::NodeMathType::NODE_MATH_ARCSINE;
		t["TYPE_ARCCOSINE"] = ccl::NodeMathType::NODE_MATH_ARCCOSINE;
		t["TYPE_ARCTANGENT"] = ccl::NodeMathType::NODE_MATH_ARCTANGENT;
		t["TYPE_POWER"] = ccl::NodeMathType::NODE_MATH_POWER;
		t["TYPE_LOGARITHM"] = ccl::NodeMathType::NODE_MATH_LOGARITHM;
		t["TYPE_MINIMUM"] = ccl::NodeMathType::NODE_MATH_MINIMUM;
		t["TYPE_MAXIMUM"] = ccl::NodeMathType::NODE_MATH_MAXIMUM;
		t["TYPE_ROUND"] = ccl::NodeMathType::NODE_MATH_ROUND;
		t["TYPE_LESS_THAN"] = ccl::NodeMathType::NODE_MATH_LESS_THAN;
		t["TYPE_GREATER_THAN"] = ccl::NodeMathType::NODE_MATH_GREATER_THAN;
		t["TYPE_MODULO"] = ccl::NodeMathType::NODE_MATH_MODULO;
		t["TYPE_ABSOLUTE"] = ccl::NodeMathType::NODE_MATH_ABSOLUTE;
		t["TYPE_ARCTAN2"] = ccl::NodeMathType::NODE_MATH_ARCTAN2;
		t["TYPE_FLOOR"] = ccl::NodeMathType::NODE_MATH_FLOOR;
		t["TYPE_CEIL"] = ccl::NodeMathType::NODE_MATH_CEIL;
		t["TYPE_FRACTION"] = ccl::NodeMathType::NODE_MATH_FRACTION;
		t["TYPE_SQRT"] = ccl::NodeMathType::NODE_MATH_SQRT;
#if 0
		// These were removed from Cycles for some reason?
		t["TYPE_INV_SQRT"] = ccl::NodeMathType::NODE_MATH_INV_SQRT;
		t["TYPE_SIGN"] = ccl::NodeMathType::NODE_MATH_SIGN;
		t["TYPE_EXPONENT"] = ccl::NodeMathType::NODE_MATH_EXPONENT;
		t["TYPE_RADIANS"] = ccl::NodeMathType::NODE_MATH_RADIANS;
		t["TYPE_DEGREES"] = ccl::NodeMathType::NODE_MATH_DEGREES;
		t["TYPE_SINH"] = ccl::NodeMathType::NODE_MATH_SINH;
		t["TYPE_COSH"] = ccl::NodeMathType::NODE_MATH_COSH;
		t["TYPE_TANH"] = ccl::NodeMathType::NODE_MATH_TANH;
		t["TYPE_TRUNC"] = ccl::NodeMathType::NODE_MATH_TRUNC;
		t["TYPE_SNAP"] = ccl::NodeMathType::NODE_MATH_SNAP;
		t["TYPE_WRAP"] = ccl::NodeMathType::NODE_MATH_WRAP;
		t["TYPE_COMPARE"] = ccl::NodeMathType::NODE_MATH_COMPARE;
		t["TYPE_MULTIPLY_ADD"] = ccl::NodeMathType::NODE_MATH_MULTIPLY_ADD;
		t["TYPE_PINGPONG"] = ccl::NodeMathType::NODE_MATH_PINGPONG;
		t["TYPE_SMOOTH_MIN"] = ccl::NodeMathType::NODE_MATH_SMOOTH_MIN;
		t["TYPE_SMOOTH_MAX"] = ccl::NodeMathType::NODE_MATH_SMOOTH_MAX;
#endif
		
		t = nodeTypeEnums[unirender::NODE_HSV] = luabind::newtable(l.GetState());
		t["IN_HUE"] = unirender::nodes::hsv::IN_HUE;
		t["IN_SATURATION"] = unirender::nodes::hsv::IN_SATURATION;
		t["IN_VALUE"] = unirender::nodes::hsv::IN_VALUE;
		t["IN_FAC"] = unirender::nodes::hsv::IN_FAC;
		t["IN_COLOR"] = unirender::nodes::hsv::IN_COLOR;
		t["OUT_COLOR"] = unirender::nodes::hsv::OUT_COLOR;
		
		t = nodeTypeEnums[unirender::NODE_SEPARATE_XYZ] = luabind::newtable(l.GetState());
		t["IN_VECTOR"] = unirender::nodes::separate_xyz::IN_VECTOR;
		t["OUT_X"] = unirender::nodes::separate_xyz::OUT_X;
		t["OUT_Y"] = unirender::nodes::separate_xyz::OUT_Y;
		t["OUT_Z"] = unirender::nodes::separate_xyz::OUT_Z;
		
		t = nodeTypeEnums[unirender::NODE_COMBINE_XYZ] = luabind::newtable(l.GetState());
		t["IN_X"] = unirender::nodes::combine_xyz::IN_X;
		t["IN_Y"] = unirender::nodes::combine_xyz::IN_Y;
		t["IN_Z"] = unirender::nodes::combine_xyz::IN_Z;
		t["OUT_VECTOR"] = unirender::nodes::combine_xyz::OUT_VECTOR;
		
		t = nodeTypeEnums[unirender::NODE_SEPARATE_RGB] = luabind::newtable(l.GetState());
		t["IN_COLOR"] = unirender::nodes::separate_rgb::IN_COLOR;
		t["OUT_R"] = unirender::nodes::separate_rgb::OUT_R;
		t["OUT_G"] = unirender::nodes::separate_rgb::OUT_G;
		t["OUT_B"] = unirender::nodes::separate_rgb::OUT_B;
		
		t = nodeTypeEnums[unirender::NODE_COMBINE_RGB] = luabind::newtable(l.GetState());
		t["IN_R"] = unirender::nodes::combine_rgb::IN_R;
		t["IN_G"] = unirender::nodes::combine_rgb::IN_G;
		t["IN_B"] = unirender::nodes::combine_rgb::IN_B;
		
		t = nodeTypeEnums[unirender::NODE_GEOMETRY] = luabind::newtable(l.GetState());
		t["IN_NORMAL_OSL"] = unirender::nodes::geometry::IN_NORMAL_OSL;
		t["OUT_POSITION"] = unirender::nodes::geometry::OUT_POSITION;
		t["OUT_NORMAL"] = unirender::nodes::geometry::OUT_NORMAL;
		t["OUT_TANGENT"] = unirender::nodes::geometry::OUT_TANGENT;
		t["OUT_TRUE_NORMAL"] = unirender::nodes::geometry::OUT_TRUE_NORMAL;
		t["OUT_INCOMING"] = unirender::nodes::geometry::OUT_INCOMING;
		t["OUT_PARAMETRIC"] = unirender::nodes::geometry::OUT_PARAMETRIC;
		t["OUT_BACKFACING"] = unirender::nodes::geometry::OUT_BACKFACING;
		t["OUT_POINTINESS"] = unirender::nodes::geometry::OUT_POINTINESS;
		t["OUT_RANDOM_PER_ISLAND"] = unirender::nodes::geometry::OUT_RANDOM_PER_ISLAND;
		
		t = nodeTypeEnums[unirender::NODE_CAMERA_INFO] = luabind::newtable(l.GetState());
		t["OUT_VIEW_VECTOR"] = unirender::nodes::camera_info::OUT_VIEW_VECTOR;
		t["OUT_VIEW_Z_DEPTH"] = unirender::nodes::camera_info::OUT_VIEW_Z_DEPTH;
		t["OUT_VIEW_DISTANCE"] = unirender::nodes::camera_info::OUT_VIEW_DISTANCE;
		
		t = nodeTypeEnums[unirender::NODE_IMAGE_TEXTURE] = luabind::newtable(l.GetState());
		t["IN_FILENAME"] = unirender::nodes::image_texture::IN_FILENAME;
		t["IN_COLORSPACE"] = unirender::nodes::image_texture::IN_COLORSPACE;
		t["IN_ALPHA_TYPE"] = unirender::nodes::image_texture::IN_ALPHA_TYPE;
		t["IN_INTERPOLATION"] = unirender::nodes::image_texture::IN_INTERPOLATION;
		t["IN_EXTENSION"] = unirender::nodes::image_texture::IN_EXTENSION;
		t["IN_PROJECTION"] = unirender::nodes::image_texture::IN_PROJECTION;
		t["IN_PROJECTION_BLEND"] = unirender::nodes::image_texture::IN_PROJECTION_BLEND;
		t["IN_VECTOR"] = unirender::nodes::image_texture::IN_VECTOR;
		t["OUT_COLOR"] = unirender::nodes::image_texture::OUT_COLOR;
		t["OUT_ALPHA"] = unirender::nodes::image_texture::OUT_ALPHA;

		t["TEXTURE_TYPE_COLOR_IMAGE"] = umath::to_integral(unirender::TextureType::ColorImage);
		t["TEXTURE_TYPE_EQUIRECTANGULAR_IMAGE"] = umath::to_integral(unirender::TextureType::EquirectangularImage);
		t["TEXTURE_TYPE_NON_COLOR_IMAGE"] = umath::to_integral(unirender::TextureType::NonColorImage);
		t["TEXTURE_TYPE_NORMAL_MAP"] = umath::to_integral(unirender::TextureType::NormalMap);
		static_assert(umath::to_integral(unirender::TextureType::Count) == 4);
		
		t = nodeTypeEnums[unirender::NODE_ENVIRONMENT_TEXTURE] = luabind::newtable(l.GetState());
		t["IN_FILENAME"] = unirender::nodes::environment_texture::IN_FILENAME;
		t["IN_COLORSPACE"] = unirender::nodes::environment_texture::IN_COLORSPACE;
		t["IN_ALPHA_TYPE"] = unirender::nodes::environment_texture::IN_ALPHA_TYPE;
		t["IN_INTERPOLATION"] = unirender::nodes::environment_texture::IN_INTERPOLATION;
		t["IN_PROJECTION"] = unirender::nodes::environment_texture::IN_PROJECTION;
		t["IN_VECTOR"] = unirender::nodes::environment_texture::IN_VECTOR;
		t["OUT_COLOR"] = unirender::nodes::environment_texture::OUT_COLOR;
		t["OUT_ALPHA"] = unirender::nodes::environment_texture::OUT_ALPHA;
		
		t = nodeTypeEnums[unirender::NODE_MIX_CLOSURE] = luabind::newtable(l.GetState());
		t["IN_FAC"] = unirender::nodes::mix_closure::IN_FAC;
		t["IN_CLOSURE1"] = unirender::nodes::mix_closure::IN_CLOSURE1;
		t["IN_CLOSURE2"] = unirender::nodes::mix_closure::IN_CLOSURE2;
		t["OUT_CLOSURE"] = unirender::nodes::mix_closure::OUT_CLOSURE;
		
		t = nodeTypeEnums[unirender::NODE_ADD_CLOSURE] = luabind::newtable(l.GetState());
		t["IN_CLOSURE1"] = unirender::nodes::add_closure::IN_CLOSURE1;
		t["IN_CLOSURE2"] = unirender::nodes::add_closure::IN_CLOSURE2;
		t["OUT_CLOSURE"] = unirender::nodes::add_closure::OUT_CLOSURE;
		
		t = nodeTypeEnums[unirender::NODE_BACKGROUND_SHADER] = luabind::newtable(l.GetState());
		t["IN_COLOR"] = unirender::nodes::background_shader::IN_COLOR;
		t["IN_STRENGTH"] = unirender::nodes::background_shader::IN_STRENGTH;
		t["IN_SURFACE_MIX_WEIGHT"] = unirender::nodes::background_shader::IN_SURFACE_MIX_WEIGHT;
		t["OUT_BACKGROUND"] = unirender::nodes::background_shader::OUT_BACKGROUND;
		
		t = nodeTypeEnums[unirender::NODE_TEXTURE_COORDINATE] = luabind::newtable(l.GetState());
		t["IN_FROM_DUPLI"] = unirender::nodes::texture_coordinate::IN_FROM_DUPLI;
		t["IN_USE_TRANSFORM"] = unirender::nodes::texture_coordinate::IN_USE_TRANSFORM;
		t["IN_OB_TFM"] = unirender::nodes::texture_coordinate::IN_OB_TFM;
		t["IN_NORMAL_OSL"] = unirender::nodes::texture_coordinate::IN_NORMAL_OSL;
		t["OUT_GENERATED"] = unirender::nodes::texture_coordinate::OUT_GENERATED;
		t["OUT_NORMAL"] = unirender::nodes::texture_coordinate::OUT_NORMAL;
		t["OUT_UV"] = unirender::nodes::texture_coordinate::OUT_UV;
		t["OUT_OBJECT"] = unirender::nodes::texture_coordinate::OUT_OBJECT;
		t["OUT_CAMERA"] = unirender::nodes::texture_coordinate::OUT_CAMERA;
		t["OUT_WINDOW"] = unirender::nodes::texture_coordinate::OUT_WINDOW;
		t["OUT_REFLECTION"] = unirender::nodes::texture_coordinate::OUT_REFLECTION;
		
		t = nodeTypeEnums[unirender::NODE_MAPPING] = luabind::newtable(l.GetState());
		t["IN_TYPE"] = unirender::nodes::mapping::IN_TYPE;
		t["IN_VECTOR"] = unirender::nodes::mapping::IN_VECTOR;
		t["IN_LOCATION"] = unirender::nodes::mapping::IN_LOCATION;
		t["IN_ROTATION"] = unirender::nodes::mapping::IN_ROTATION;
		t["IN_SCALE"] = unirender::nodes::mapping::IN_SCALE;
		t["OUT_VECTOR"] = unirender::nodes::mapping::OUT_VECTOR;
		
		t = nodeTypeEnums[unirender::NODE_SCATTER_VOLUME] = luabind::newtable(l.GetState());
		t["IN_COLOR"] = unirender::nodes::scatter_volume::IN_COLOR;
		t["IN_DENSITY"] = unirender::nodes::scatter_volume::IN_DENSITY;
		t["IN_ANISOTROPY"] = unirender::nodes::scatter_volume::IN_ANISOTROPY;
		t["IN_VOLUME_MIX_WEIGHT"] = unirender::nodes::scatter_volume::IN_VOLUME_MIX_WEIGHT;
		t["OUT_VOLUME"] = unirender::nodes::scatter_volume::OUT_VOLUME;
		
		t = nodeTypeEnums[unirender::NODE_EMISSION] = luabind::newtable(l.GetState());
		t["IN_COLOR"] = unirender::nodes::emission::IN_COLOR;
		t["IN_STRENGTH"] = unirender::nodes::emission::IN_STRENGTH;
		t["IN_SURFACE_MIX_WEIGHT"] = unirender::nodes::emission::IN_SURFACE_MIX_WEIGHT;
		t["OUT_EMISSION"] = unirender::nodes::emission::OUT_EMISSION;
		
		t = nodeTypeEnums[unirender::NODE_COLOR] = luabind::newtable(l.GetState());
		t["IN_VALUE"] = unirender::nodes::color::IN_VALUE;
		t["OUT_COLOR"] = unirender::nodes::color::OUT_COLOR;
		
		t = nodeTypeEnums[unirender::NODE_ATTRIBUTE] = luabind::newtable(l.GetState());
		t["IN_ATTRIBUTE"] = unirender::nodes::attribute::IN_ATTRIBUTE;
		t["OUT_COLOR"] = unirender::nodes::attribute::OUT_COLOR;
		t["OUT_VECTOR"] = unirender::nodes::attribute::OUT_VECTOR;
		t["OUT_FAC"] = unirender::nodes::attribute::OUT_FAC;
		
		t = nodeTypeEnums[unirender::NODE_LIGHT_PATH] = luabind::newtable(l.GetState());
		t["OUT_IS_CAMERA_RAY"] = unirender::nodes::light_path::OUT_IS_CAMERA_RAY;
		t["OUT_IS_SHADOW_RAY"] = unirender::nodes::light_path::OUT_IS_SHADOW_RAY;
		t["OUT_IS_DIFFUSE_RAY"] = unirender::nodes::light_path::OUT_IS_DIFFUSE_RAY;
		t["OUT_IS_GLOSSY_RAY"] = unirender::nodes::light_path::OUT_IS_GLOSSY_RAY;
		t["OUT_IS_SINGULAR_RAY"] = unirender::nodes::light_path::OUT_IS_SINGULAR_RAY;
		t["OUT_IS_REFLECTION_RAY"] = unirender::nodes::light_path::OUT_IS_REFLECTION_RAY;
		t["OUT_IS_TRANSMISSION_RAY"] = unirender::nodes::light_path::OUT_IS_TRANSMISSION_RAY;
		t["OUT_IS_VOLUME_SCATTER_RAY"] = unirender::nodes::light_path::OUT_IS_VOLUME_SCATTER_RAY;
		t["OUT_RAY_LENGTH"] = unirender::nodes::light_path::OUT_RAY_LENGTH;
		t["OUT_RAY_DEPTH"] = unirender::nodes::light_path::OUT_RAY_DEPTH;
		t["OUT_DIFFUSE_DEPTH"] = unirender::nodes::light_path::OUT_DIFFUSE_DEPTH;
		t["OUT_GLOSSY_DEPTH"] = unirender::nodes::light_path::OUT_GLOSSY_DEPTH;
		t["OUT_TRANSPARENT_DEPTH"] = unirender::nodes::light_path::OUT_TRANSPARENT_DEPTH;
		t["OUT_TRANSMISSION_DEPTH"] = unirender::nodes::light_path::OUT_TRANSMISSION_DEPTH;
		
		t = nodeTypeEnums[unirender::NODE_TRANSPARENT_BSDF] = luabind::newtable(l.GetState());
		t["IN_COLOR"] = unirender::nodes::transparent_bsdf::IN_COLOR;
		t["IN_SURFACE_MIX_WEIGHT"] = unirender::nodes::transparent_bsdf::IN_SURFACE_MIX_WEIGHT;
		t["OUT_BSDF"] = unirender::nodes::transparent_bsdf::OUT_BSDF;
		
		t = nodeTypeEnums[unirender::NODE_TRANSLUCENT_BSDF] = luabind::newtable(l.GetState());
		t["IN_COLOR"] = unirender::nodes::translucent_bsdf::IN_COLOR;
		t["IN_NORMAL"] = unirender::nodes::translucent_bsdf::IN_NORMAL;
		t["IN_SURFACE_MIX_WEIGHT"] = unirender::nodes::translucent_bsdf::IN_SURFACE_MIX_WEIGHT;
		t["OUT_BSDF"] = unirender::nodes::translucent_bsdf::OUT_BSDF;
		
		t = nodeTypeEnums[unirender::NODE_DIFFUSE_BSDF] = luabind::newtable(l.GetState());
		t["IN_COLOR"] = unirender::nodes::diffuse_bsdf::IN_COLOR;
		t["IN_NORMAL"] = unirender::nodes::diffuse_bsdf::IN_NORMAL;
		t["IN_SURFACE_MIX_WEIGHT"] = unirender::nodes::diffuse_bsdf::IN_SURFACE_MIX_WEIGHT;
		t["IN_ROUGHNESS"] = unirender::nodes::diffuse_bsdf::IN_ROUGHNESS;
		t["OUT_BSDF"] = unirender::nodes::diffuse_bsdf::OUT_BSDF;
		
		t = nodeTypeEnums[unirender::NODE_NORMAL_MAP] = luabind::newtable(l.GetState());
		t["IN_SPACE"] = unirender::nodes::normal_map::IN_SPACE;
		t["IN_ATTRIBUTE"] = unirender::nodes::normal_map::IN_ATTRIBUTE;
		t["IN_NORMAL_OSL"] = unirender::nodes::normal_map::IN_NORMAL_OSL;
		t["IN_STRENGTH"] = unirender::nodes::normal_map::IN_STRENGTH;
		t["IN_COLOR"] = unirender::nodes::normal_map::IN_COLOR;
		t["OUT_NORMAL"] = unirender::nodes::normal_map::OUT_NORMAL;
		
		t = nodeTypeEnums[unirender::NODE_PRINCIPLED_BSDF] = luabind::newtable(l.GetState());
		t["IN_DISTRIBUTION"] = unirender::nodes::principled_bsdf::IN_DISTRIBUTION;
		t["IN_SUBSURFACE_METHOD"] = unirender::nodes::principled_bsdf::IN_SUBSURFACE_METHOD;
		t["IN_BASE_COLOR"] = unirender::nodes::principled_bsdf::IN_BASE_COLOR;
		t["IN_SUBSURFACE_COLOR"] = unirender::nodes::principled_bsdf::IN_SUBSURFACE_COLOR;
		t["IN_METALLIC"] = unirender::nodes::principled_bsdf::IN_METALLIC;
		t["IN_SUBSURFACE"] = unirender::nodes::principled_bsdf::IN_SUBSURFACE;
		t["IN_SUBSURFACE_RADIUS"] = unirender::nodes::principled_bsdf::IN_SUBSURFACE_RADIUS;
		t["IN_SPECULAR"] = unirender::nodes::principled_bsdf::IN_SPECULAR;
		t["IN_ROUGHNESS"] = unirender::nodes::principled_bsdf::IN_ROUGHNESS;
		t["IN_SPECULAR_TINT"] = unirender::nodes::principled_bsdf::IN_SPECULAR_TINT;
		t["IN_ANISOTROPIC"] = unirender::nodes::principled_bsdf::IN_ANISOTROPIC;
		t["IN_SHEEN"] = unirender::nodes::principled_bsdf::IN_SHEEN;
		t["IN_SHEEN_TINT"] = unirender::nodes::principled_bsdf::IN_SHEEN_TINT;
		t["IN_CLEARCOAT"] = unirender::nodes::principled_bsdf::IN_CLEARCOAT;
		t["IN_CLEARCOAT_ROUGHNESS"] = unirender::nodes::principled_bsdf::IN_CLEARCOAT_ROUGHNESS;
		t["IN_IOR"] = unirender::nodes::principled_bsdf::IN_IOR;
		t["IN_TRANSMISSION"] = unirender::nodes::principled_bsdf::IN_TRANSMISSION;
		t["IN_TRANSMISSION_ROUGHNESS"] = unirender::nodes::principled_bsdf::IN_TRANSMISSION_ROUGHNESS;
		t["IN_ANISOTROPIC_ROTATION"] = unirender::nodes::principled_bsdf::IN_ANISOTROPIC_ROTATION;
		t["IN_EMISSION"] = unirender::nodes::principled_bsdf::IN_EMISSION;
		t["IN_ALPHA"] = unirender::nodes::principled_bsdf::IN_ALPHA;
		t["IN_NORMAL"] = unirender::nodes::principled_bsdf::IN_NORMAL;
		t["IN_CLEARCOAT_NORMAL"] = unirender::nodes::principled_bsdf::IN_CLEARCOAT_NORMAL;
		t["IN_TANGENT"] = unirender::nodes::principled_bsdf::IN_TANGENT;
		t["IN_SURFACE_MIX_WEIGHT"] = unirender::nodes::principled_bsdf::IN_SURFACE_MIX_WEIGHT;
		t["OUT_BSDF"] = unirender::nodes::principled_bsdf::OUT_BSDF;
		
		t = nodeTypeEnums[unirender::NODE_TOON_BSDF] = luabind::newtable(l.GetState());
		t["IN_COMPONENT"] = unirender::nodes::toon_bsdf::IN_COMPONENT;
		t["IN_COLOR"] = unirender::nodes::toon_bsdf::IN_COLOR;
		t["IN_NORMAL"] = unirender::nodes::toon_bsdf::IN_NORMAL;
		t["IN_SURFACE_MIX_WEIGHT"] = unirender::nodes::toon_bsdf::IN_SURFACE_MIX_WEIGHT;
		t["IN_SIZE"] = unirender::nodes::toon_bsdf::IN_SIZE;
		t["IN_SMOOTH"] = unirender::nodes::toon_bsdf::IN_SMOOTH;
		t["OUT_BSDF"] = unirender::nodes::toon_bsdf::OUT_BSDF;
		
		t = nodeTypeEnums[unirender::NODE_GLASS_BSDF] = luabind::newtable(l.GetState());
		t["IN_DISTRIBUTION"] = unirender::nodes::glass_bsdf::IN_DISTRIBUTION;
		t["IN_COLOR"] = unirender::nodes::glass_bsdf::IN_COLOR;
		t["IN_NORMAL"] = unirender::nodes::glass_bsdf::IN_NORMAL;
		t["IN_SURFACE_MIX_WEIGHT"] = unirender::nodes::glass_bsdf::IN_SURFACE_MIX_WEIGHT;
		t["IN_ROUGHNESS"] = unirender::nodes::glass_bsdf::IN_ROUGHNESS;
		t["IN_IOR"] = unirender::nodes::glass_bsdf::IN_IOR;
		t["OUT_BSDF"] = unirender::nodes::glass_bsdf::OUT_BSDF;
		
		t["DISTRIBUTION_SHARP"] = ccl::ClosureType::CLOSURE_BSDF_SHARP_GLASS_ID;
		t["DISTRIBUTION_BECKMANN"] = ccl::ClosureType::CLOSURE_BSDF_MICROFACET_BECKMANN_GLASS_ID;
		t["DISTRIBUTION_GGX"] = ccl::ClosureType::CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID;
		t["DISTRIBUTION_MULTISCATTER_GGX"] = ccl::ClosureType::CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID;
		
		t = nodeTypeEnums[unirender::NODE_OUTPUT] = luabind::newtable(l.GetState());
		t["IN_SURFACE"] = unirender::nodes::output::IN_SURFACE;
		t["IN_VOLUME"] = unirender::nodes::output::IN_VOLUME;
		t["IN_DISPLACEMENT"] = unirender::nodes::output::IN_DISPLACEMENT;
		t["IN_NORMAL"] = unirender::nodes::output::IN_NORMAL;

		t = nodeTypeEnums[unirender::NODE_VECTOR_MATH] = luabind::newtable(l.GetState());
		t["IN_TYPE"] = unirender::nodes::vector_math::IN_TYPE;
		t["IN_VECTOR1"] = unirender::nodes::vector_math::IN_VECTOR1;
		t["IN_VECTOR2"] = unirender::nodes::vector_math::IN_VECTOR2;
		t["IN_SCALE"] = unirender::nodes::vector_math::IN_SCALE;
		t["OUT_VALUE"] = unirender::nodes::vector_math::OUT_VALUE;
		t["OUT_VECTOR"] = unirender::nodes::vector_math::OUT_VECTOR;

		t["TYPE_ADD"] = ccl::NodeVectorMathType::NODE_VECTOR_MATH_ADD;
		t["TYPE_SUBTRACT"] = ccl::NodeVectorMathType::NODE_VECTOR_MATH_SUBTRACT;
		t["TYPE_MULTIPLY"] = ccl::NodeVectorMathType::NODE_VECTOR_MATH_MULTIPLY;
		t["TYPE_DIVIDE"] = ccl::NodeVectorMathType::NODE_VECTOR_MATH_DIVIDE;
		t["TYPE_CROSS_PRODUCT"] = ccl::NodeVectorMathType::NODE_VECTOR_MATH_CROSS_PRODUCT;
		t["TYPE_PROJECT"] = ccl::NodeVectorMathType::NODE_VECTOR_MATH_PROJECT;
		t["TYPE_REFLECT"] = ccl::NodeVectorMathType::NODE_VECTOR_MATH_REFLECT;
		t["TYPE_DOT_PRODUCT"] = ccl::NodeVectorMathType::NODE_VECTOR_MATH_DOT_PRODUCT;
		t["TYPE_DISTANCE"] = ccl::NodeVectorMathType::NODE_VECTOR_MATH_DISTANCE;
		t["TYPE_LENGTH"] = ccl::NodeVectorMathType::NODE_VECTOR_MATH_LENGTH;
		t["TYPE_SCALE"] = ccl::NodeVectorMathType::NODE_VECTOR_MATH_SCALE;
		t["TYPE_NORMALIZE"] = ccl::NodeVectorMathType::NODE_VECTOR_MATH_NORMALIZE;
		t["TYPE_SNAP"] = ccl::NodeVectorMathType::NODE_VECTOR_MATH_SNAP;
		t["TYPE_FLOOR"] = ccl::NodeVectorMathType::NODE_VECTOR_MATH_FLOOR;
		t["TYPE_CEIL"] = ccl::NodeVectorMathType::NODE_VECTOR_MATH_CEIL;
		t["TYPE_MODULO"] = ccl::NodeVectorMathType::NODE_VECTOR_MATH_MODULO;
		t["TYPE_FRACTION"] = ccl::NodeVectorMathType::NODE_VECTOR_MATH_FRACTION;
		t["TYPE_ABSOLUTE"] = ccl::NodeVectorMathType::NODE_VECTOR_MATH_ABSOLUTE;
		t["TYPE_MINIMUM"] = ccl::NodeVectorMathType::NODE_VECTOR_MATH_MINIMUM;
		t["TYPE_MAXIMUM"] = ccl::NodeVectorMathType::NODE_VECTOR_MATH_MAXIMUM;
		
		t = nodeTypeEnums[unirender::NODE_MIX] = luabind::newtable(l.GetState());
		t["IN_TYPE"] = unirender::nodes::mix::IN_TYPE;
		t["IN_USE_CLAMP"] = unirender::nodes::mix::IN_USE_CLAMP;
		t["IN_FAC"] = unirender::nodes::mix::IN_FAC;
		t["IN_COLOR1"] = unirender::nodes::mix::IN_COLOR1;
		t["IN_COLOR2"] = unirender::nodes::mix::IN_COLOR2;
		t["OUT_COLOR"] = unirender::nodes::mix::OUT_COLOR;
		
		t = nodeTypeEnums[unirender::NODE_RGB_TO_BW] = luabind::newtable(l.GetState());
		t["IN_COLOR"] = unirender::nodes::rgb_to_bw::IN_COLOR;
		t["OUT_VAL"] = unirender::nodes::rgb_to_bw::OUT_VAL;
		
		t = nodeTypeEnums[unirender::NODE_INVERT] = luabind::newtable(l.GetState());
		t["IN_COLOR"] = unirender::nodes::invert::IN_COLOR;
		t["IN_FAC"] = unirender::nodes::invert::IN_FAC;
		t["OUT_COLOR"] = unirender::nodes::invert::OUT_COLOR;
		
		t = nodeTypeEnums[unirender::NODE_VECTOR_TRANSFORM] = luabind::newtable(l.GetState());
		t["IN_TYPE"] = unirender::nodes::vector_transform::IN_TYPE;
		t["IN_CONVERT_FROM"] = unirender::nodes::vector_transform::IN_CONVERT_FROM;
		t["IN_CONVERT_TO"] = unirender::nodes::vector_transform::IN_CONVERT_TO;
		t["IN_VECTOR"] = unirender::nodes::vector_transform::IN_VECTOR;
		t["OUT_VECTOR"] = unirender::nodes::vector_transform::OUT_VECTOR;

		t["VECTOR_TRANSFORM_TYPE_VECTOR"] = ccl::NodeVectorTransformType::NODE_VECTOR_TRANSFORM_TYPE_VECTOR;
		t["VECTOR_TRANSFORM_TYPE_POINT"] = ccl::NodeVectorTransformType::NODE_VECTOR_TRANSFORM_TYPE_POINT;
		t["VECTOR_TRANSFORM_TYPE_NORMAL"] = ccl::NodeVectorTransformType::NODE_VECTOR_TRANSFORM_TYPE_NORMAL;
		
		t["VECTOR_TRANSFORM_CONVERT_SPACE_WORLD"] = ccl::NodeVectorTransformConvertSpace::NODE_VECTOR_TRANSFORM_CONVERT_SPACE_WORLD;
		t["VECTOR_TRANSFORM_CONVERT_SPACE_OBJECT"] = ccl::NodeVectorTransformConvertSpace::NODE_VECTOR_TRANSFORM_CONVERT_SPACE_OBJECT;
		t["VECTOR_TRANSFORM_CONVERT_SPACE_CAMERA"] = ccl::NodeVectorTransformConvertSpace::NODE_VECTOR_TRANSFORM_CONVERT_SPACE_CAMERA;

		t = nodeTypeEnums[unirender::NODE_RGB_RAMP] = luabind::newtable(l.GetState());
		t["IN_RAMP"] = unirender::nodes::rgb_ramp::IN_RAMP;
		t["IN_RAMP_ALPHA"] = unirender::nodes::rgb_ramp::IN_RAMP_ALPHA;
		t["IN_INTERPOLATE"] = unirender::nodes::rgb_ramp::IN_INTERPOLATE;
		t["IN_FAC"] = unirender::nodes::rgb_ramp::IN_FAC;
		t["OUT_COLOR"] = unirender::nodes::rgb_ramp::OUT_COLOR;
		t["OUT_ALPHA"] = unirender::nodes::rgb_ramp::OUT_ALPHA;
		
		t = nodeTypeEnums[unirender::NODE_LAYER_WEIGHT] = luabind::newtable(l.GetState());
		t["IN_NORMAL"] = unirender::nodes::layer_weight::IN_NORMAL;
		t["IN_BLEND"] = unirender::nodes::layer_weight::IN_BLEND;
		t["OUT_FRESNEL"] = unirender::nodes::layer_weight::OUT_FRESNEL;
		t["OUT_FACING"] = unirender::nodes::layer_weight::OUT_FACING;

		static_assert(unirender::NODE_COUNT == 35,"Increase this number if new node types are added!");
		Lua::RegisterLibraryValues<luabind::object>(l.GetState(),"cycles.Node",nodeTypeEnums);

		auto defShader = luabind::class_<pragma::modules::cycles::LuaShader>("Shader");
		defShader.def(luabind::constructor<>());
		defShader.def("InitializeCombinedPass",&pragma::modules::cycles::LuaShader::Lua_InitializeCombinedPass,&pragma::modules::cycles::LuaShader::Lua_default_InitializeCombinedPass);
		defShader.def("InitializeAlbedoPass",&pragma::modules::cycles::LuaShader::Lua_InitializeAlbedoPass,&pragma::modules::cycles::LuaShader::Lua_default_InitializeAlbedoPass);
		defShader.def("InitializeNormalPass",&pragma::modules::cycles::LuaShader::Lua_InitializeNormalPass,&pragma::modules::cycles::LuaShader::Lua_default_InitializeNormalPass);
		defShader.def("InitializeDepthPass",&pragma::modules::cycles::LuaShader::Lua_InitializeDepthPass,&pragma::modules::cycles::LuaShader::Lua_default_InitializeDepthPass);
		defShader.def("GetEntity",static_cast<luabind::object(*)(lua_State*,pragma::modules::cycles::LuaShader&)>([](lua_State *l,pragma::modules::cycles::LuaShader &shader) -> luabind::object {
			auto *ent = shader.GetEntity();
			if(ent == nullptr)
				return {};
			return *ent->GetLuaObject();
		}));
		defShader.def("GetMaterial",static_cast<Material*(*)(lua_State*,pragma::modules::cycles::LuaShader&)>([](lua_State *l,pragma::modules::cycles::LuaShader &shader) -> Material* {
			return shader.GetMaterial();
		}));
		defShader.def("PrepareTexture",static_cast<luabind::object(*)(lua_State*,pragma::modules::cycles::LuaShader&,const std::string&)>([](lua_State *l,pragma::modules::cycles::LuaShader &shader,const std::string &texturePath) -> luabind::object {
			auto res = pragma::modules::cycles::prepare_texture(texturePath);
			return res.has_value() ? luabind::object{l,*res} : luabind::object{};
		}));
		defShader.def("PrepareTexture",static_cast<luabind::object(*)(lua_State*,pragma::modules::cycles::LuaShader&,const std::string&,const std::string&)>([](lua_State *l,pragma::modules::cycles::LuaShader &shader,const std::string &texturePath,const std::string &defaultTexture) -> luabind::object {
			auto res = pragma::modules::cycles::prepare_texture(texturePath,defaultTexture);
			return res.has_value() ? luabind::object{l,*res} : luabind::object{};
		}));
		modCycles[defShader];

		auto defSocket = luabind::class_<unirender::Socket>("Socket");
		defSocket.def(tostring(luabind::self));
		defSocket.def(luabind::constructor<>());
		defSocket.def(luabind::constructor<float>());
		defSocket.def(luabind::constructor<Vector3>());
		defSocket.def(-luabind::const_self);
		defSocket.def(luabind::const_self +float{});
		defSocket.def(luabind::const_self -float{});
		defSocket.def(luabind::const_self *float{});
		defSocket.def(luabind::const_self /float{});
		defSocket.def(luabind::const_self %float{});
		defSocket.def(luabind::const_self ^float{});
		// defSocket.def(luabind::const_self <float{});
		// defSocket.def(luabind::const_self <=float{});
		defSocket.def(float() +luabind::const_self);
		defSocket.def(float() -luabind::const_self);
		defSocket.def(float() *luabind::const_self);
		defSocket.def(float() /luabind::const_self);
		defSocket.def(float() %luabind::const_self);
		defSocket.def(float() ^luabind::const_self);
		// defSocket.def(float() <luabind::const_self);
		// defSocket.def(float() <=luabind::const_self);
		defSocket.def(luabind::const_self +Vector3{});
		defSocket.def(luabind::const_self -Vector3{});
		defSocket.def(luabind::const_self *Vector3{});
		defSocket.def(luabind::const_self /Vector3{});
		defSocket.def(luabind::const_self %Vector3{});
		defSocket.def(Vector3() +luabind::const_self);
		defSocket.def(Vector3() -luabind::const_self);
		defSocket.def(Vector3() *luabind::const_self);
		defSocket.def(Vector3() /luabind::const_self);
		defSocket.def(Vector3() %luabind::const_self);
		defSocket.def(luabind::const_self +unirender::Socket{});
		defSocket.def(luabind::const_self -unirender::Socket{});
		defSocket.def(luabind::const_self *unirender::Socket{});
		defSocket.def(luabind::const_self /unirender::Socket{});
		defSocket.def(luabind::const_self %unirender::Socket{});
		defSocket.def(luabind::const_self ^unirender::Socket{});
		defSocket.property("x",get_vector_socket_component<VectorChannel::X>,set_vector_socket_component<VectorChannel::X>);
		defSocket.property("y",get_vector_socket_component<VectorChannel::Y>,set_vector_socket_component<VectorChannel::Y>);
		defSocket.property("z",get_vector_socket_component<VectorChannel::Z>,set_vector_socket_component<VectorChannel::Z>);
		defSocket.property("r",get_vector_socket_component<VectorChannel::X>,set_vector_socket_component<VectorChannel::X>);
		defSocket.property("g",get_vector_socket_component<VectorChannel::Y>,set_vector_socket_component<VectorChannel::Y>);
		defSocket.property("b",get_vector_socket_component<VectorChannel::Z>,set_vector_socket_component<VectorChannel::Z>);
		// defSocket.def(luabind::const_self <unirender::Socket{});
		// defSocket.def(luabind::const_self <=unirender::Socket{});
		defSocket.add_static_constant("TYPE_BOOL",umath::to_integral(unirender::SocketType::Bool));
		defSocket.add_static_constant("TYPE_FLOAT",umath::to_integral(unirender::SocketType::Float));
		defSocket.add_static_constant("TYPE_INT",umath::to_integral(unirender::SocketType::Int));
		defSocket.add_static_constant("TYPE_UINT",umath::to_integral(unirender::SocketType::UInt));
		defSocket.add_static_constant("TYPE_COLOR",umath::to_integral(unirender::SocketType::Color));
		defSocket.add_static_constant("TYPE_VECTOR",umath::to_integral(unirender::SocketType::Vector));
		defSocket.add_static_constant("TYPE_POINT",umath::to_integral(unirender::SocketType::Point));
		defSocket.add_static_constant("TYPE_NORMAL",umath::to_integral(unirender::SocketType::Normal));
		defSocket.add_static_constant("TYPE_POINT2",umath::to_integral(unirender::SocketType::Point2));
		defSocket.add_static_constant("TYPE_CLOSURE",umath::to_integral(unirender::SocketType::Closure));
		defSocket.add_static_constant("TYPE_STRING",umath::to_integral(unirender::SocketType::String));
		defSocket.add_static_constant("TYPE_ENUM",umath::to_integral(unirender::SocketType::Enum));
		defSocket.add_static_constant("TYPE_TRANSFORM",umath::to_integral(unirender::SocketType::Transform));
		defSocket.add_static_constant("TYPE_NODE",umath::to_integral(unirender::SocketType::Node));
		defSocket.add_static_constant("TYPE_FLOAT_ARRAY",umath::to_integral(unirender::SocketType::FloatArray));
		defSocket.add_static_constant("TYPE_COLOR_ARRAY",umath::to_integral(unirender::SocketType::ColorArray));
		defSocket.add_static_constant("TYPE_COUNT",umath::to_integral(unirender::SocketType::Count));
		static_assert(umath::to_integral(unirender::SocketType::Count) == 16);
		defSocket.def(tostring(luabind::self));
		defSocket.def("GetNode",static_cast<luabind::object(*)(lua_State*,unirender::Socket&)>([](lua_State *l,unirender::Socket &socket) -> luabind::object {
			auto *node = socket.GetNode();
			if(node == nullptr)
				return {};
			return luabind::object{l,node};
		}));
		defSocket.def("GetSocketName",static_cast<luabind::object(*)(lua_State*,unirender::Socket&)>([](lua_State *l,unirender::Socket &socket) -> luabind::object {
			std::string socketName;
			auto *node = socket.GetNode(socketName);
			if(node == nullptr)
				return {};
			return luabind::object{l,socketName};
		}));
		defSocket.def("IsConcreteValue",static_cast<bool(*)(lua_State*,unirender::Socket&)>([](lua_State *l,unirender::Socket &socket) -> bool {
			return socket.IsConcreteValue();
		}));
		defSocket.def("IsNodeSocket",static_cast<bool(*)(lua_State*,unirender::Socket&)>([](lua_State *l,unirender::Socket &socket) -> bool {
			return socket.IsNodeSocket();
		}));
		defSocket.def("IsOutputSocket",static_cast<bool(*)(lua_State*,unirender::Socket&)>([](lua_State *l,unirender::Socket &socket) -> bool {
			return socket.IsOutputSocket();
		}));
		defSocket.def("Link",static_cast<void(*)(lua_State*,unirender::Socket&,const unirender::Socket&)>(
			[](lua_State *l,unirender::Socket &socket,const unirender::Socket &toSocket) {
			try {socket.Link(toSocket);}
			catch(const unirender::Exception &e) {std::rethrow_exception(std::current_exception());}
		}));
		defSocket.def("Link",static_cast<void(*)(lua_State*,unirender::Socket&,const unirender::NodeDesc&,const std::string&)>(
			[](lua_State *l,unirender::Socket &socket,const unirender::NodeDesc &toNode,const std::string &socketName) {
			try {socket.Link(const_cast<unirender::NodeDesc&>(toNode).GetInputSocket(socketName));}
			catch(const unirender::Exception &e) {std::rethrow_exception(std::current_exception());}
		}));
		defSocket.def("Link",static_cast<void(*)(lua_State*,unirender::Socket&,float)>(
			[](lua_State *l,unirender::Socket &socket,float f) {
			try {socket.Link(unirender::Socket{f});}
			catch(const unirender::Exception &e) {std::rethrow_exception(std::current_exception());}
		}));
		defSocket.def("Link",static_cast<void(*)(lua_State*,unirender::Socket&,const Vector3&)>(
			[](lua_State *l,unirender::Socket &socket,const Vector3 &v) {
			try {socket.Link(unirender::Socket{v});}
			catch(const unirender::Exception &e) {std::rethrow_exception(std::current_exception());}
		}));
		defSocket.def("LessThan",static_cast<unirender::Socket(*)(unirender::Socket&,luabind::object)>([](unirender::Socket &socket,luabind::object socketOther) -> unirender::Socket {
			return socket < get_socket(socketOther);
		}));
		defSocket.def("LessThanOrEqualTo",static_cast<unirender::Socket(*)(unirender::Socket&,luabind::object)>([](unirender::Socket &socket,luabind::object socketOther) -> unirender::Socket {
			return socket <= get_socket(socketOther);
		}));
		defSocket.def("GreaterThan",static_cast<unirender::Socket(*)(unirender::Socket&,luabind::object)>([](unirender::Socket &socket,luabind::object socketOther) -> unirender::Socket {
			return socket > get_socket(socketOther);
		}));
		defSocket.def("GreaterThanOrEqualTo",static_cast<unirender::Socket(*)(unirender::Socket&,luabind::object)>([](unirender::Socket &socket,luabind::object socketOther) -> unirender::Socket {
			return socket >= get_socket(socketOther);
		}));
		defSocket.def("Mix",static_cast<unirender::Socket(*)(lua_State*,unirender::Socket&,luabind::object,luabind::object)>([](lua_State *l,unirender::Socket &socket,luabind::object oSocketOther,luabind::object oFac) -> unirender::Socket {
			auto socketOther = get_socket(oSocketOther);
			auto fac = get_socket(oFac);
			auto &parent = get_socket_node(l,std::vector<std::reference_wrapper<unirender::Socket>>{socket,socketOther,fac});
			return parent.Mix(socket,socketOther,fac);
		}));
		defSocket.def("Mix",static_cast<unirender::Socket(*)(lua_State*,unirender::Socket&,luabind::object,luabind::object,unirender::nodes::mix::Mix)>([](lua_State *l,unirender::Socket &socket,luabind::object oSocketOther,luabind::object oFac,unirender::nodes::mix::Mix mixType) -> unirender::Socket {
			auto socketOther = get_socket(oSocketOther);
			auto fac = get_socket(oFac);
			auto &parent = get_socket_node(l,std::vector<std::reference_wrapper<unirender::Socket>>{socket,socketOther,fac});
			return parent.Mix(socket,socketOther,fac,mixType);
		}));
		defSocket.def("Invert",static_cast<unirender::Socket(*)(lua_State*,unirender::Socket&,luabind::object)>([](lua_State *l,unirender::Socket &socket,luabind::object oFac) -> unirender::Socket {
			auto fac = get_socket(oFac);
			auto &parent = get_socket_node(l,std::vector<std::reference_wrapper<unirender::Socket>>{socket,fac});
			return parent.Invert(socket,fac);
		}));
		defSocket.def("Invert",static_cast<unirender::Socket(*)(lua_State*,unirender::Socket&)>([](lua_State *l,unirender::Socket &socket) -> unirender::Socket {
			auto &parent = get_socket_node(l,socket);
			return parent.Invert(socket);
		}));
		defSocket.def("ToGrayScale",static_cast<unirender::Socket(*)(lua_State*,unirender::Socket&)>([](lua_State *l,unirender::Socket &socket) -> unirender::Socket {
			auto &parent = get_socket_node(l,socket);
			return parent.ToGrayScale(socket);
		}));

		// Math operations
		defSocket.def("Sin",socket_math_op_unary<unirender::nodes::math::MathType::Sine>);
		defSocket.def("Cos",socket_math_op_unary<unirender::nodes::math::MathType::Cosine>);
		defSocket.def("Tan",socket_math_op_unary<unirender::nodes::math::MathType::Tangent>);
		defSocket.def("Asin",socket_math_op_unary<unirender::nodes::math::MathType::ArcSine>);
		defSocket.def("Acos",socket_math_op_unary<unirender::nodes::math::MathType::ArcCosine>);
		defSocket.def("Atan",socket_math_op_unary<unirender::nodes::math::MathType::ArcTangent>);
		defSocket.def("Log",socket_math_op<unirender::nodes::math::MathType::Logarithm>);
		defSocket.def("Min",static_cast<unirender::Socket(*)(lua_State*,unirender::Socket&,luabind::object)>([](lua_State *l,unirender::Socket &socket,luabind::object socketOther) -> unirender::Socket {
			if(unirender::is_vector_type(socket.GetType()))
				return socket_vector_op<unirender::nodes::vector_math::MathType::Minimum>(l,socket,socketOther);
			return socket_math_op<unirender::nodes::math::MathType::Minimum>(l,socket,socketOther);
		}));
		defSocket.def("Max",static_cast<unirender::Socket(*)(lua_State*,unirender::Socket&,luabind::object)>([](lua_State *l,unirender::Socket &socket,luabind::object socketOther) -> unirender::Socket {
			if(unirender::is_vector_type(socket.GetType()))
				return socket_vector_op<unirender::nodes::vector_math::MathType::Maximum>(l,socket,socketOther);
			return socket_math_op<unirender::nodes::math::MathType::Maximum>(l,socket,socketOther);
		}));
		defSocket.def("Round",socket_math_op_unary<unirender::nodes::math::MathType::Round>);
		defSocket.def("Atan2",socket_math_op<unirender::nodes::math::MathType::ArcTan2>);
		defSocket.def("Floor",static_cast<unirender::Socket(*)(lua_State*,unirender::Socket&)>([](lua_State *l,unirender::Socket &socket) -> unirender::Socket {
			if(unirender::is_vector_type(socket.GetType()))
				return socket_vector_op_unary<unirender::nodes::vector_math::MathType::Floor>(l,socket);
			return socket_math_op_unary<unirender::nodes::math::MathType::Floor>(l,socket);
		}));
		defSocket.def("Ceil",static_cast<unirender::Socket(*)(lua_State*,unirender::Socket&)>([](lua_State *l,unirender::Socket &socket) -> unirender::Socket {
			if(unirender::is_vector_type(socket.GetType()))
				return socket_vector_op_unary<unirender::nodes::vector_math::MathType::Ceil>(l,socket);
			return socket_math_op_unary<unirender::nodes::math::MathType::Ceil>(l,socket);
		}));
		defSocket.def("Fraction",static_cast<unirender::Socket(*)(lua_State*,unirender::Socket&)>([](lua_State *l,unirender::Socket &socket) -> unirender::Socket {
			if(unirender::is_vector_type(socket.GetType()))
				return socket_vector_op_unary<unirender::nodes::vector_math::MathType::Fraction>(l,socket);
			return socket_math_op_unary<unirender::nodes::math::MathType::Fraction>(l,socket);
		}));
		defSocket.def("Abs",static_cast<unirender::Socket(*)(lua_State*,unirender::Socket&)>([](lua_State *l,unirender::Socket &socket) -> unirender::Socket {
			if(unirender::is_vector_type(socket.GetType()))
				return socket_vector_op_unary<unirender::nodes::vector_math::MathType::Absolute>(l,socket);
			return socket_math_op_unary<unirender::nodes::math::MathType::Absolute>(l,socket);
		}));
		defSocket.def("Sqrt",socket_math_op_unary<unirender::nodes::math::MathType::Sqrt>);
#if 0
		defSocket.def("InvSqrt",socket_math_op_unary<unirender::nodes::math::MathType::NODE_MATH_INV_SQRT>);
		defSocket.def("Sign",socket_math_op_unary<unirender::nodes::math::MathType::NODE_MATH_SIGN>);
		defSocket.def("Exp",socket_math_op_unary<unirender::nodes::math::MathType::NODE_MATH_EXPONENT>);
		defSocket.def("Rad",socket_math_op_unary<unirender::nodes::math::MathType::NODE_MATH_RADIANS>);
		defSocket.def("Deg",socket_math_op_unary<unirender::nodes::math::MathType::NODE_MATH_DEGREES>);
		defSocket.def("SinH",socket_math_op_unary<unirender::nodes::math::MathType::NODE_MATH_SINH>);
		defSocket.def("CosH",socket_math_op_unary<unirender::nodes::math::MathType::NODE_MATH_COSH>);
		defSocket.def("TanH",socket_math_op_unary<unirender::nodes::math::MathType::NODE_MATH_TANH>);
		defSocket.def("Trunc",socket_math_op_unary<unirender::nodes::math::MathType::NODE_MATH_TRUNC>);
		defSocket.def("Snap",static_cast<unirender::Socket(*)(lua_State*,unirender::Socket&,luabind::object)>([](lua_State *l,unirender::Socket &socket,luabind::object socketOther) -> unirender::Socket {
			if(unirender::is_vector_type(socket.GetType()))
				return socket_vector_op<unirender::nodes::vector_math::MathType::NODE_VECTOR_MATH_SNAP>(l,socket,socketOther);
			return socket_math_op<unirender::nodes::math::MathType::NODE_MATH_SNAP>(l,socket,socketOther);
		}));
		defSocket.def("Wrap",socket_math_op_tri<unirender::nodes::math::MathType::NODE_MATH_WRAP>);
		defSocket.def("Compare",socket_math_op_tri<unirender::nodes::math::MathType::NODE_MATH_COMPARE>);
		defSocket.def("MultiplyAdd",socket_math_op_tri<unirender::nodes::math::MathType::NODE_MATH_MULTIPLY_ADD>);
		defSocket.def("Pingpong",socket_math_op<unirender::nodes::math::MathType::NODE_MATH_PINGPONG>);
		defSocket.def("SmoothMin",socket_math_op_tri<unirender::nodes::math::MathType::NODE_MATH_SMOOTH_MIN>);
		defSocket.def("SmoothMax",socket_math_op_tri<unirender::nodes::math::MathType::NODE_MATH_SMOOTH_MAX>);
#endif
		defSocket.def("Lerp",static_cast<unirender::Socket(*)(lua_State*,unirender::Socket&,unirender::Socket&,unirender::Socket&)>([](lua_State *l,unirender::Socket &socket,unirender::Socket &other,unirender::Socket &factor) -> unirender::Socket {
			auto *node = find_socket_node(l,socket);
			node = node ? node : find_socket_node(l,other);
			node = node ? node : &get_socket_node(l,factor);
			if(unirender::is_vector_type(socket.GetType()) || unirender::is_vector_type(other.GetType()) || unirender::is_vector_type(factor.GetType()))
				return socket_to_vector(*node,socket) +(socket_to_vector(*node,other) -socket_to_vector(*node,socket)) *socket_to_vector(*node,factor);
			return socket +(other -socket) *factor;
		}));
		defSocket.def("Clamp",static_cast<unirender::Socket(*)(lua_State*,unirender::Socket&,luabind::object,luabind::object)>([](lua_State *l,unirender::Socket &socket,luabind::object min,luabind::object max) -> unirender::Socket {
			auto *node = find_socket_node(l,socket);
			return socket_math_op<unirender::nodes::math::MathType::Maximum>(l,socket_math_op<unirender::nodes::math::MathType::Minimum>(l,socket,min),max);
		}));

		// Vector operations
		defSocket.def("Cross",socket_vector_op<unirender::nodes::vector_math::MathType::CrossProduct>);
		defSocket.def("Project",socket_vector_op<unirender::nodes::vector_math::MathType::Project>);
		defSocket.def("Reflect",socket_vector_op<unirender::nodes::vector_math::MathType::Reflect>);
		defSocket.def("DotProduct",socket_vector_op<unirender::nodes::vector_math::MathType::DotProduct,false>);
		defSocket.def("Distance",socket_vector_op<unirender::nodes::vector_math::MathType::Distance,false>);
		defSocket.def("Length",socket_vector_op_unary<unirender::nodes::vector_math::MathType::Length,false>);
		defSocket.def("Scale",static_cast<unirender::Socket(*)(lua_State*,unirender::Socket&,luabind::object)>([](lua_State *l,unirender::Socket &socket,luabind::object scale) {
			auto &parent = get_socket_node(l,socket);
			auto &result = parent.AddVectorMathNode(socket,{},unirender::nodes::vector_math::MathType::Scale);
			parent.Link(get_socket(scale),result.GetInputSocket(unirender::nodes::vector_math::IN_SCALE));
			return *result.GetPrimaryOutputSocket();
		}));
		defSocket.def("Normalize",socket_vector_op_unary<unirender::nodes::vector_math::MathType::Normalize>);
		// These are already defined above (since they have both float and vector variants)
		// defSocket.def("Snap",socket_vector_op<unirender::nodes::vector_math::MathType::NODE_VECTOR_MATH_SNAP>);
		// defSocket.def("Min",socket_vector_op<unirender::nodes::vector_math::MathType::NODE_VECTOR_MATH_MINIMUM>);
		// defSocket.def("Max",socket_vector_op<unirender::nodes::vector_math::MathType::NODE_VECTOR_MATH_MAXIMUM>);
		// defSocket.def("Floor",socket_vector_op_unary<unirender::nodes::vector_math::MathType::NODE_VECTOR_MATH_FLOOR>);
		// defSocket.def("Ceil",socket_vector_op_unary<unirender::nodes::vector_math::MathType::NODE_VECTOR_MATH_CEIL>);
		// defSocket.def("Fraction",socket_vector_op_unary<unirender::nodes::vector_math::MathType::NODE_VECTOR_MATH_FRACTION>);
		// defSocket.def("Abs",socket_vector_op_unary<unirender::nodes::vector_math::MathType::NODE_VECTOR_MATH_ABSOLUTE>);
		modCycles[defSocket];

		auto defSceneObject = luabind::class_<unirender::SceneObject>("SceneObject");
		defSceneObject.def("GetScene",static_cast<void(*)(lua_State*,unirender::SceneObject&)>([](lua_State *l,unirender::SceneObject &sceneObject) {
			Lua::Push(l,sceneObject.GetScene().shared_from_this());
		}));
		defSceneObject.def("Finalize",static_cast<void(*)(lua_State*,unirender::SceneObject&,cycles::Scene&)>([](lua_State *l,unirender::SceneObject &sceneObject,cycles::Scene &scene) {
			sceneObject.Finalize(*scene,true);
		}));
		modCycles[defSceneObject];

		auto defWorldObject = luabind::class_<unirender::WorldObject>("WorldObject");
		defWorldObject.def("SetPos",static_cast<void(*)(lua_State*,unirender::WorldObject&,const Vector3&)>([](lua_State *l,unirender::WorldObject &worldObject,const Vector3 &pos) {
			worldObject.SetPos(pos);
			}));
		defWorldObject.def("GetPos",static_cast<void(*)(lua_State*,unirender::WorldObject&)>([](lua_State *l,unirender::WorldObject &worldObject) {
			Lua::Push<Vector3>(l,worldObject.GetPos());
			}));
		defWorldObject.def("SetRotation",static_cast<void(*)(lua_State*,unirender::WorldObject&,const Quat&)>([](lua_State *l,unirender::WorldObject &worldObject,const Quat &rot) {
			worldObject.SetRotation(rot);
			}));
		defWorldObject.def("GetRotation",static_cast<void(*)(lua_State*,unirender::WorldObject&)>([](lua_State *l,unirender::WorldObject &worldObject) {
			Lua::Push<Quat>(l,worldObject.GetRotation());
			}));
		defWorldObject.def("GetPose",static_cast<void(*)(lua_State*,unirender::WorldObject&)>([](lua_State *l,unirender::WorldObject &worldObject) {
			Lua::Push<umath::Transform>(l,worldObject.GetPose());
			}));
		modCycles[defWorldObject];

		auto defCamera = luabind::class_<unirender::Camera,luabind::bases<unirender::WorldObject,unirender::SceneObject>>("Camera");
		defCamera.add_static_constant("TYPE_PERSPECTIVE",umath::to_integral(unirender::Camera::CameraType::Perspective));
		defCamera.add_static_constant("TYPE_ORTHOGRAPHIC",umath::to_integral(unirender::Camera::CameraType::Orthographic));
		defCamera.add_static_constant("TYPE_PANORAMA",umath::to_integral(unirender::Camera::CameraType::Panorama));

		defCamera.add_static_constant("PANORAMA_TYPE_EQUIRECTANGULAR",umath::to_integral(unirender::Camera::PanoramaType::Equirectangular));
		defCamera.add_static_constant("PANORAMA_TYPE_FISHEYE_EQUIDISTANT",umath::to_integral(unirender::Camera::PanoramaType::FisheyeEquidistant));
		defCamera.add_static_constant("PANORAMA_TYPE_FISHEYE_EQUISOLID",umath::to_integral(unirender::Camera::PanoramaType::FisheyeEquisolid));
		defCamera.add_static_constant("PANORAMA_TYPE_MIRRORBALL",umath::to_integral(unirender::Camera::PanoramaType::Mirrorball));
		defCamera.def("SetInterocularDistance",static_cast<void(*)(lua_State*,util::WeakHandle<unirender::Camera>&,float)>([](lua_State *l,util::WeakHandle<unirender::Camera> &cam,float interocularDistance) {
			pragma::Lua::check_component(l,cam);
			cam->SetInterocularDistance(interocularDistance);
		}));
		defCamera.def("SetEquirectangularHorizontalRange",static_cast<void(*)(lua_State*,util::WeakHandle<unirender::Camera>&,float)>([](lua_State *l,util::WeakHandle<unirender::Camera> &cam,float range) {
			pragma::Lua::check_component(l,cam);
			cam->SetEquirectangularHorizontalRange(range);
		}));
		defCamera.def("SetEquirectangularVerticalRange",static_cast<void(*)(lua_State*,util::WeakHandle<unirender::Camera>&,float)>([](lua_State *l,util::WeakHandle<unirender::Camera> &cam,float range) {
			pragma::Lua::check_component(l,cam);
			cam->SetEquirectangularVerticalRange(range);
		}));
		defCamera.def("SetStereoscopic",static_cast<void(*)(lua_State*,util::WeakHandle<unirender::Camera>&,bool)>([](lua_State *l,util::WeakHandle<unirender::Camera> &cam,bool stereoscopic) {
			pragma::Lua::check_component(l,cam);
			cam->SetStereoscopic(stereoscopic);
		}));
		defCamera.def("SetResolution",static_cast<void(*)(lua_State*,util::WeakHandle<unirender::Camera>&,uint32_t,uint32_t)>([](lua_State *l,util::WeakHandle<unirender::Camera> &cam,uint32_t width,uint32_t height) {
			pragma::Lua::check_component(l,cam);
			cam->SetResolution(width,height);
		}));
		defCamera.def("SetFarZ",static_cast<void(*)(lua_State*,util::WeakHandle<unirender::Camera>&,float)>([](lua_State *l,util::WeakHandle<unirender::Camera> &cam,float farZ) {
			pragma::Lua::check_component(l,cam);
			cam->SetFarZ(farZ);
		}));
		defCamera.def("SetNearZ",static_cast<void(*)(lua_State*,util::WeakHandle<unirender::Camera>&,float)>([](lua_State *l,util::WeakHandle<unirender::Camera> &cam,float nearZ) {
			pragma::Lua::check_component(l,cam);
			cam->SetNearZ(nearZ);
		}));
		defCamera.def("SetFOV",static_cast<void(*)(lua_State*,util::WeakHandle<unirender::Camera>&,float)>([](lua_State *l,util::WeakHandle<unirender::Camera> &cam,float fov) {
			pragma::Lua::check_component(l,cam);
			cam->SetFOV(umath::deg_to_rad(fov));
		}));
		defCamera.def("SetCameraType",static_cast<void(*)(lua_State*,util::WeakHandle<unirender::Camera>&,uint32_t)>([](lua_State *l,util::WeakHandle<unirender::Camera> &cam,uint32_t camType) {
			pragma::Lua::check_component(l,cam);
			cam->SetCameraType(static_cast<unirender::Camera::CameraType>(camType));
		}));
		defCamera.def("SetPanoramaType",static_cast<void(*)(lua_State*,util::WeakHandle<unirender::Camera>&,uint32_t)>([](lua_State *l,util::WeakHandle<unirender::Camera> &cam,uint32_t panoramaType) {
			pragma::Lua::check_component(l,cam);
			cam->SetPanoramaType(static_cast<unirender::Camera::PanoramaType>(panoramaType));
		}));
		defCamera.def("SetFocalDistance",static_cast<void(*)(lua_State*,util::WeakHandle<unirender::Camera>&,float)>([](lua_State *l,util::WeakHandle<unirender::Camera> &cam,float focalDistance) {
			pragma::Lua::check_component(l,cam);
			cam->SetFocalDistance(focalDistance);
		}));
		defCamera.def("SetApertureSize",static_cast<void(*)(lua_State*,util::WeakHandle<unirender::Camera>&,float)>([](lua_State *l,util::WeakHandle<unirender::Camera> &cam,float size) {
			pragma::Lua::check_component(l,cam);
			cam->SetApertureSize(size);
		}));
		defCamera.def("SetApertureSizeFromFStop",static_cast<void(*)(lua_State*,util::WeakHandle<unirender::Camera>&,float,umath::Millimeter)>([](lua_State *l,util::WeakHandle<unirender::Camera> &cam,float fstop,umath::Millimeter focalLength) {
			pragma::Lua::check_component(l,cam);
			cam->SetApertureSizeFromFStop(fstop,focalLength);
		}));
		defCamera.def("SetFOVFromFocalLength",static_cast<void(*)(lua_State*,util::WeakHandle<unirender::Camera>&,umath::Millimeter,umath::Millimeter)>([](lua_State *l,util::WeakHandle<unirender::Camera> &cam,umath::Millimeter focalLength,umath::Millimeter sensorSize) {
			pragma::Lua::check_component(l,cam);
			cam->SetFOVFromFocalLength(focalLength,sensorSize);
		}));
		defCamera.def("SetBokehRatio",static_cast<void(*)(lua_State*,util::WeakHandle<unirender::Camera>&,float)>([](lua_State *l,util::WeakHandle<unirender::Camera> &cam,float ratio) {
			pragma::Lua::check_component(l,cam);
			cam->SetBokehRatio(ratio);
		}));
		defCamera.def("SetBladeCount",static_cast<void(*)(lua_State*,util::WeakHandle<unirender::Camera>&,uint32_t)>([](lua_State *l,util::WeakHandle<unirender::Camera> &cam,uint32_t numBlades) {
			pragma::Lua::check_component(l,cam);
			cam->SetBladeCount(numBlades);
		}));
		defCamera.def("SetBladesRotation",static_cast<void(*)(lua_State*,util::WeakHandle<unirender::Camera>&,float)>([](lua_State *l,util::WeakHandle<unirender::Camera> &cam,float rotation) {
			pragma::Lua::check_component(l,cam);
			cam->SetBladesRotation(rotation);
		}));
		defCamera.def("SetDepthOfFieldEnabled",static_cast<void(*)(lua_State*,util::WeakHandle<unirender::Camera>&,bool)>([](lua_State *l,util::WeakHandle<unirender::Camera> &cam,bool enabled) {
			pragma::Lua::check_component(l,cam);
			cam->SetDepthOfFieldEnabled(enabled);
		}));
		modCycles[defCamera];

		auto defProgressiveRefine = luabind::class_<pragma::modules::cycles::ProgressiveTexture>("ProgressiveTexture");
		defProgressiveRefine.def("GetTexture",static_cast<std::shared_ptr<prosper::Texture>(*)(pragma::modules::cycles::ProgressiveTexture&)>([](pragma::modules::cycles::ProgressiveTexture &prt) -> std::shared_ptr<prosper::Texture> {
			return prt.GetTexture();
		}));
		modCycles[defProgressiveRefine];

		auto defCache = luabind::class_<pragma::modules::cycles::Cache>("Cache");
		/*defCache.def("InitializeFromGameScene",static_cast<void(*)(lua_State*,pragma::modules::cycles::Cache&,Scene&,luabind::object,luabind::object)>([](lua_State *l,pragma::modules::cycles::Cache &cache,Scene &gameScene,luabind::object entFilter,luabind::object lightFilter) {
			initialize_cycles_geometry(const_cast<Scene&>(gameScene),cache,{},SceneFlags::None,to_entity_filter(l,&entFilter,3),to_entity_filter(l,&entFilter,4));
		}));*/
		defCache.def("InitializeFromGameScene",static_cast<void(*)(lua_State*,pragma::modules::cycles::Cache&,CSceneHandle&,luabind::object)>([](lua_State *l,pragma::modules::cycles::Cache &cache,CSceneHandle &gameScene,luabind::object entFilter) {
			pragma::Lua::check_component(l,gameScene);
			initialize_cycles_geometry(*gameScene,cache,{},SceneFlags::None,to_entity_filter(l,&entFilter,3),nullptr);
		}));
		defCache.def("InitializeFromGameScene",static_cast<void(*)(lua_State*,pragma::modules::cycles::Cache&,CSceneHandle&)>([](lua_State *l,pragma::modules::cycles::Cache &cache,CSceneHandle &gameScene) {
			pragma::Lua::check_component(l,gameScene);
			initialize_cycles_geometry(*gameScene,cache,{},SceneFlags::None,nullptr,nullptr);
		}));
		modCycles[defCache];

		auto defScene = luabind::class_<cycles::Scene>("Scene");

		auto defSerializationData = luabind::class_<unirender::Scene::SerializationData>("SerializationData");
		defSerializationData.def(luabind::constructor<>());
		defSerializationData.def_readwrite("outputFileName",&unirender::Scene::SerializationData::outputFileName);
		defScene.scope[defSerializationData];

		defScene.add_static_constant("RENDER_MODE_COMBINED",umath::to_integral(unirender::Scene::RenderMode::RenderImage));
		defScene.add_static_constant("RENDER_MODE_BAKE_AMBIENT_OCCLUSION",umath::to_integral(unirender::Scene::RenderMode::BakeAmbientOcclusion));
		defScene.add_static_constant("RENDER_MODE_BAKE_NORMALS",umath::to_integral(unirender::Scene::RenderMode::BakeNormals));
		defScene.add_static_constant("RENDER_MODE_BAKE_DIFFUSE_LIGHTING",umath::to_integral(unirender::Scene::RenderMode::BakeDiffuseLighting));
		defScene.add_static_constant("RENDER_MODE_ALBEDO",umath::to_integral(unirender::Scene::RenderMode::SceneAlbedo));
		defScene.add_static_constant("RENDER_MODE_NORMALS",umath::to_integral(unirender::Scene::RenderMode::SceneNormals));
		defScene.add_static_constant("RENDER_MODE_DEPTH",umath::to_integral(unirender::Scene::RenderMode::SceneDepth));

		defScene.add_static_constant("DEVICE_TYPE_CPU",umath::to_integral(unirender::Scene::DeviceType::CPU));
		defScene.add_static_constant("DEVICE_TYPE_GPU",umath::to_integral(unirender::Scene::DeviceType::GPU));

		defScene.add_static_constant("SCENE_FLAG_NONE",umath::to_integral(SceneFlags::None));
		defScene.add_static_constant("SCENE_FLAG_BIT_CULL_OBJECTS_OUTSIDE_CAMERA_FRUSTUM",umath::to_integral(SceneFlags::CullObjectsOutsideCameraFrustum));
		defScene.add_static_constant("SCENE_FLAG_BIT_CULL_OBJECTS_OUTSIDE_PVS",umath::to_integral(SceneFlags::CullObjectsOutsidePvs));
		
		defScene.add_static_constant("DENOISE_MODE_NONE",umath::to_integral(unirender::Scene::DenoiseMode::None));
		defScene.add_static_constant("DENOISE_MODE_FAST",umath::to_integral(unirender::Scene::DenoiseMode::Fast));
		defScene.add_static_constant("DENOISE_MODE_DETAILED",umath::to_integral(unirender::Scene::DenoiseMode::Detailed));

		defScene.def("InitializeFromGameScene",static_cast<void(*)(lua_State*,cycles::Scene&,CSceneHandle&,const Vector3&,const Quat&,const Mat4&,float,float,float,uint32_t,luabind::object,luabind::object)>([](lua_State *l,cycles::Scene &scene,CSceneHandle &gameScene,const Vector3 &camPos,const Quat &camRot,const Mat4 &vp,float nearZ,float farZ,float fov,uint32_t sceneFlags,luabind::object entFilter,luabind::object lightFilter) {
			pragma::Lua::check_component(l,gameScene);
			initialize_from_game_scene(l,*gameScene,scene,camPos,camRot,vp,nearZ,farZ,fov,static_cast<SceneFlags>(sceneFlags),&entFilter,&lightFilter);
		}));
		defScene.def("InitializeFromGameScene",static_cast<void(*)(lua_State*,cycles::Scene&,CSceneHandle&,const Vector3&,const Quat&,const Mat4&,float,float,float,uint32_t,luabind::object)>([](lua_State *l,cycles::Scene &scene,CSceneHandle &gameScene,const Vector3 &camPos,const Quat &camRot,const Mat4 &vp,float nearZ,float farZ,float fov,uint32_t sceneFlags,luabind::object entFilter) {
			pragma::Lua::check_component(l,gameScene);
			initialize_from_game_scene(l,*gameScene,scene,camPos,camRot,vp,nearZ,farZ,fov,static_cast<SceneFlags>(sceneFlags),&entFilter,nullptr);
		}));
		defScene.def("InitializeFromGameScene",static_cast<void(*)(lua_State*,cycles::Scene&,CSceneHandle&,const Vector3&,const Quat&,const Mat4&,float,float,float,uint32_t)>([](lua_State *l,cycles::Scene &scene,CSceneHandle &gameScene,const Vector3 &camPos,const Quat &camRot,const Mat4 &vp,float nearZ,float farZ,float fov,uint32_t sceneFlags) {
			pragma::Lua::check_component(l,gameScene);
			initialize_from_game_scene(l,*gameScene,scene,camPos,camRot,vp,nearZ,farZ,fov,static_cast<SceneFlags>(sceneFlags),nullptr,nullptr);
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
		defScene.def("Finalize",static_cast<void(*)(lua_State*,cycles::Scene&)>([](lua_State *l,cycles::Scene &scene) {
			scene->Finalize();
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
			auto light = unirender::Light::Create();
			if(light == nullptr)
				return;
			light->SetType(static_cast<unirender::Light::Type>(type));
			light->SetPos(pos);
			scene->AddLight(*light);
			Lua::Push(l,light.get());
		}));
		defScene.def("Save",static_cast<void(*)(lua_State*,cycles::Scene&,DataStream&,const std::string&,const unirender::Scene::SerializationData&)>([](lua_State *l,cycles::Scene &scene,DataStream &ds,const std::string &rootDir,const unirender::Scene::SerializationData &serializationData) {
			auto path = rootDir;
			if(Lua::file::validate_write_operation(l,path) == false)
				return;
			scene->Save(ds,path,serializationData);
		}));
		defScene.def("Load",static_cast<void(*)(lua_State*,cycles::Scene&,DataStream&,const std::string&)>([](lua_State *l,cycles::Scene &scene,DataStream &ds,const std::string &rootDir) {
			auto path = rootDir;
			if(Lua::file::validate_write_operation(l,path) == false)
				return;
			scene->Load(ds,path);
		}));
		defScene.def("AddCache",static_cast<void(*)(lua_State*,cycles::Scene&,const pragma::modules::cycles::Cache&)>([](lua_State *l,cycles::Scene &scene,const pragma::modules::cycles::Cache &cache) {
			scene->AddModelsFromCache(cache.GetModelCache());
		}));

		auto defSceneCreateInfo = luabind::class_<unirender::Scene::CreateInfo>("CreateInfo");
		defSceneCreateInfo.def(luabind::constructor<>());
		defSceneCreateInfo.def_readwrite("exposure",&unirender::Scene::CreateInfo::exposure);
		defSceneCreateInfo.def_readwrite("progressive",&unirender::Scene::CreateInfo::progressive);
		defSceneCreateInfo.def_readwrite("progressiveRefine",&unirender::Scene::CreateInfo::progressiveRefine);
		defSceneCreateInfo.def_readwrite("hdrOutput",&unirender::Scene::CreateInfo::hdrOutput);
		defSceneCreateInfo.def_readwrite("denoiseMode",reinterpret_cast<uint8_t unirender::Scene::CreateInfo::*>(&unirender::Scene::CreateInfo::denoiseMode));
		defSceneCreateInfo.def_readwrite("deviceType",reinterpret_cast<uint32_t unirender::Scene::CreateInfo::*>(&unirender::Scene::CreateInfo::deviceType));
		defSceneCreateInfo.def("SetSamplesPerPixel",static_cast<void(*)(lua_State*,unirender::Scene::CreateInfo&,uint32_t)>([](lua_State *l,unirender::Scene::CreateInfo &createInfo,uint32_t samples) {
			createInfo.samples = samples;
		}));
		defSceneCreateInfo.def("SetColorTransform",static_cast<void(*)(lua_State*,unirender::Scene::CreateInfo&,const std::string&)>([](lua_State *l,unirender::Scene::CreateInfo &createInfo,const std::string &config) {
			createInfo.colorTransform = unirender::Scene::ColorTransformInfo{};
			createInfo.colorTransform->config = config;
		}));
		defSceneCreateInfo.def("SetColorTransform",static_cast<void(*)(lua_State*,unirender::Scene::CreateInfo&,const std::string&,const std::string&)>([](lua_State *l,unirender::Scene::CreateInfo &createInfo,const std::string &config,const std::string &lookName) {
			createInfo.colorTransform = unirender::Scene::ColorTransformInfo{};
			createInfo.colorTransform->config = config;
			createInfo.colorTransform->lookName = lookName;
		}));
		defScene.scope[defSceneCreateInfo];

		auto defLight = luabind::class_<unirender::Light,luabind::bases<unirender::WorldObject>>("LightSource");
		defLight.add_static_constant("TYPE_POINT",umath::to_integral(unirender::Light::Type::Point));
		defLight.add_static_constant("TYPE_SPOT",umath::to_integral(unirender::Light::Type::Spot));
		defLight.add_static_constant("TYPE_DIRECTIONAL",umath::to_integral(unirender::Light::Type::Directional));
		defLight.add_static_constant("TYPE_AREA",umath::to_integral(unirender::Light::Type::Area));
		defLight.add_static_constant("TYPE_BACKGROUND",umath::to_integral(unirender::Light::Type::Background));
		defLight.add_static_constant("TYPE_TRIANGLE",umath::to_integral(unirender::Light::Type::Triangle));
		defLight.def("SetType",static_cast<void(*)(lua_State*,unirender::Light&,uint32_t)>([](lua_State *l,unirender::Light &light,uint32_t type) {
			light.SetType(static_cast<unirender::Light::Type>(type));
			}));
		defLight.def("SetConeAngles",static_cast<void(*)(lua_State*,unirender::Light&,float,float)>([](lua_State *l,unirender::Light &light,float innerAngle,float outerAngle) {
			light.SetConeAngles(umath::deg_to_rad(innerAngle),umath::deg_to_rad(outerAngle));
			}));
		defLight.def("SetColor",static_cast<void(*)(lua_State*,unirender::Light&,const Color&)>([](lua_State *l,unirender::Light &light,const Color &color) {
			light.SetColor(color);
			}));
		defLight.def("SetIntensity",static_cast<void(*)(lua_State*,unirender::Light&,float)>([](lua_State *l,unirender::Light &light,float intensity) {
			light.SetIntensity(intensity);
			}));
		defLight.def("SetSize",static_cast<void(*)(lua_State*,unirender::Light&,float)>([](lua_State *l,unirender::Light &light,float size) {
			light.SetSize(size);
			}));
		defLight.def("SetAxisU",static_cast<void(*)(lua_State*,unirender::Light&,const Vector3&)>([](lua_State *l,unirender::Light &light,const Vector3 &axisU) {
			light.SetAxisU(axisU);
			}));
		defLight.def("SetAxisV",static_cast<void(*)(lua_State*,unirender::Light&,const Vector3&)>([](lua_State *l,unirender::Light &light,const Vector3 &axisV) {
			light.SetAxisV(axisV);
			}));
		defLight.def("SetSizeU",static_cast<void(*)(lua_State*,unirender::Light&,float)>([](lua_State *l,unirender::Light &light,float sizeU) {
			light.SetSizeU(sizeU);
			}));
		defLight.def("SetSizeV",static_cast<void(*)(lua_State*,unirender::Light&,float)>([](lua_State *l,unirender::Light &light,float sizeV) {
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
				Lua::Push<unirender::PScene>(l,scene);
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
