#ifndef __PR_CYCLES_WORLD_OBJECT_HPP__
#define __PR_CYCLES_WORLD_OBJECT_HPP__

#include "scene_object.hpp"
#include <pragma/physics/transform.hpp>
#include <mathutil/uvec.h>

namespace pragma::modules::cycles
{
	class WorldObject;
	using PWorldObject = std::shared_ptr<WorldObject>;
	class WorldObject
		: public SceneObject
	{
	public:
		void SetPos(const Vector3 &pos);
		const Vector3 &GetPos() const;

		void SetRotation(const Quat &rot);
		const Quat &GetRotation() const;

		void SetScale(const Vector3 &scale);
		const Vector3 &GetScale() const;

		pragma::physics::ScaledTransform &GetPose();
		const pragma::physics::ScaledTransform &GetPose() const;
	protected:
		WorldObject(Scene &scene);
	private:
		pragma::physics::ScaledTransform m_pose = {};
	};
};

#endif
