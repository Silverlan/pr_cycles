#include "pr_cycles/object.hpp"
#include "pr_cycles/mesh.hpp"
#include "pr_cycles/scene.hpp"
#include "pr_cycles/shader.hpp"
#include <render/object.h>
#include <render/scene.h>
#include <render/mesh.h>

using namespace pragma::modules;

#pragma optimize("",off)
cycles::PObject cycles::Object::Create(Scene &scene,Mesh &mesh)
{
	auto *object = new ccl::Object{}; // Object will be removed automatically by cycles
	object->mesh = *mesh;
	object->tfm = ccl::transform_identity();
	scene->objects.push_back(object);
	auto pObject = PObject{new Object{scene,*object,static_cast<uint32_t>(scene->objects.size() -1),mesh}};
	scene.m_objects.push_back(pObject);
	return pObject;
}

cycles::Object::Object(Scene &scene,ccl::Object &object,uint32_t objectId,Mesh &mesh)
	: WorldObject{scene},m_object{object},m_mesh{mesh.shared_from_this()},
	m_id{objectId}
{}

uint32_t cycles::Object::GetId() const {return m_id;}

util::WeakHandle<cycles::Object> cycles::Object::GetHandle()
{
	return util::WeakHandle<cycles::Object>{shared_from_this()};
}

void cycles::Object::DoFinalize()
{
	m_mesh->Finalize();
	m_object.tfm = Scene::ToCyclesTransform(GetPose());

#ifdef ENABLE_MOTION_BLUR_TEST
	m_motionPose.SetOrigin(Vector3{100.f,100.f,100.f});
	m_object.motion.push_back_slow(Scene::ToCyclesTransform(GetMotionPose()));
#endif
}

const cycles::Mesh &cycles::Object::GetMesh() const {return const_cast<Object*>(this)->GetMesh();}
cycles::Mesh &cycles::Object::GetMesh() {return *m_mesh;}

const pragma::physics::Transform &cycles::Object::GetMotionPose() const {return m_motionPose;}
void cycles::Object::SetMotionPose(const pragma::physics::Transform &pose) {m_motionPose = pose;}

ccl::Object *cycles::Object::operator->() {return &m_object;}
ccl::Object *cycles::Object::operator*() {return &m_object;}
#pragma optimize("",on)
