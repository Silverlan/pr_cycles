/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

#include "pr_cycles/scene.hpp"
namespace pragma::asset {
	class WorldData;
	class EntityData;
};
#include <pragma/c_engine.h>
#include <pragma/rendering/c_rendermode.h>
#include <pragma/entities/environment/effects/c_env_particle_system.h>
#include <pragma/entities/environment/c_env_camera.h>
#include <pragma/rendering/shaders/particles/c_shader_particle.hpp>
#include <pragma/game/c_game.h>
#include <datasystem_vector.h>
#include <future>
#include <deque>
#include <queue>

import pragma.scenekit;

extern DLLCLIENT CEngine *c_engine;
extern DLLCLIENT CGame *c_game;

using namespace pragma::modules;

static float get_particle_extent(float radius) { return sqrt(umath::pow2(radius) * 2.0); }
static Mat4 get_rotation_matrix(Vector3 axis, float angle)
{
	uvec::normalize(&axis);
	float s = sin(angle);
	float c = cos(angle);
	float oc = 1.0 - c;
	return Mat4(oc * axis.x * axis.x + c, oc * axis.x * axis.y - axis.z * s, oc * axis.z * axis.x + axis.y * s, 0.0, oc * axis.x * axis.y + axis.z * s, oc * axis.y * axis.y + c, oc * axis.y * axis.z - axis.x * s, 0.0, oc * axis.z * axis.x - axis.y * s, oc * axis.y * axis.z + axis.x * s,
	  oc * axis.z * axis.z + c, 0.0, 0.0, 0.0, 0.0, 1.0);
}

static Mat3 get_rotation_matrix(Vector4 q)
{
	return Mat3(1.0 - 2.0 * umath::pow2(q.y) - 2.0 * umath::pow2(q.z), 2.0 * q.x * q.y + 2.0 * q.z * q.w, 2.0 * q.x * q.z - 2.0 * q.y * q.w, 2.0 * q.x * q.y - 2.0 * q.z * q.w, 1.0 - 2.0 * umath::pow2(q.x) - 2.0 * umath::pow2(q.z), 2.0 * q.y * q.z + 2.0 * q.x * q.w,
	  2.0 * q.x * q.z + 2.0 * q.y * q.w, 2.0 * q.y * q.z - 2.0 * q.x * q.w, 1.0 - 2.0 * umath::pow2(q.x) - 2.0 * umath::pow2(q.y));
}

static Vector3 get_corner_particle_vertex_position(const pragma::CParticleSystemComponent::ParticleData &pt, const Vector3 &camPos, pragma::CParticleSystemComponent::OrientationType orientation, const Vector2 &vertPos, const Vector3 &camUpWs, const Vector3 &camRightWs, float nearZ,
  float farZ)
{
	Vector3 particleCenterWs {pt.position.x, pt.position.y, pt.position.z};
	Vector2 vsize {get_particle_extent(pt.radius), get_particle_extent(pt.length)};
	Vector3 squareVert {vertPos.x, vertPos.y, 0.0};

	Vector3 right {};
	Vector3 up {};
	switch(orientation) {
	case pragma::CParticleSystemComponent::OrientationType::Upright:
		{
			auto dir = camUpWs; // 'camUp_ws' is the particle world-rotation if this orientation type is selected
			right = uvec::cross(normalize(particleCenterWs - camPos), dir);
			up = -dir;
			break;
		}
	case pragma::CParticleSystemComponent::OrientationType::Static:
		right = uvec::UP;
		up = camUpWs;
		break;
	case pragma::CParticleSystemComponent::OrientationType::World:
		up = -uvec::get_normal(camUpWs);
		right = -uvec::get_normal(camRightWs);
		vsize = Vector2 {nearZ, farZ};
		break;
	default:
		right = camRightWs;
		up = camUpWs;
	}
	// TODO: What is in_rotation?
	auto sv = get_rotation_matrix(Vector3 {0.f, 0.f, 1.f}, umath::deg_to_rad(pt.rotation)) * Vector4 {squareVert.x, squareVert.y, squareVert.z, 1};
	squareVert = {sv.x, sv.y, sv.z};
	return right * squareVert.x * vsize.x + up * squareVert.y * vsize.y;
}

