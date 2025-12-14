// SPDX-FileCopyrightText: (c) 2024 Silverlan <opensource@pragma-engine.com>
// SPDX-License-Identifier: MIT

export module pragma.modules.scenekit:shader;

export import pragma.scenekit;
export import pragma.shared;

export namespace pragma::modules::scenekit {
	class Scene;
	class ShaderManager;
	class Shader {
	  public:
		virtual ~Shader() = default;
		virtual void Initialize(pragma::scenekit::NodeManager &nodeManager, pragma::ecs::BaseEntity *ent, geometry::ModelSubMesh *mesh, material::Material &mat);
		virtual std::shared_ptr<pragma::scenekit::GroupNodeDesc> InitializeCombinedPass();
		virtual std::shared_ptr<pragma::scenekit::GroupNodeDesc> InitializeAlbedoPass();
		virtual std::shared_ptr<pragma::scenekit::GroupNodeDesc> InitializeNormalPass();
		virtual std::shared_ptr<pragma::scenekit::GroupNodeDesc> InitializeDepthPass();

		void SetHairConfig(const pragma::util::HairConfig &hairConfig) { m_hairConfig = hairConfig; }
		void ClearHairConfig() { m_hairConfig = {}; }
		const std::optional<pragma::util::HairConfig> &GetHairConfig() const { return m_hairConfig; }

		void SetSubdivisionSettings(const pragma::scenekit::SubdivisionSettings &subdivSettings) { m_subdivSettings = subdivSettings; }
		void ClearSubdivisionSettings() { m_subdivSettings = {}; }
		const std::optional<pragma::scenekit::SubdivisionSettings> &GetSubdivisionSettings() const { return m_subdivSettings; }

		pragma::ecs::BaseEntity *GetEntity() const;
		material::Material *GetMaterial() const;
		geometry::ModelSubMesh *GetMesh() const;
	  protected:
		Shader() = default;
		pragma::scenekit::NodeManager *m_nodeManager = nullptr;
		std::optional<pragma::util::HairConfig> m_hairConfig {};
		std::optional<pragma::scenekit::SubdivisionSettings> m_subdivSettings {};
	  private:
		mutable EntityHandle m_hEntity {};
		mutable material::MaterialHandle m_hMaterial {};
		mutable std::shared_ptr<geometry::ModelSubMesh> m_mesh {};
	};

	class ShaderManager {
	  public:
		static std::shared_ptr<ShaderManager> Create();

		ShaderManager(const ShaderManager &) = delete;
		ShaderManager(ShaderManager &&) = delete;
		ShaderManager &operator=(const ShaderManager &) = delete;

		void RegisterShader(const std::string &name, luabind::object oClass);
		bool IsShaderRegistered(const std::string &name) const { return m_shaders.find(name) != m_shaders.end(); }
		std::shared_ptr<Shader> CreateShader(pragma::scenekit::NodeManager &nodeManager, const std::string &name, pragma::ecs::BaseEntity *ent, geometry::ModelSubMesh *mesh, material::Material &mat);
	  private:
		ShaderManager() = default;
		std::unordered_map<std::string, luabind::object> m_shaders;
	};
	pragma::modules::scenekit::ShaderManager &get_shader_manager();

	class LuaShader : public LuaObjectBase, public Shader {
	  public:
		void Initialize(const luabind::object &o);
		virtual void Initialize(pragma::scenekit::NodeManager &nodeManager, pragma::ecs::BaseEntity *ent, geometry::ModelSubMesh *mesh, material::Material &mat) override;

		void Lua_Initialize() {}
		static void Lua_default_Initialize(lua::State *l, LuaShader &shader) {}

		void Lua_InitializeCombinedPass(pragma::scenekit::GroupNodeDesc &desc, pragma::scenekit::NodeDesc &outputNode) {}
		static void Lua_default_InitializeCombinedPass(lua::State *l, LuaShader &shader, pragma::scenekit::GroupNodeDesc &desc, pragma::scenekit::NodeDesc &outputNode) { (&shader)->Shader::InitializeCombinedPass(); }

		void Lua_InitializeAlbedoPass(pragma::scenekit::GroupNodeDesc &desc, pragma::scenekit::NodeDesc &outputNode) {}
		static void Lua_default_InitializeAlbedoPass(lua::State *l, LuaShader &shader, pragma::scenekit::GroupNodeDesc &desc, pragma::scenekit::NodeDesc &outputNode) { (&shader)->Shader::InitializeAlbedoPass(); }

		void Lua_InitializeNormalPass(pragma::scenekit::GroupNodeDesc &desc, pragma::scenekit::NodeDesc &outputNode) {}
		static void Lua_default_InitializeNormalPass(lua::State *l, LuaShader &shader, pragma::scenekit::GroupNodeDesc &desc, pragma::scenekit::NodeDesc &outputNode) { (&shader)->Shader::InitializeNormalPass(); }

		void Lua_InitializeDepthPass(pragma::scenekit::GroupNodeDesc &desc, pragma::scenekit::NodeDesc &outputNode) {}
		static void Lua_default_InitializeDepthPass(lua::State *l, LuaShader &shader, pragma::scenekit::GroupNodeDesc &desc, pragma::scenekit::NodeDesc &outputNode) { (&shader)->Shader::InitializeDepthPass(); }

		virtual std::shared_ptr<pragma::scenekit::GroupNodeDesc> InitializeCombinedPass() override;
		virtual std::shared_ptr<pragma::scenekit::GroupNodeDesc> InitializeAlbedoPass() override;
		virtual std::shared_ptr<pragma::scenekit::GroupNodeDesc> InitializeNormalPass() override;
		virtual std::shared_ptr<pragma::scenekit::GroupNodeDesc> InitializeDepthPass() override;
	  private:
	};
};
