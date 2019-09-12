#include "pr_cycles/shader.hpp"
#include "pr_cycles/scene.hpp"
#include <render/shader.h>
#include <render/graph.h>
#include <render/scene.h>
#include <OpenImageIO/ustring.h>
#include <pragma/console/conout.h>

using namespace pragma::modules;

#pragma optimize("",off)
cycles::PShader cycles::Shader::Create(Scene &scene,ccl::Shader &shader)
{
	shader.volume_sampling_method = ccl::VOLUME_SAMPLING_MULTIPLE_IMPORTANCE;

	ccl::ShaderGraph *graph = new ccl::ShaderGraph(); // TODO: Delete this if not added to shader
	auto pShader = PShader{new Shader{scene,shader,*graph}};
	scene.m_shaders.push_back(pShader);

	for(auto *pNode : graph->nodes)
	{
		auto node = ShaderNode::Create(*pShader,*pNode);
		pShader->m_nodes.push_back(node);
	}
	return pShader;
}
cycles::PShader cycles::Shader::Create(Scene &scene,const std::string &name)
{
	auto *shader = new ccl::Shader{}; // Object will be removed automatically by cycles
	shader->name = name;
	scene->shaders.push_back(shader);
	return Create(scene,*shader);
}

cycles::Shader::Shader(Scene &scene,ccl::Shader &shader,ccl::ShaderGraph &shaderGraph)
	: SceneObject{scene},m_shader{shader},m_graph{shaderGraph}
{}

util::WeakHandle<cycles::Shader> cycles::Shader::GetHandle()
{
	return util::WeakHandle<cycles::Shader>{shared_from_this()};
}

cycles::PShaderNode cycles::Shader::AddNode(const std::string &type,const std::string &name)
{
	auto *nodeType = ccl::NodeType::find(ccl::ustring{type});
	auto *snode = nodeType ? static_cast<ccl::ShaderNode*>(nodeType->create(nodeType)) : nullptr;
	if(snode == nullptr)
	{
		Con::cerr<<"ERROR: Unable to create node of type '"<<type<<"': Invalid type!"<<Con::endl;
		return nullptr;
	}
	snode->name = name;
	m_graph.add(snode);

	auto node = ShaderNode::Create(*this,*snode);
	m_nodes.push_back(node);
	return node;
}

cycles::PShaderNode cycles::Shader::FindNode(const std::string &name) const
{
	auto it = std::find_if(m_nodes.begin(),m_nodes.end(),[&name](const PShaderNode &node) {
		return (*node)->name == name;
	});
	return (it != m_nodes.end()) ? *it : nullptr;
}

bool cycles::Shader::Link(
	const std::string &fromNodeName,const std::string &fromSocketName,
	const std::string &toNodeName,const std::string &toSocketName
)
{
	auto srcNode = FindNode(fromNodeName);
	auto dstNode = FindNode(toNodeName);
	if(srcNode == nullptr || dstNode == nullptr)
	{
		Con::cerr<<"ERROR: Attempted to link socket '"<<fromSocketName<<"' of node '"<<fromNodeName<<"' to socket '"<<toSocketName<<"' of node '"<<toNodeName<<"', but one of the nodes does not exist!"<<Con::endl;
		return false;
	}
	auto *output = srcNode->FindOutput(fromSocketName);
	auto *input = dstNode->FindInput(toSocketName);
	if(output == nullptr || input == nullptr)
	{
		Con::cerr<<"ERROR: Attempted to link socket '"<<fromSocketName<<"' of node '"<<fromNodeName<<"' to socket '"<<toSocketName<<"' of node '"<<toNodeName<<"', but one of the sockets does not exist!"<<Con::endl;
		return false;
	}
	m_graph.connect(output,input);
	return true;
}

void cycles::Shader::DoFinalize()
{
	m_shader.set_graph(&m_graph);
	m_shader.tag_update(*GetScene());
}

ccl::Shader *cycles::Shader::operator->() {return &m_shader;}
ccl::Shader *cycles::Shader::operator*() {return &m_shader;}

////////////////

cycles::PShaderNode cycles::ShaderNode::Create(Shader &shader,ccl::ShaderNode &shaderNode)
{
	return PShaderNode{new ShaderNode{shader,shaderNode}};
}
cycles::ShaderNode::ShaderNode(Shader &shader,ccl::ShaderNode &shaderNode)
	: m_shader{shader},m_shaderNode{shaderNode}
{}

util::WeakHandle<cycles::ShaderNode> cycles::ShaderNode::GetHandle()
{
	return util::WeakHandle<cycles::ShaderNode>{shared_from_this()};
}

ccl::ShaderNode *cycles::ShaderNode::operator->() {return &m_shaderNode;}
ccl::ShaderNode *cycles::ShaderNode::operator*() {return &m_shaderNode;}

ccl::ShaderInput *cycles::ShaderNode::FindInput(const std::string &inputName)
{
	auto it = std::find_if(m_shaderNode.inputs.begin(),m_shaderNode.inputs.end(),[&inputName](const ccl::ShaderInput *shInput) {
		return ccl::string_iequals(shInput->socket_type.name.string(),inputName);
	});
	return (it != m_shaderNode.inputs.end()) ? *it : nullptr;
}
ccl::ShaderOutput *cycles::ShaderNode::FindOutput(const std::string &outputName)
{
	auto it = std::find_if(m_shaderNode.outputs.begin(),m_shaderNode.outputs.end(),[&outputName](const ccl::ShaderOutput *shOutput) {
		return ccl::string_iequals(shOutput->socket_type.name.string(),outputName);
	});
	return (it != m_shaderNode.outputs.end()) ? *it : nullptr;
}
#pragma optimize("",on)
