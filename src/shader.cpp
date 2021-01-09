/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#include "pr_cycles/shader.hpp"
#include "pr_cycles/scene.hpp"
#include <pragma/console/conout.h>
#include <pragma/lua/ldefinitions.h>

using namespace pragma::modules::cycles;

void Shader::Initialize(unirender::NodeManager &nodeManager,BaseEntity *ent,Material &mat)
{
	m_nodeManager = &nodeManager;
	m_hEntity = ent ? ent->GetHandle() : EntityHandle{};
	m_hMaterial = mat.GetHandle();
}

std::shared_ptr<unirender::GroupNodeDesc> Shader::InitializeCombinedPass() {return nullptr;}
std::shared_ptr<unirender::GroupNodeDesc> Shader::InitializeAlbedoPass() {return nullptr;}
std::shared_ptr<unirender::GroupNodeDesc> Shader::InitializeNormalPass() {return nullptr;}
std::shared_ptr<unirender::GroupNodeDesc> Shader::InitializeDepthPass() {return nullptr;}
BaseEntity *Shader::GetEntity() const {return m_hEntity.get();}
Material *Shader::GetMaterial() const {return m_hMaterial.get();}

//////////////

std::shared_ptr<ShaderManager> ShaderManager::Create() {return std::shared_ptr<ShaderManager>{new ShaderManager{}};}
void ShaderManager::RegisterShader(const std::string &name,luabind::object oClass)
{
	m_shaders[name] = oClass;
}
std::shared_ptr<Shader> ShaderManager::CreateShader(unirender::NodeManager &nodeManager,const std::string &name,BaseEntity *ent,Material &mat)
{
	auto it = m_shaders.find(name);
	if(it == m_shaders.end())
		return nullptr;
	auto &o = it->second;

	auto *l = o.interpreter();
	luabind::object r;
#ifndef LUABIND_NO_EXCEPTIONS
	try
	{
#endif
		r = o();
#ifndef LUABIND_NO_EXCEPTIONS
	}
	catch(luabind::error&)
	{
		::Lua::HandleLuaError(l);
		return nullptr;
	}
#endif
	if(!r)
	{
		Con::ccl<<"WARNING: Unable to create lua cycles shader '"<<name<<"'!"<<Con::endl;
		return nullptr;
	}

	auto *shader = luabind::object_cast_nothrow<LuaShader*>(r,static_cast<LuaShader*>(nullptr));
	if(shader == nullptr)
	{
		// TODO: Can we check this when the particle modifier is being registered?
		Con::ccl<<"WARNING: Unable to create lua cycles shader '"<<name<<"': Lua class is not derived from valid cycles shader base!"<<Con::endl;
		return nullptr;
	}
	shader->Initialize(r);
	shader->Initialize(nodeManager,ent,mat);
	auto pShader = std::unique_ptr<Shader,void(*)(Shader*)>{shader,[](Shader*) {}};
	return pShader;
}

//////////////

void LuaShader::Initialize(const luabind::object &o)
{
	m_baseLuaObj = std::shared_ptr<luabind::object>(new luabind::object(o));
}
void LuaShader::Initialize(unirender::NodeManager &nodeManager,BaseEntity *ent,Material &mat)
{
	Shader::Initialize(nodeManager,ent,mat);
	CallLuaMember<void>("Initialize");
}
std::shared_ptr<unirender::GroupNodeDesc> LuaShader::InitializeCombinedPass()
{
	auto desc = unirender::GroupNodeDesc::Create(*m_nodeManager);
	auto &nodeOutput = desc->AddNode(unirender::NODE_OUTPUT);
	CallLuaMember<void,std::shared_ptr<unirender::GroupNodeDesc>,std::shared_ptr<unirender::NodeDesc>>("InitializeCombinedPass",desc,nodeOutput.shared_from_this());
	return desc;
}
std::shared_ptr<unirender::GroupNodeDesc> LuaShader::InitializeAlbedoPass()
{
	auto desc = unirender::GroupNodeDesc::Create(*m_nodeManager);
	auto &nodeOutput = desc->AddNode(unirender::NODE_OUTPUT);
	CallLuaMember<void,std::shared_ptr<unirender::GroupNodeDesc>,std::shared_ptr<unirender::NodeDesc>>("InitializeAlbedoPass",desc,nodeOutput.shared_from_this());
	return desc;
}
std::shared_ptr<unirender::GroupNodeDesc> LuaShader::InitializeNormalPass()
{
	auto desc = unirender::GroupNodeDesc::Create(*m_nodeManager);
	auto &nodeOutput = desc->AddNode(unirender::NODE_OUTPUT);
	CallLuaMember<void,std::shared_ptr<unirender::GroupNodeDesc>,std::shared_ptr<unirender::NodeDesc>>("InitializeNormalPass",desc,nodeOutput.shared_from_this());
	return desc;
}
std::shared_ptr<unirender::GroupNodeDesc> LuaShader::InitializeDepthPass()
{
	auto desc = unirender::GroupNodeDesc::Create(*m_nodeManager);
	auto &nodeOutput = desc->AddNode(unirender::NODE_OUTPUT);
	CallLuaMember<void,std::shared_ptr<unirender::GroupNodeDesc>,std::shared_ptr<unirender::NodeDesc>>("InitializeDepthPass",desc,nodeOutput.shared_from_this());
	return desc;
}
