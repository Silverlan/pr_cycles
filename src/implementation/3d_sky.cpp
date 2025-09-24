// SPDX-FileCopyrightText: (c) 2024 Silverlan <opensource@pragma-engine.com>
// SPDX-License-Identifier: MIT

module;

#include <sharedutils/util_event_reply.hpp>
#include <sharedutils/ctpl_stl.h>
#include <prosper_context.hpp>
#include <sharedutils/functioncallback.h>
#include <unordered_set>
#include <future>
#include <deque>
#include <queue>

module pragma.modules.scenekit;

import pragma.client;
import pragma.scenekit;
import :scene;

using namespace pragma::modules;

void scenekit::Scene::Add3DSkybox(pragma::CSceneComponent &gameScene, pragma::CSkyCameraComponent &skyCam, const Vector3 &camPos)
{
	std::unordered_map<CBaseEntity *, std::unordered_set<ModelSubMesh *>> entMeshes;
	auto fIterateRenderQueue = [&entMeshes](pragma::rendering::RenderQueue &renderQueue) {
		for(auto &item : renderQueue.queue) {
			auto *ent = static_cast<CBaseEntity *>(pragma::get_client_game()->GetEntityByLocalIndex(item.entity));
			if(!ent)
				continue;
			auto renderC = ent->GetComponent<pragma::CRenderComponent>();
			if(renderC.expired())
				continue;
			auto &renderMeshes = renderC->GetRenderMeshes();
			auto itEnt = entMeshes.find(ent);
			if(itEnt == entMeshes.end())
				itEnt = entMeshes.insert(std::make_pair(ent, std::unordered_set<::ModelSubMesh *> {})).first;
			for(auto &mesh : renderMeshes)
				itEnt->second.insert(mesh.get());
		}
	};

	auto renderQueue = pragma::rendering::RenderQueue::Create("unirender_3d_sky");
	auto translucentRenderQueue = pragma::rendering::RenderQueue::Create("unirender_3d_sky_translucent");

	pragma::rendering::RenderMask inclusionMask, exclusionMask;
	static_cast<CGame*>(pragma::get_client_game())->GetPrimaryCameraRenderMask(inclusionMask, exclusionMask);
	auto mask = static_cast<CGame*>(pragma::get_client_game())->GetInclusiveRenderMasks();
	mask |= inclusionMask;
	mask &= ~exclusionMask;

	skyCam.BuildSkyMeshRenderQueues(gameScene, RenderFlags::All, mask, false /* enableClipping */, *renderQueue, *translucentRenderQueue);
	fIterateRenderQueue(*renderQueue);
	fIterateRenderQueue(*translucentRenderQueue);

	auto &posSkyCam = skyCam.GetEntity().GetPosition();
	auto scale = skyCam.GetSkyboxScale();
	for(auto &pair : entMeshes) {
		auto &subMeshes = pair.second;
		auto entObj = m_cache->AddEntity(
		  *pair.first, nullptr, nullptr, [&subMeshes](::ModelSubMesh &subMesh, const umath::ScaledTransform &pose) -> bool { return subMeshes.find(&subMesh) != subMeshes.end(); }, "3d_sky");
		if(!entObj)
			continue;
		auto entPos = entObj->GetPos();
		entPos -= posSkyCam;
		entPos *= scale;
		entObj->SetPos(entPos);
		entObj->SetScale(Vector3 {scale, scale, scale});
	}
}
