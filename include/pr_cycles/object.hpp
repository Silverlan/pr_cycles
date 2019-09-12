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
		: public WorldObject,
		public std::enable_shared_from_this<Object>
	{
	public:
		static PObject Create(Scene &scene,Mesh &mesh);
		util::WeakHandle<Object> GetHandle();
		virtual void DoFinalize() override;

		const Mesh &GetMesh() const;
		Mesh &GetMesh();

		ccl::Object *operator->();
		ccl::Object *operator*();
	private:
		Object(Scene &scene,ccl::Object &object,Mesh &mesh);
		ccl::Object &m_object;
		PMesh m_mesh = nullptr;
	};
};

#endif
