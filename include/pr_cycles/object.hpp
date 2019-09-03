#ifndef __PR_CYCLES_OBJECT_HPP__
#define __PR_CYCLES_OBJECT_HPP__

#include "world_object.hpp"
#include <memory>

namespace ccl {class Object;};
namespace pragma::modules::cycles
{
	class Scene;
	class Mesh;
	using PMesh = std::shared_ptr<Mesh>;
	class Object;
	using PObject = std::shared_ptr<Object>;
	class Object
		: public WorldObject
	{
	public:
		static PObject Create(Scene &scene,Mesh &mesh);
		virtual void DoFinalize() override;

		ccl::Object *operator->();
		ccl::Object *operator*();
	private:
		Object(Scene &scene,ccl::Object &object,Mesh &mesh);
		ccl::Object &m_object;
		PMesh m_mesh = nullptr;
	};
};

#endif
