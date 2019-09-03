#ifndef __PR_CYCLES_LIGHT_HPP__
#define __PR_CYCLES_LIGHT_HPP__

#include "world_object.hpp"
#include <mathutil/color.h>
#include <mathutil/uvec.h>
#include <memory>

namespace ccl {class Light;};
namespace pragma::modules::cycles
{
	using Lumen = float;
	class Light;
	using PLight = std::shared_ptr<Light>;
	class Light
		: public WorldObject
	{
	public:
		enum class Type : uint8_t
		{
			Point = 0u,
			Spot,
			Directional
		};
		static PLight Create(Scene &scene);

		void SetType(Type type);
		void SetConeAngle(umath::Radian angle);
		void SetColor(const Color &color);
		void SetIntensity(Lumen intensity);
		virtual void DoFinalize() override;

		ccl::Light *operator->();
		ccl::Light *operator*();
	private:
		Light(Scene &scene,ccl::Light &light);
		ccl::Light &m_light;
		Vector3 m_color = {1.f,1.f,1.f};
		Lumen m_intensity = 1'600.f;
		Type m_type = Type::Point;
	};
};

#endif
