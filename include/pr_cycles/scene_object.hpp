#ifndef __PR_CYCLES_SCENE_OBJECT_HPP__
#define __PR_CYCLES_SCENE_OBJECT_HPP__

#include <memory>

namespace pragma::modules::cycles
{
	class Scene;
	class SceneObject
	{
	public:
		virtual ~SceneObject()=default;
		void Finalize();
		Scene &GetScene() const;
	protected:
		virtual void DoFinalize();
		SceneObject(Scene &scene);
	private:
		Scene &m_scene;
		bool m_bFinalized = false;
	};
};

#endif
