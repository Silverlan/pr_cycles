#ifndef __PR_CYCLES_SHADER_HPP__
#define __PR_CYCLES_SHADER_HPP__

#include "scene_object.hpp"
#include <memory>
#include <string>
#include <vector>

namespace ccl {class Scene; class Shader; class ShaderGraph; class ShaderNode; class ShaderInput; class ShaderOutput;};
namespace pragma::modules::cycles
{
	class Scene;
	class Shader;
	class ShaderNode;
	using PShaderNode = std::shared_ptr<ShaderNode>;
	using PShader = std::shared_ptr<Shader>;
	class Shader
		: public SceneObject
	{
	public:
		static PShader Create(Scene &scene,const std::string &name);
		static PShader Create(Scene &scene,ccl::Shader &shader);

		PShaderNode AddNode(const std::string &type,const std::string &name);
		PShaderNode FindNode(const std::string &name) const;
		bool Link(
			const std::string &fromNodeName,const std::string &fromSocketName,
			const std::string &toNodeName,const std::string &toSocketName
		);
		virtual void DoFinalize() override;

		ccl::Shader *operator->();
		ccl::Shader *operator*();
	private:
		Shader(Scene &scene,ccl::Shader &shader,ccl::ShaderGraph &shaderGraph);
		ccl::Shader &m_shader;
		ccl::ShaderGraph &m_graph;
		std::vector<PShaderNode> m_nodes = {};
	};

	class ShaderNode
	{
	public:
		static PShaderNode Create(Shader &shader,ccl::ShaderNode &shaderNode);
		ccl::ShaderNode *operator->();
		ccl::ShaderNode *operator*();

		template<typename T>
			bool SetInputArgument(const std::string &inputName,const T &arg);
	private:
		friend Shader;
		ShaderNode(Shader &shader,ccl::ShaderNode &shaderNode);
		ccl::ShaderInput *FindInput(const std::string &inputName);
		ccl::ShaderOutput *FindOutput(const std::string &outputName);
		Shader &m_shader;
		ccl::ShaderNode &m_shaderNode;
	};
};

template<typename T>
	bool pragma::modules::cycles::ShaderNode::SetInputArgument(const std::string &inputName,const T &arg)
{
	auto it = std::find_if(m_shaderNode.inputs.begin(),m_shaderNode.inputs.end(),[&inputName](const ccl::ShaderInput *shInput) {
		return ccl::string_iequals(shInput->socket_type.name.string(),inputName);
	});
	if(it == m_shaderNode.inputs.end())
		return false;
	auto *input = *it;
	input->set(arg);
	return true;
}

#endif
