#include "pr_cycles/camera.hpp"
#include "pr_cycles/scene.hpp"
#include <render/camera.h>
#include <render/scene.h>

using namespace pragma::modules;

#pragma optimize("",off)
cycles::PCamera cycles::Camera::Create(Scene &scene)
{
	return PCamera{new Camera{scene,*scene->camera}};
}

cycles::Camera::Camera(Scene &scene,ccl::Camera &cam)
	: WorldObject{scene},m_camera{cam}
{
	cam.type = ccl::CameraType::CAMERA_PERSPECTIVE;
	cam.matrix = ccl::transform_identity();
}

util::WeakHandle<cycles::Camera> cycles::Camera::GetHandle()
{
	return util::WeakHandle<cycles::Camera>{shared_from_this()};
}

void cycles::Camera::SetResolution(uint32_t width,uint32_t height)
{
	m_camera.width = width;
	m_camera.height = height;
}

void cycles::Camera::SetFarZ(float farZ) {m_camera.farclip = Scene::ToCyclesLength(farZ);}
void cycles::Camera::SetNearZ(float nearZ) {m_camera.nearclip = Scene::ToCyclesLength(nearZ);}
void cycles::Camera::SetFOV(umath::Radian fov) {m_camera.fov = fov;}

void cycles::Camera::DoFinalize()
{
	m_camera.matrix = Scene::ToCyclesTransform(GetPose());
	m_camera.compute_auto_viewplane();

	m_camera.need_update = true;
	m_camera.update(*GetScene());
}

ccl::Camera *cycles::Camera::operator->() {return &m_camera;}
ccl::Camera *cycles::Camera::operator*() {return &m_camera;}
#pragma optimize("",on)
