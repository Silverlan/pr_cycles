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
#include <pragma/rendering/render_queue.hpp>
#include <pragma/game/c_game.h>
#include <pragma/entities/environment/c_sky_camera.hpp>
#include <pragma/entities/components/c_render_component.hpp>

using namespace pragma::modules;

extern DLLCLIENT CGame *c_game;
#pragma optimize("",off)
void cycles::Scene::Add3DSkybox(pragma::CSceneComponent &gameScene,pragma::CSkyCameraComponent &skyCam,const Vector3 &camPos)
{
	std::unordered_map<CBaseEntity*,std::unordered_set<ModelSubMesh*>> entMeshes;
	auto fIterateRenderQueue = [&entMeshes](pragma::rendering::RenderQueue &renderQueue) {
		for(auto &item : renderQueue.queue)
		{
			auto *ent = static_cast<CBaseEntity*>(c_game->GetEntityByLocalIndex(item.entity));
			if(!ent)
				continue;
			auto renderC = ent->GetComponent<pragma::CRenderComponent>();
			if(renderC.expired())
				continue;
			auto &renderMeshes = renderC->GetRenderMeshes();
			auto itEnt = entMeshes.find(ent);
			if(itEnt == entMeshes.end())
				itEnt = entMeshes.insert(std::make_pair(ent,std::unordered_set<ModelSubMesh*>{})).first;
			for(auto &mesh : renderMeshes)
				itEnt->second.insert(mesh.get());
		}
	};

	auto renderQueue = pragma::rendering::RenderQueue::Create();
	auto translucentRenderQueue = pragma::rendering::RenderQueue::Create();

	pragma::rendering::RenderMask inclusionMask,exclusionMask;
	c_game->GetPrimaryCameraRenderMask(inclusionMask,exclusionMask);
	auto mask = c_game->GetInclusiveRenderMasks();
	mask |= inclusionMask;
	mask &= ~exclusionMask;

	skyCam.BuildSkyMeshRenderQueues(gameScene,RenderFlags::All,mask,false /* enableClipping */,*renderQueue,*translucentRenderQueue);
	fIterateRenderQueue(*renderQueue);
	fIterateRenderQueue(*translucentRenderQueue);

	auto &posSkyCam = skyCam.GetEntity().GetPosition();
	auto scale = skyCam.GetSkyboxScale();
	for(auto &pair : entMeshes)
	{
		auto &subMeshes = pair.second;
		auto entObj = m_cache->AddEntity(*pair.first,nullptr,nullptr,[&subMeshes](ModelSubMesh &subMesh,const umath::ScaledTransform &pose) -> bool {
			return subMeshes.find(&subMesh) != subMeshes.end();
		},"3d_sky");
		if(!entObj)
			continue;
		auto entPos = entObj->GetPos();
		entPos -= posSkyCam;
		entPos *= scale;
		entObj->SetPos(entPos);
		entObj->SetScale(Vector3{scale,scale,scale});
	}
}
#pragma optimize("",on)
