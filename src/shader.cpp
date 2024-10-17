/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

#include "pr_cycles/shader.hpp"
#include "pr_cycles/scene.hpp"
#include <pragma/model/modelmesh.h>
#include <pragma/console/conout.h>
#include <pragma/lua/ldefinitions.h>

using namespace pragma::modules::cycles;

#ifdef _WIN32
// For some reason client.dll exports these, even though it shouldn't, which causes a multiple defined symbol issue. The code below is a work-around.
// TODO: Find out where the symbols are exported in client.dll and remove them!
extern template class util::TWeakSharedHandle<BaseEntity>;
template __declspec(dllimport) util::TWeakSharedHandle<BaseEntity>::~TWeakSharedHandle();
//
#endif

void Shader::Initialize(pragma::scenekit::NodeManager &nodeManager, BaseEntity *ent, ModelSubMesh *mesh, Material &mat)
{
	m_nodeManager = &nodeManager;
	m_hEntity = ent ? ent->GetHandle() : EntityHandle {};
	m_hMaterial = mat.GetHandle();
	m_mesh = mesh ? mesh->shared_from_this() : nullptr;
}

std::shared_ptr<pragma::scenekit::GroupNodeDesc> Shader::InitializeCombinedPass() { return nullptr; }
std::shared_ptr<pragma::scenekit::GroupNodeDesc> Shader::InitializeAlbedoPass() { return nullptr; }
std::shared_ptr<pragma::scenekit::GroupNodeDesc> Shader::InitializeNormalPass() { return nullptr; }
std::shared_ptr<pragma::scenekit::GroupNodeDesc> Shader::InitializeDepthPass() { return nullptr; }
BaseEntity *Shader::GetEntity() const { return m_hEntity.get(); }
Material *Shader::GetMaterial() const { return m_hMaterial.get(); }
ModelSubMesh *Shader::GetMesh() const { return m_mesh.get(); }

//////////////

std::shared_ptr<ShaderManager> ShaderManager::Create() { return std::shared_ptr<ShaderManager> {new ShaderManager {}}; }
void ShaderManager::RegisterShader(const std::string &name, luabind::object oClass) { m_shaders[name] = oClass; }
std::shared_ptr<Shader> ShaderManager::CreateShader(pragma::scenekit::NodeManager &nodeManager, const std::string &name, BaseEntity *ent, ModelSubMesh *mesh, Material &mat)
{
	auto it = m_shaders.find(name);
	if(it == m_shaders.end())
		return nullptr;
	auto &o = it->second;

	auto *l = o.interpreter();
	luabind::object r;
#ifndef LUABIND_NO_EXCEPTIONS
	try {
#endif
		r = o();
#ifndef LUABIND_NO_EXCEPTIONS
	}
	catch(luabind::error &) {
		::Lua::HandleLuaError(l);
		return nullptr;
	}
#endif
	if(!r) {
		Con::ccl << "WARNING: Unable to create lua cycles shader '" << name << "'!" << Con::endl;
		return nullptr;
	}

	auto *shader = luabind::object_cast_nothrow<LuaShader *>(r, static_cast<LuaShader *>(nullptr));
	if(shader == nullptr) {
		// TODO: Can we check this when the particle modifier is being registered?
		Con::ccl << "WARNING: Unable to create lua cycles shader '" << name << "': Lua class is not derived from valid cycles shader base!" << Con::endl;
		return nullptr;
	}
	shader->Initialize(r);
	shader->Initialize(nodeManager, ent, mesh, mat);
	auto pShader = std::unique_ptr<Shader, void (*)(Shader *)> {shader, [](Shader *) {}};
	return pShader;
}

//////////////

void LuaShader::Initialize(const luabind::object &o) { m_baseLuaObj = std::shared_ptr<luabind::object>(new luabind::object(o)); }
void LuaShader::Initialize(pragma::scenekit::NodeManager &nodeManager, BaseEntity *ent, ModelSubMesh *mesh, Material &mat)
{
	Shader::Initialize(nodeManager, ent, mesh, mat);
	CallLuaMember<void>("Initialize");
}
std::shared_ptr<pragma::scenekit::GroupNodeDesc> LuaShader::InitializeCombinedPass()
{
	auto desc = pragma::scenekit::GroupNodeDesc::Create(*m_nodeManager);
	auto &nodeOutput = desc->AddNode(pragma::scenekit::NODE_OUTPUT);
	CallLuaMember<void, std::shared_ptr<pragma::scenekit::GroupNodeDesc>, std::shared_ptr<pragma::scenekit::NodeDesc>>("InitializeCombinedPass", desc, nodeOutput.shared_from_this());
	return desc;
}
std::shared_ptr<pragma::scenekit::GroupNodeDesc> LuaShader::InitializeAlbedoPass()
{
	auto desc = pragma::scenekit::GroupNodeDesc::Create(*m_nodeManager);
	auto &nodeOutput = desc->AddNode(pragma::scenekit::NODE_OUTPUT);
	CallLuaMember<void, std::shared_ptr<pragma::scenekit::GroupNodeDesc>, std::shared_ptr<pragma::scenekit::NodeDesc>>("InitializeAlbedoPass", desc, nodeOutput.shared_from_this());
	return desc;
}
std::shared_ptr<pragma::scenekit::GroupNodeDesc> LuaShader::InitializeNormalPass()
{
	auto desc = pragma::scenekit::GroupNodeDesc::Create(*m_nodeManager);
	auto &nodeOutput = desc->AddNode(pragma::scenekit::NODE_OUTPUT);
	CallLuaMember<void, std::shared_ptr<pragma::scenekit::GroupNodeDesc>, std::shared_ptr<pragma::scenekit::NodeDesc>>("InitializeNormalPass", desc, nodeOutput.shared_from_this());
	return desc;
}
std::shared_ptr<pragma::scenekit::GroupNodeDesc> LuaShader::InitializeDepthPass()
{
	auto desc = pragma::scenekit::GroupNodeDesc::Create(*m_nodeManager);
	auto &nodeOutput = desc->AddNode(pragma::scenekit::NODE_OUTPUT);
	CallLuaMember<void, std::shared_ptr<pragma::scenekit::GroupNodeDesc>, std::shared_ptr<pragma::scenekit::NodeDesc>>("InitializeDepthPass", desc, nodeOutput.shared_from_this());
	return desc;
}
