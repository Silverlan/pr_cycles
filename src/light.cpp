#include "pr_cycles/light.hpp"
#include "pr_cycles/scene.hpp"
#include "pr_cycles/shader.hpp"
#include <render/light.h>
#include <render/scene.h>
#include <render/nodes.h>

using namespace pragma::modules;

#pragma optimize("",off)
cycles::PLight cycles::Light::Create(Scene &scene)
{
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
	case Type::Point:
	default:
		m_light.type = ccl::LightType::LIGHT_POINT;
		break;
	}
}

void cycles::Light::SetConeAngle(umath::Radian angle)
{
	m_light.spot_angle = angle;
}

void cycles::Light::SetColor(const Color &color)
{
	m_color = color.ToVector3();
	m_intensity = color.a;
}
void cycles::Light::SetIntensity(Lumen intensity) {m_intensity = intensity;}

void cycles::Light::DoFinalize()
{
	// auto ledLuminousEfficacy = 90.f; // Luminous efficacy in lumens per watt for a LED light source
	auto ledLuminousEfficacy = 1.f;
	if(m_type != Type::Directional)
		ledLuminousEfficacy = 5.f; // The above will result in spot and point lights that are very dim, so we use a different value for these (arbitrarily chosen)
	else
		ledLuminousEfficacy = 360.f;
	auto watt = m_intensity /ledLuminousEfficacy;

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
		auto forward = -uquat::forward(rot);
		m_light.spot_smooth = 1.f;
		m_light.dir = cycles::Scene::ToCyclesNormal(forward);
		break;
	}
	case Type::Directional:
	{
		shader = cycles::Shader::Create(GetScene(),"distance_shader");

		auto &rot = GetRotation();
		auto forward = -uquat::forward(rot);
		m_light.dir = cycles::Scene::ToCyclesNormal(forward);
		break;
	}
	}

	if(shader)
	{
		auto emissionStrength = watt;
		auto nodeEmission = shader->AddNode("emission","emission");
		nodeEmission->SetInputArgument<float>("strength",emissionStrength);
		// This seems to be just a modifier, it doesn't matter if we set the light color here, or for the actual light source below.
		nodeEmission->SetInputArgument<ccl::float3>("color",ccl::float3{1.f,1.f,1.f});
		shader->Link("emission","emission","output","surface");

		m_light.shader = **shader;
	}

	m_light.strength = ccl::float3{m_color.r,m_color.g,m_color.b};
	m_light.size = 0.f;
	m_light.co = cycles::Scene::ToCyclesPosition(GetPos());
}

ccl::Light *cycles::Light::operator->() {return &m_light;}
ccl::Light *cycles::Light::operator*() {return &m_light;}
#pragma optimize("",on)
