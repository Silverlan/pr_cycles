#include "pr_cycles/scene.hpp"
#include "pr_cycles/mesh.hpp"
#include "pr_cycles/object.hpp"
#include "pr_cycles/shader.hpp"
#include <pragma/c_engine.h>
#include <pragma/rendering/c_rendermode.h>
#include <pragma/entities/environment/effects/c_env_particle_system.h>
#include <pragma/entities/environment/c_env_camera.h>
#include <pragma/rendering/shaders/particles/c_shader_particle.hpp>
#include <pragma/game/c_game.h>

extern DLLCENGINE CEngine *c_engine;
extern DLLCLIENT CGame *c_game;

using namespace pragma::modules;

#pragma optimize("",off)
static float get_particle_extent(float radius) {return sqrt(umath::pow2(radius) *2.0);}
static Mat4 get_rotation_matrix(Vector3 axis,float angle)
{
	uvec::normalize(&axis);
	float s = sin(angle);
	float c = cos(angle);
	float oc = 1.0 - c;
	return Mat4(
		oc *axis.x *axis.x +c,oc *axis.x *axis.y -axis.z *s,oc *axis.z *axis.x +axis.y *s,0.0,
		oc *axis.x *axis.y +axis.z *s,oc *axis.y *axis.y +c,oc *axis.y *axis.z -axis.x *s,0.0,
		oc *axis.z *axis.x -axis.y *s,oc *axis.y *axis.z +axis.x *s,oc *axis.z *axis.z +c,0.0,
		0.0,0.0,0.0,1.0
	);
}

static Mat3 get_rotation_matrix(Vector4 q)
{
	return Mat3(
		1.0 -2.0 *umath::pow2(q.y) -2.0 *umath::pow2(q.z),2.0 *q.x *q.y +2.0 *q.z *q.w,2.0 *q.x *q.z -2.0 *q.y *q.w,
		2.0 *q.x *q.y -2.0 *q.z *q.w,1.0 -2.0 *umath::pow2(q.x) -2.0 *umath::pow2(q.z),2.0 *q.y *q.z +2.0 *q.x *q.w,
		2.0 *q.x *q.z +2.0 *q.y *q.w,2.0 *q.y *q.z -2.0 *q.x *q.w,1.0 -2.0 *umath::pow2(q.x) -2.0 *umath::pow2(q.y)
	);
}

static Vector2 get_animated_texture_uv(const Vector2 &UV,float tStart,const pragma::CParticleSystemComponent::AnimationData &animData,float curTime)
{
	if(animData.frames == 0)
		return UV;
	float frame = animData.offset;
	float w = 1.0 /animData.columns;
	float h = 1.0 /animData.rows;
	if(animData.fps > 0)
	{
		float t = umath::max(curTime -tStart,0.f);
		float scale = float(animData.frames) /float(animData.fps);
		float r = t /scale;
		r = floor((t -floor(r) *scale) /scale *animData.frames);
		frame += r;
	}
	else
	{
		float t = tStart;
		float scale = t;
		float r = floor(scale *animData.frames);
		frame += r;
	}
	++frame;
	Vector2 texUV;
	texUV.x = UV.x /animData.columns +w *(frame -1);
	texUV.y = (UV.y /animData.rows) +h *(floor((frame -1) /animData.columns));
	return texUV;
}
static Vector2 get_particle_uv(const Vector2 &uv,float tStart,const pragma::CParticleSystemComponent::AnimationData &animData,float curTime,pragma::ShaderParticleBase::RenderFlags flags)
{
	if(umath::is_flag_set(flags,pragma::ShaderParticleBase::RenderFlags::Animated) == false)
		return uv;
	return get_animated_texture_uv(uv,tStart,animData,curTime);
}

