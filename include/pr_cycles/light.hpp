#ifndef __PR_CYCLES_LIGHT_HPP__
#define __PR_CYCLES_LIGHT_HPP__

#include "world_object.hpp"
#include <pragma/util/util_game.hpp>
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
		: public WorldObject,
		public std::enable_shared_from_this<Light>
	{
	public:
		enum class Type : uint8_t
		{
			Point = 0u,
			Spot,
			Directional,

			Area,
			Background,
			Triangle
		};
		static PLight Create(Scene &scene);
		util::WeakHandle<Light> GetHandle();

		void SetType(Type type);
		void SetConeAngles(umath::Radian innerAngle,umath::Radian outerAngle);
		void SetColor(const Color &color);
		void SetIntensity(Lumen intensity);
		void SetSize(float size);
		virtual void DoFinalize() override;

		void SetAxisU(const Vector3 &axisU);
		void SetAxisV(const Vector3 &axisV);
		void SetSizeU(float sizeU);
		void SetSizeV(float sizeV);

		ccl::Light *operator->();
		ccl::Light *operator*();
	private:
		Light(Scene &scene,ccl::Light &light);
		ccl::Light &m_light;
		float m_size = util::metres_to_units(0.25f);
		Vector3 m_color = {1.f,1.f,1.f};
		Lumen m_intensity = 1'600.f;
		Type m_type = Type::Point;
		umath::Radian m_spotInnerAngle = 0.f;
		umath::Radian m_spotOuterAngle = 0.f;

		Vector3 m_axisU = {};
		Vector3 m_axisV = {};
		float m_sizeU = util::metres_to_units(1.f);
		float m_sizeV = util::metres_to_units(1.f);
		bool m_bRound = false;
	};
};

#endif
