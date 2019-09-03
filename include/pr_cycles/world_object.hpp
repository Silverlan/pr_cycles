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

		pragma::physics::Transform &GetPose();
		const pragma::physics::Transform &GetPose() const;
	protected:
		WorldObject(Scene &scene);
	private:
		pragma::physics::Transform m_pose = {};
	};
};

#endif
