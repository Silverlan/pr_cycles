#include "pr_cycles/world_object.hpp"

using namespace pragma::modules;

#pragma optimize("",off)
cycles::WorldObject::WorldObject(Scene &scene)
	: SceneObject{scene}
{}

void cycles::WorldObject::SetPos(const Vector3 &pos) {m_pose.SetOrigin(pos);}
const Vector3 &cycles::WorldObject::GetPos() const {return m_pose.GetOrigin();}

void cycles::WorldObject::SetRotation(const Quat &rot) {m_pose.SetRotation(rot);}
const Quat &cycles::WorldObject::GetRotation() const {return m_pose.GetRotation();}

pragma::physics::Transform &cycles::WorldObject::GetPose() {return m_pose;}
const pragma::physics::Transform &cycles::WorldObject::GetPose() const {return const_cast<WorldObject*>(this)->GetPose();}
#pragma optimize("",on)
