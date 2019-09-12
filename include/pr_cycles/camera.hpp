#ifndef __PR_CYCLES_CAMERA_HPP__
#define __PR_CYCLES_CAMERA_HPP__

#include "world_object.hpp"
#include <memory>

namespace ccl {class Camera;};
namespace pragma::modules::cycles
{
	class Camera;
	using PCamera = std::shared_ptr<Camera>;
	class Camera
		: public WorldObject,
		public std::enable_shared_from_this<Camera>
	{
	public:
		static PCamera Create(Scene &scene);
		util::WeakHandle<Camera> GetHandle();

		void SetResolution(uint32_t width,uint32_t height);
		void SetFarZ(float farZ);
		void SetNearZ(float nearZ);
		void SetFOV(umath::Radian fov);

		virtual void DoFinalize() override;

		ccl::Camera *operator->();
		ccl::Camera *operator*();
	private:
		Camera(Scene &scene,ccl::Camera &cam);
		ccl::Camera &m_camera;
	};
};

#endif