static Vector3 get_corner_particle_vertex_position(
	const pragma::CParticleSystemComponent::ParticleData &pt,const Vector3 &camPos,
	pragma::CParticleSystemComponent::OrientationType orientation,const Vector2 &vertPos,
	const Vector3 &camUpWs,const Vector3 &camRightWs,float nearZ,float farZ
)
{
	Vector3 particleCenterWs {pt.position.x,pt.position.y,pt.position.z};
	Vector2 vsize {get_particle_extent(pt.position.w),get_particle_extent(pt.length)};
	Vector3 squareVert {vertPos.x,vertPos.y,0.0};

	Vector3 right {};
	Vector3 up {};
	switch(orientation)
	{
	case pragma::CParticleSystemComponent::OrientationType::Upright:
	{
		auto dir = camUpWs; // 'camUp_ws' is the particle world-rotation if this orientation type is selected
		right = uvec::cross(normalize(particleCenterWs -camPos),dir);
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
		vsize = Vector2{nearZ,farZ};
		break;
	default:
		right = camRightWs;
		up = camUpWs;
	}
	// TODO: What is in_rotation?
	auto sv = get_rotation_matrix(Vector3{0.f,0.f,1.f},umath::deg_to_rad(pt.rotation)) *Vector4{squareVert.x,squareVert.y,squareVert.z,1};
	squareVert = {sv.x,sv.y,sv.z};
	return right *squareVert.x *vsize.x
		+up *squareVert.y *vsize.y;
}

void cycles::Scene::AddParticleSystem(pragma::CParticleSystemComponent &ptc,const Vector3 &camPos,const Mat4 &vp,float nearZ,float farZ)
{
	auto *mat = ptc.GetMaterial();
	if(mat == nullptr)
		return;

	auto *pShader = static_cast<pragma::ShaderParticle*>(c_engine->GetShader("particle").get());
	if(pShader == nullptr)
		return;

	Vector3 camUpWs;
	Vector3 camRightWs;
	float ptNearZ,ptFarZ;
	auto orientationType = ptc.GetOrientationType();
	pShader->GetParticleSystemOrientationInfo(
		vp,ptc,orientationType,camUpWs,camRightWs,
		ptNearZ,ptFarZ,mat,nearZ,farZ
	);
	auto renderFlags = pShader->GetRenderFlags(ptc);

	constexpr std::array<Vector2,6> vertices = {
		Vector2{0.5f,-0.5f},
		Vector2{-0.5f,-0.5f},
		Vector2{-0.5f,0.5f},
		Vector2{0.5f,0.5f},
		Vector2{0.5f,-0.5f},
		Vector2{-0.5f,0.5f}
	};

	ShaderInfo shaderInfo {};
	shaderInfo.particleSystem = &ptc;
	std::string meshName = "particleMesh" +std::to_string(ptc.GetEntity().GetLocalIndex());
	auto *animData = ptc.GetAnimationData();
	auto curTime = c_game->CurTime();
	auto &particles = ptc.GetRenderParticleData();
	auto &animStartBuffer = ptc.GetRenderParticleAnimationStartData();
	auto numParticles = ptc.GetRenderParticleCount();
	for(auto i=decltype(numParticles){0u};i<numParticles;++i)
	{
		auto &pt = particles.at(i);
		auto ptMeshName = meshName +"_" +std::to_string(i);
		constexpr uint32_t numVerts = vertices.size();
		constexpr uint32_t numTris = 2;
		auto mesh = Mesh::Create(*this,ptMeshName,numVerts,numTris);
		auto pos = Vector3{pt.position.x,pt.position.y,pt.position.z};
		for(auto &v : vertices)
		{
			auto uv = v +Vector2{0.5f,0.5f};
			if(animData && i < animStartBuffer.size())
				uv = get_animated_texture_uv(uv,animStartBuffer.at(i),*animData,curTime);
			auto vertPos = pos +get_corner_particle_vertex_position(
				pt,camPos,orientationType,v,
				camUpWs,camRightWs,
				ptNearZ,ptFarZ
			);
			mesh->AddVertex(vertPos,uvec::RIGHT,uvec::FORWARD,uv);
		}
		mesh->AddTriangle(0,1,2,0);
		mesh->AddTriangle(3,4,5,0);
		shaderInfo.particle = &pt;
		auto shader = CreateShader(*mat,ptMeshName,shaderInfo);
		mesh->AddSubMeshShader(*shader);

		Object::Create(*this,*mesh);
	}
}
#pragma optimize("",on)
