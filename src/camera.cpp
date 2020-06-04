#include "pr_cycles/camera.hpp"
#include "pr_cycles/scene.hpp"
#include <pragma/math/util_engine_math.hpp>
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
float cycles::Camera::GetAspectRatio() const {return static_cast<float>(m_camera.width) /static_cast<float>(m_camera.height);}
void cycles::Camera::SetCameraType(CameraType type)
{
	switch(type)
	{
	case CameraType::Perspective:
		m_camera.type = ccl::CameraType::CAMERA_PERSPECTIVE;
		break;
	case CameraType::Orthographic:
		m_camera.type = ccl::CameraType::CAMERA_ORTHOGRAPHIC;
		break;
	case CameraType::Panorama:
		m_camera.type = ccl::CameraType::CAMERA_PANORAMA;
		break;
	}
}
void cycles::Camera::SetDepthOfFieldEnabled(bool enabled) {m_dofEnabled = enabled;}
void cycles::Camera::SetFocalDistance(float focalDistance) {m_camera.focaldistance = Scene::ToCyclesLength(focalDistance);}
void cycles::Camera::SetApertureSize(float size) {m_camera.aperturesize = size;}
void cycles::Camera::SetBokehRatio(float ratio) {m_camera.aperture_ratio = ratio;}
void cycles::Camera::SetBladeCount(uint32_t numBlades) {m_camera.blades = numBlades;}
void cycles::Camera::SetBladesRotation(float rotation) {m_camera.bladesrotation = rotation;}
void cycles::Camera::SetApertureSizeFromFStop(float fstop,umath::Millimeter focalLength)
{
	SetApertureSize(util::calc_aperture_size_from_fstop(fstop,focalLength,m_camera.type == ccl::CameraType::CAMERA_ORTHOGRAPHIC));
}
void cycles::Camera::SetFOVFromFocalLength(umath::Millimeter focalLength,umath::Millimeter sensorSize)
{
	SetFOV(util::calc_fov_from_lens(sensorSize,focalLength,GetAspectRatio()));
}
void cycles::Camera::SetPanoramaType(PanoramaType type)
{
	switch(type)
	{
	case PanoramaType::Equirectangular:
		m_camera.panorama_type = ccl::PanoramaType::PANORAMA_EQUIRECTANGULAR;
		break;
	case PanoramaType::FisheyeEquidistant:
		m_camera.panorama_type = ccl::PanoramaType::PANORAMA_FISHEYE_EQUIDISTANT;
		break;
	case PanoramaType::FisheyeEquisolid:
		m_camera.panorama_type = ccl::PanoramaType::PANORAMA_FISHEYE_EQUISOLID;
		break;
	case PanoramaType::Mirrorball:
		m_camera.panorama_type = ccl::PanoramaType::PANORAMA_MIRRORBALL;
		break;
	}
}
void cycles::Camera::SetShutterTime(float timeInFrames) {m_camera.shuttertime = timeInFrames;}
void cycles::Camera::SetRollingShutterEnabled(bool enabled)
{
	m_camera.rolling_shutter_type = enabled ? ccl::Camera::RollingShutterType::ROLLING_SHUTTER_TOP : ccl::Camera::RollingShutterType::ROLLING_SHUTTER_NONE;
}
void cycles::Camera::SetRollingShutterDuration(float duration)
{
	m_camera.rolling_shutter_duration = duration;
}

void cycles::Camera::DoFinalize()
{
#ifdef ENABLE_MOTION_BLUR_TEST
	SetShutterTime(1.f);
#endif

	if(m_dofEnabled == false)
		m_camera.aperturesize = 0.f;
	if(m_camera.type == ccl::CameraType::CAMERA_PANORAMA)
	{
		auto rot = GetRotation();
		switch(m_camera.panorama_type)
		{
		case ccl::PanoramaType::PANORAMA_MIRRORBALL:
			rot *= uquat::create(EulerAngles{-90.f,0.f,0.f});
			break;
		case ccl::PanoramaType::PANORAMA_FISHEYE_EQUISOLID:
			m_camera.fisheye_lens = 10.5f;
			m_camera.fisheye_fov = 180.f;
			// No break is intentional!
		default:
			rot *= uquat::create(EulerAngles{-90.f,-90.f,0.f});
			break;
		}
		SetRotation(rot);
	}

	m_camera.matrix = Scene::ToCyclesTransform(GetPose());
	m_camera.compute_auto_viewplane();

	m_camera.need_update = true;
	m_camera.update(*GetScene());
}

ccl::Camera *cycles::Camera::operator->() {return &m_camera;}
ccl::Camera *cycles::Camera::operator*() {return &m_camera;}
#pragma optimize("",on)
