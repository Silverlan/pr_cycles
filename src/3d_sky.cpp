#include <render/mesh.h>
#include "pr_cycles/scene.hpp"
#include "pr_cycles/mesh.hpp"
#include "pr_cycles/object.hpp"
#include "pr_cycles/shader.hpp"
#include <sharedutils/functioncallback.h>
#include <pragma/rendering/c_rendermode.h>
#include <pragma/rendering/scene/scene.h>
#include <pragma/entities/environment/c_sky_camera.hpp>

using namespace pragma::modules;

#pragma optimize("",off)
void cycles::Scene::Add3DSkybox(pragma::CSkyCameraComponent &skyCam,const Vector3 &camPos)
{
	auto filteredMeshes = skyCam.GetRenderMeshCollectionHandler().GetOcclusionFilteredMeshes();
	if(filteredMeshes.empty())
		return;
	auto scale = skyCam.GetSkyboxScale();
	std::unordered_set<CBaseEntity*> ents {};
	ents.reserve(filteredMeshes.size());
	for(auto &meshInfo : filteredMeshes)
	{
		if(meshInfo.hEntity.IsValid() == false)
			continue;
		ents.insert(static_cast<CBaseEntity*>(meshInfo.hEntity.get()));
	}
	auto &posSkyCam = skyCam.GetEntity().GetPosition();
	for(auto *ent : ents)
	{
		auto obj = AddEntity(*ent,nullptr,nullptr,nullptr,"3d_sky");
		if(obj == nullptr)
			continue;
		auto entPos = obj->GetPos();
		obj->SetPos({});
		auto &mesh = obj->GetMesh();
		// Move vertices so they are relative to camera
		for(auto i=decltype(mesh->verts.size()){0u};i<mesh->verts.size();++i)
		{
			auto v = entPos +Scene::ToPragmaPosition(mesh->verts[i]);
			v -= posSkyCam;
			v *= scale;
			mesh->verts[i] = Scene::ToCyclesPosition(v);
		}
	}
}
#pragma optimize("",on)