void cycles::Cache::AddParticleSystem(pragma::CParticleSystemComponent &ptc, const Vector3 &camPos, const Mat4 &vp, float nearZ, float farZ)
{
	auto *mat = ptc.GetMaterial();
	if(mat == nullptr)
		return;
	auto &renderers = ptc.GetRenderers();
	if(renderers.empty())
		return;
	auto &renderer = *renderers.front();
	auto *pShader = dynamic_cast<pragma::ShaderParticle2DBase *>(renderer.GetShader());
	if(pShader == nullptr)
		return;
	auto alphaMode = ptc.GetEffectiveAlphaMode();

	Vector3 camUpWs;
	Vector3 camRightWs;
	float ptNearZ, ptFarZ;
	auto orientationType = ptc.GetOrientationType();
	pShader->GetParticleSystemOrientationInfo(vp, ptc, orientationType, camUpWs, camRightWs, ptNearZ, ptFarZ, mat, nearZ, farZ);
	auto renderFlags = pShader->GetRenderFlags(ptc, pragma::ParticleRenderFlags::None);

	ShaderInfo shaderInfo {};
	shaderInfo.particleSystem = &ptc;
	std::string meshName = "particleMesh" + std::to_string(ptc.GetEntity().GetLocalIndex());
	auto *spriteSheetAnim = ptc.GetSpriteSheetAnimation();
	auto curTime = c_game->CurTime();
	auto &particles = ptc.GetRenderParticleData();
	auto &animData = ptc.GetParticleAnimationData();
	auto numParticles = ptc.GetRenderParticleCount();
	for(auto i = decltype(numParticles) {0u}; i < numParticles; ++i) {
		auto &pt = particles.at(i);
		auto ptIdx = ptc.TranslateBufferIndex(i);
		auto ptMeshName = meshName + "_" + std::to_string(i);
		constexpr uint32_t numVerts = pragma::ShaderParticle2DBase::VERTEX_COUNT;
		uint32_t numTris = pragma::ShaderParticle2DBase::TRIANGLE_COUNT * 2;
		auto mesh = pragma::scenekit::Mesh::Create(ptMeshName, numVerts, numTris);
		auto pos = ptc.GetParticlePosition(ptIdx);
		for(auto vertIdx = decltype(numVerts) {0u}; vertIdx < numVerts; ++vertIdx) {
			auto vertPos = pShader->CalcVertexPosition(ptc, ptc.TranslateBufferIndex(i), vertIdx, camPos, camUpWs, camRightWs, nearZ, farZ);
			auto uv = pragma::ShaderParticle2DBase::GetVertexUV(vertIdx);
			mesh->AddVertex(vertPos, -uvec::RIGHT, Vector4 {uvec::FORWARD, 1.f}, uv);
		}
		static_assert(pragma::ShaderParticle2DBase::TRIANGLE_COUNT == 2 && pragma::ShaderParticle2DBase::VERTEX_COUNT == 6);
		mesh->AddTriangle(0, 1, 2, 0);
		mesh->AddTriangle(3, 4, 5, 0);

		// We need back-faces to make sure particles with light emission also light up what's behind them
		mesh->AddTriangle(0, 2, 1, 0);
		mesh->AddTriangle(3, 5, 4, 0);

		shaderInfo.particle = &pt;

		auto shader = CreateShader(*mat, ptMeshName, shaderInfo);
		if(shader == nullptr)
			continue;
#if 0
		shader->SetFlags(pragma::scenekit::Shader::Flags::AdditiveByColor,alphaMode == ParticleAlphaMode::AdditiveByColor);
		shader->SetAlphaMode(AlphaMode::Blend);
		auto *shaderModAlbedo = dynamic_cast<pragma::scenekit::ShaderModuleAlbedo*>(shader.get());
		auto *shaderModEmission = dynamic_cast<pragma::scenekit::ShaderModuleEmission*>(shader.get());
		auto *shaderModSpriteSheet = dynamic_cast<pragma::scenekit::ShaderModuleSpriteSheet*>(shader.get());
		if(shaderModAlbedo)
		{
			if(shaderModSpriteSheet)
			{
				auto &ptData = *ptc.GetParticle(ptIdx);
				auto sequence = ptData.GetSequence();
				if(spriteSheetAnim && i < animData.size() && sequence < spriteSheetAnim->sequences.size())
				{
					auto &ptAnimData = animData.at(i);
					auto &seq = spriteSheetAnim->sequences.at(sequence);
					auto &frame0 = seq.frames.at(seq.GetLocalFrameIndex(ptAnimData.frameIndex0));
					auto &frame1 = seq.frames.at(seq.GetLocalFrameIndex(ptAnimData.frameIndex1));

					shaderModSpriteSheet->SetSpriteSheetData(
						frame0.uvStart,frame0.uvEnd,
						*shaderModAlbedo->GetAlbedoSet().GetAlbedoMap(),frame1.uvStart,frame1.uvEnd,
						ptAnimData.interpFactor
					);
				}
			}
			if(shaderModEmission)
			{
				if(ptc.IsBloomEnabled() == false)
					shaderModEmission->SetEmissionFactor({1.f,1.f,1.f}); // Clear emission
				else
				{
					auto &albedoMap = shaderModAlbedo->GetAlbedoSet().GetAlbedoMap();
					if(albedoMap.has_value())
					{
						shaderModEmission->SetEmissionMap(*albedoMap);
						/*auto valEmissionFactor = mat->GetDataBlock()->GetValue("emission_factor");
						if(valEmissionFactor && typeid(*valEmissionFactor) == typeid(ds::Vector))
						{
							auto &emissionFactor = static_cast<ds::Vector&>(*valEmissionFactor).GetValue();
							shaderModEmission->SetEmissionFactor(emissionFactor *m_emissionStrength);
						}*/

						auto &bloomColor = ptc.GetBloomColorFactor();
						auto emissionFactor = shaderModEmission->GetEmissionFactor();
						if(uvec::length_sqr(emissionFactor) != 0.f)
							emissionFactor *= Vector3{bloomColor};
						else
							emissionFactor = Vector3{bloomColor};
						shaderModEmission->SetEmissionFactor(emissionFactor);
					}
				}
			}
			auto ptColor = pt.GetColor().ToVector4();
			auto colorFactor = shaderModAlbedo->GetAlbedoSet().GetColorFactor();
			colorFactor *= ptColor;
			shaderModAlbedo->GetAlbedoSet().SetColorFactor(colorFactor);
		}
		mesh->AddSubMeshShader(*shader);

		pragma::scenekit::Object::Create(*m_rtScene,*mesh);
#endif
	}
}
