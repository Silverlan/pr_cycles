#include "pr_cycles/light.hpp"
#include "pr_cycles/scene.hpp"
#include "pr_cycles/shader.hpp"
#include <render/light.h>
#include <render/scene.h>
#include <render/nodes.h>
#include <mathutil/umath_lighting.hpp>

using namespace pragma::modules;

#pragma optimize("",off)
cycles::PLight cycles::Light::Create(Scene &scene)
{
	if(scene.GetRenderMode() == Scene::RenderMode::BakeAmbientOcclusion)
		return nullptr;
	auto *light = new ccl::Light{}; // Object will be removed automatically by cycles
	light->tfm = ccl::transform_identity();

	scene->lights.push_back(light);
	auto pLight = PLight{new Light{scene,*light}};
	scene.m_lights.push_back(pLight);
	return pLight;
}

cycles::Light::Light(Scene &scene,ccl::Light &light)
	: WorldObject{scene},m_light{light}
{}

util::WeakHandle<cycles::Light> cycles::Light::GetHandle()
{
	return util::WeakHandle<cycles::Light>{shared_from_this()};
}

void cycles::Light::SetType(Type type)
{
	m_type = type;
	switch(type)
	{
	case Type::Spot:
		m_light.type = ccl::LightType::LIGHT_SPOT;
		break;
	case Type::Directional:
		m_light.type = ccl::LightType::LIGHT_DISTANT;
		break;
	case Type::Area:
		m_light.type = ccl::LightType::LIGHT_AREA;
		break;
	case Type::Background:
		m_light.type = ccl::LightType::LIGHT_BACKGROUND;
		break;
	case Type::Triangle:
		m_light.type = ccl::LightType::LIGHT_TRIANGLE;
		break;
	case Type::Point:
	default:
		m_light.type = ccl::LightType::LIGHT_POINT;
		break;
	}
}

void cycles::Light::SetConeAngles(umath::Radian innerAngle,umath::Radian outerAngle)
{
	m_spotInnerAngle = innerAngle;
	m_spotOuterAngle = outerAngle;
}

void cycles::Light::SetColor(const Color &color)
{
	m_color = color.ToVector3();
	// Alpha is ignored
}
void cycles::Light::SetIntensity(Lumen intensity) {m_intensity = intensity;}

void cycles::Light::SetSize(float size) {m_size = size;}

void cycles::Light::SetAxisU(const Vector3 &axisU) {m_axisU = axisU;}
void cycles::Light::SetAxisV(const Vector3 &axisV) {m_axisV = axisV;}
void cycles::Light::SetSizeU(float sizeU) {m_sizeU = sizeU;}
void cycles::Light::SetSizeV(float sizeV) {m_sizeV = sizeV;}

void cycles::Light::DoFinalize()
{
	float watt = m_intensity;
	PShader shader = nullptr;
	switch(m_type)
	{
	case Type::Point:
	{
		shader = cycles::Shader::Create(GetScene(),"point_shader");
		break;
	}
	case Type::Spot:
	{
		shader = cycles::Shader::Create(GetScene(),"spot_shader");

		auto &rot = GetRotation();
		auto forward = uquat::forward(rot);
		m_light.spot_smooth = 1.f;
		m_light.dir = cycles::Scene::ToCyclesNormal(forward);
		m_light.spot_smooth = (m_spotOuterAngle > 0.f) ? (1.f -m_spotInnerAngle /m_spotOuterAngle) : 1.f;
		m_light.spot_angle = m_spotOuterAngle;
		break;
	}
	case Type::Directional:
	{
		shader = cycles::Shader::Create(GetScene(),"distance_shader");

		auto &rot = GetRotation();
		auto forward = uquat::forward(rot);
		m_light.dir = cycles::Scene::ToCyclesNormal(forward);
		watt /= 75.f;
		break;
	}
	case Type::Area:
	{
		shader = cycles::Shader::Create(GetScene(),"area_shader");
		m_light.axisu = cycles::Scene::ToCyclesNormal(m_axisU);
		m_light.axisv = cycles::Scene::ToCyclesNormal(m_axisV);
		m_light.sizeu = cycles::Scene::ToCyclesLength(m_sizeU);
		m_light.sizev = cycles::Scene::ToCyclesLength(m_sizeV);
		m_light.round = m_bRound;

		auto &rot = GetRotation();
		auto forward = uquat::forward(rot);
		m_light.dir = cycles::Scene::ToCyclesNormal(forward);
		break;
	}
	case Type::Background:
	{
		shader = cycles::Shader::Create(GetScene(),"background_shader");
		break;
	}
	case Type::Triangle:
	{
		shader = cycles::Shader::Create(GetScene(),"triangle_shader");
		break;
	}
	}

	if(shader)
	{
		auto nodeEmission = shader->AddNode("emission","emission");
		//nodeEmission->SetInputArgument<float>("strength",watt);
		//nodeEmission->SetInputArgument<ccl::float3>("color",ccl::float3{1.f,1.f,1.f});
		shader->Link("emission","emission","output","surface");

		m_light.shader = **shader;
	}

	if(m_type != Type::Directional)
	{
		// Factor 5 is arbitrary but makes it subjectively look better
		watt *= 5.f;

		// Note: According to the Cycles source code the luminous efficacy for a D65 standard illuminant should be used for the conversion,
		// however 160 Watt seems to be (subjectively) a much closer match to photometric values.
		// (See Cycles source code: cycles/src/util/util_ies.cpp -> IESFile::parse)
		watt = ulighting::lumens_to_watts(m_intensity,ulighting::get_luminous_efficacy(ulighting::LightSourceType::D65StandardIlluminant));
	}

	// Multiple importance sampling. It's disabled by default for some reason, but it's usually best to keep it on.
	m_light.use_mis = true;

	m_light.strength = ccl::float3{m_color.r,m_color.g,m_color.b} *watt;
	m_light.size = cycles::Scene::ToCyclesLength(m_size);
	m_light.co = cycles::Scene::ToCyclesPosition(GetPos());

	// Test
//	m_light.strength = ccl::float3{0.984539f,1.f,0.75f} *40.f;
//	m_light.size = 0.25f;
//	m_light.max_bounces = 1'024;
}

ccl::Light *cycles::Light::operator->() {return &m_light;}
ccl::Light *cycles::Light::operator*() {return &m_light;}
#pragma optimize("",on)
