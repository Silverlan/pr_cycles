#include "pr_cycles/scene_object.hpp"

using namespace pragma::modules;

#pragma optimize("",off)
cycles::Scene &cycles::SceneObject::GetScene() const {return m_scene;}
cycles::SceneObject::SceneObject(Scene &scene)
	: m_scene{scene}
{}

void cycles::SceneObject::Finalize()
{
	if(m_bFinalized)
		return;
	m_bFinalized = true;
	DoFinalize();
}
void cycles::SceneObject::DoFinalize() {}
#pragma optimize("",on)
