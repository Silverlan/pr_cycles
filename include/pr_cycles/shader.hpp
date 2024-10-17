/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

#include <pragma/entities/baseentity_handle.h>
#include <pragma/entities/baseentity.h>
#include <pragma/lua/luaobjectbase.h>
#include <material.h>

import pragma.scenekit;

class BaseEntity;
class Material;
class ModelSubMesh;
namespace pragma::modules::cycles {
	class Scene;
	class ShaderManager;
	class Shader {
	  public:
		virtual ~Shader() = default;
		virtual void Initialize(pragma::scenekit::NodeManager &nodeManager, BaseEntity *ent, ModelSubMesh *mesh, Material &mat);
		virtual std::shared_ptr<pragma::scenekit::GroupNodeDesc> InitializeCombinedPass();
		virtual std::shared_ptr<pragma::scenekit::GroupNodeDesc> InitializeAlbedoPass();
		virtual std::shared_ptr<pragma::scenekit::GroupNodeDesc> InitializeNormalPass();
		virtual std::shared_ptr<pragma::scenekit::GroupNodeDesc> InitializeDepthPass();

		void SetHairConfig(const util::HairConfig &hairConfig) { m_hairConfig = hairConfig; }
		void ClearHairConfig() { m_hairConfig = {}; }
		const std::optional<util::HairConfig> &GetHairConfig() const { return m_hairConfig; }

		void SetSubdivisionSettings(const pragma::scenekit::SubdivisionSettings &subdivSettings) { m_subdivSettings = subdivSettings; }
		void ClearSubdivisionSettings() { m_subdivSettings = {}; }
		const std::optional<pragma::scenekit::SubdivisionSettings> &GetSubdivisionSettings() const { return m_subdivSettings; }

		BaseEntity *GetEntity() const;
		Material *GetMaterial() const;
		ModelSubMesh *GetMesh() const;
	  protected:
		Shader() = default;
		pragma::scenekit::NodeManager *m_nodeManager = nullptr;
		std::optional<util::HairConfig> m_hairConfig {};
		std::optional<pragma::scenekit::SubdivisionSettings> m_subdivSettings {};
	  private:
		mutable EntityHandle m_hEntity {};
		mutable msys::MaterialHandle m_hMaterial {};
		mutable std::shared_ptr<ModelSubMesh> m_mesh {};
	};

	class ShaderManager {
	  public:
		static std::shared_ptr<ShaderManager> Create();

		ShaderManager(const ShaderManager &) = delete;
		ShaderManager(ShaderManager &&) = delete;
		ShaderManager &operator=(const ShaderManager &) = delete;

		void RegisterShader(const std::string &name, luabind::object oClass);
		bool IsShaderRegistered(const std::string &name) const { return m_shaders.find(name) != m_shaders.end(); }
		std::shared_ptr<Shader> CreateShader(pragma::scenekit::NodeManager &nodeManager, const std::string &name, BaseEntity *ent, ModelSubMesh *mesh, Material &mat);
	  private:
		ShaderManager() = default;
		std::unordered_map<std::string, luabind::object> m_shaders;
	};
	pragma::modules::cycles::ShaderManager &get_shader_manager();

	class LuaShader : public LuaObjectBase, public Shader {
	  public:
		void Initialize(const luabind::object &o);
		virtual void Initialize(pragma::scenekit::NodeManager &nodeManager, BaseEntity *ent, ModelSubMesh *mesh, Material &mat) override;

		void Lua_Initialize() {}
		static void Lua_default_Initialize(lua_State *l, LuaShader &shader) {}

		void Lua_InitializeCombinedPass(pragma::scenekit::GroupNodeDesc &desc, pragma::scenekit::NodeDesc &outputNode) {}
		static void Lua_default_InitializeCombinedPass(lua_State *l, LuaShader &shader, pragma::scenekit::GroupNodeDesc &desc, pragma::scenekit::NodeDesc &outputNode) { (&shader)->Shader::InitializeCombinedPass(); }

		void Lua_InitializeAlbedoPass(pragma::scenekit::GroupNodeDesc &desc, pragma::scenekit::NodeDesc &outputNode) {}
		static void Lua_default_InitializeAlbedoPass(lua_State *l, LuaShader &shader, pragma::scenekit::GroupNodeDesc &desc, pragma::scenekit::NodeDesc &outputNode) { (&shader)->Shader::InitializeAlbedoPass(); }

		void Lua_InitializeNormalPass(pragma::scenekit::GroupNodeDesc &desc, pragma::scenekit::NodeDesc &outputNode) {}
		static void Lua_default_InitializeNormalPass(lua_State *l, LuaShader &shader, pragma::scenekit::GroupNodeDesc &desc, pragma::scenekit::NodeDesc &outputNode) { (&shader)->Shader::InitializeNormalPass(); }

		void Lua_InitializeDepthPass(pragma::scenekit::GroupNodeDesc &desc, pragma::scenekit::NodeDesc &outputNode) {}
		static void Lua_default_InitializeDepthPass(lua_State *l, LuaShader &shader, pragma::scenekit::GroupNodeDesc &desc, pragma::scenekit::NodeDesc &outputNode) { (&shader)->Shader::InitializeDepthPass(); }

		virtual std::shared_ptr<pragma::scenekit::GroupNodeDesc> InitializeCombinedPass() override;
		virtual std::shared_ptr<pragma::scenekit::GroupNodeDesc> InitializeAlbedoPass() override;
		virtual std::shared_ptr<pragma::scenekit::GroupNodeDesc> InitializeNormalPass() override;
		virtual std::shared_ptr<pragma::scenekit::GroupNodeDesc> InitializeDepthPass() override;
	  private:
	};
};
