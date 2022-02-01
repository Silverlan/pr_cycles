/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#include "pr_cycles/scene.hpp"
#include "util_raytracing/scene.hpp"
#include "util_raytracing/mesh.hpp"
#include "util_raytracing/object.hpp"
#include "util_raytracing/shader.hpp"
#include <prosper_context.hpp>
#include <sharedutils/functioncallback.h>
#include <pragma/rendering/c_rendermode.h>
#include <pragma/entities/environment/c_sky_camera.hpp>

using namespace pragma::modules;

void cycles::Scene::Add3DSkybox(pragma::CSkyCameraComponent &skyCam,const Vector3 &camPos)
{
	// TODO
#if 0
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
		auto obj = m_cache->AddEntity(*ent,nullptr,nullptr,nullptr,"3d_sky");
		if(obj == nullptr)
			continue;
		auto entPos = obj->GetPos();
		entPos -= posSkyCam;
		entPos *= scale;
		obj->SetPos(entPos);
		obj->SetScale(Vector3{scale,scale,scale});
	}
#endif
}
