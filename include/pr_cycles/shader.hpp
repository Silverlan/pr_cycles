/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#include <pragma/entities/baseentity_handle.h>
#include <pragma/entities/baseentity.h>
#include <util_raytracing/shader_nodes.hpp>
#include <util_raytracing/ccl_shader.hpp>
#include <pragma/lua/luaobjectbase.h>
#include <material.h>

class BaseEntity;
class Material;
namespace pragma::modules::cycles
{
	class Scene;
	class ShaderManager;
	class Shader
	{
	public:
		virtual ~Shader()=default;
		void Initialize(unirender::NodeManager &nodeManager,BaseEntity *ent,Material &mat);
		virtual std::shared_ptr<unirender::GroupNodeDesc> InitializeCombinedPass();
		virtual std::shared_ptr<unirender::GroupNodeDesc> InitializeAlbedoPass();
		virtual std::shared_ptr<unirender::GroupNodeDesc> InitializeNormalPass();
		virtual std::shared_ptr<unirender::GroupNodeDesc> InitializeDepthPass();

		BaseEntity *GetEntity() const;
		Material *GetMaterial() const;
	protected:
		Shader()=default;
		unirender::NodeManager *m_nodeManager = nullptr;
	private:
		mutable EntityHandle m_hEntity {};
		mutable MaterialHandle m_hMaterial {};
	};

	class ShaderManager
	{
	public:
		static std::shared_ptr<ShaderManager> Create();

		ShaderManager(const ShaderManager&)=delete;
		ShaderManager(ShaderManager&&)=delete;
		ShaderManager &operator=(const ShaderManager&)=delete;

		void RegisterShader(const std::string &name,luabind::object oClass);
		std::shared_ptr<Shader> CreateShader(unirender::NodeManager &nodeManager,const std::string &name,BaseEntity *ent,Material &mat);
	private:
		ShaderManager()=default;
		std::unordered_map<std::string,luabind::object> m_shaders;
	};
	pragma::modules::cycles::ShaderManager &get_shader_manager();


	class LuaShader
		: public LuaObjectBase,
		public Shader
	{
	public:
		void Initialize(const luabind::object &o);
		using Shader::Initialize;

		void Lua_InitializeCombinedPass(unirender::GroupNodeDesc &desc,unirender::NodeDesc &outputNode) {}
		static void Lua_default_InitializeCombinedPass(lua_State *l,LuaShader &shader,unirender::GroupNodeDesc &desc,unirender::NodeDesc &outputNode) {(&shader)->Shader::InitializeCombinedPass();}
		
		void Lua_InitializeAlbedoPass(unirender::GroupNodeDesc &desc,unirender::NodeDesc &outputNode) {}
		static void Lua_default_InitializeAlbedoPass(lua_State *l,LuaShader &shader,unirender::GroupNodeDesc &desc,unirender::NodeDesc &outputNode) {(&shader)->Shader::InitializeAlbedoPass();}
		
		void Lua_InitializeNormalPass(unirender::GroupNodeDesc &desc,unirender::NodeDesc &outputNode) {}
		static void Lua_default_InitializeNormalPass(lua_State *l,LuaShader &shader,unirender::GroupNodeDesc &desc,unirender::NodeDesc &outputNode) {(&shader)->Shader::InitializeNormalPass();}
		
		void Lua_InitializeDepthPass(unirender::GroupNodeDesc &desc,unirender::NodeDesc &outputNode) {}
		static void Lua_default_InitializeDepthPass(lua_State *l,LuaShader &shader,unirender::GroupNodeDesc &desc,unirender::NodeDesc &outputNode) {(&shader)->Shader::InitializeDepthPass();}

		virtual std::shared_ptr<unirender::GroupNodeDesc> InitializeCombinedPass() override;
		virtual std::shared_ptr<unirender::GroupNodeDesc> InitializeAlbedoPass() override;
		virtual std::shared_ptr<unirender::GroupNodeDesc> InitializeNormalPass() override;
		virtual std::shared_ptr<unirender::GroupNodeDesc> InitializeDepthPass() override;
	private:

	};
};
