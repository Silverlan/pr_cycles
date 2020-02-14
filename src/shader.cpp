#include "pr_cycles/shader.hpp"
#include "pr_cycles/scene.hpp"
#include <cmaterial.h>
#include <render/shader.h>
#include <render/graph.h>
#include <render/scene.h>
#include <render/nodes.h>
#include <OpenImageIO/ustring.h>
#include <pragma/console/conout.h>
#include <pragma/math/surfacematerial.h>
#include <pragma/util/util_game.hpp>

using namespace pragma::modules;

#pragma optimize("",off)
cycles::Shader::Shader(Scene &scene,const std::string &name)
	: SceneObject{scene},m_scene{scene},m_name{name}
{}

util::WeakHandle<cycles::Shader> cycles::Shader::GetHandle()
{
	return util::WeakHandle<cycles::Shader>{shared_from_this()};
}

cycles::Scene &cycles::Shader::GetScene() const {return m_scene;}
const std::string &cycles::Shader::GetName() const {return m_name;}
const std::string &cycles::Shader::GetMeshName() const {return m_meshName;}
void cycles::Shader::SetMeshName(const std::string &meshName) {m_meshName = meshName;}
bool cycles::Shader::HasFlag(Flags flags) const {return umath::is_flag_set(m_flags,flags);}
void cycles::Shader::SetFlags(Flags flags,bool enabled) {umath::set_flag(m_flags,flags,enabled);}
void cycles::Shader::SetUVHandler(TextureType type,const std::shared_ptr<UVHandler> &uvHandler) {m_uvHandlers.at(umath::to_integral(type)) = uvHandler;}
const std::shared_ptr<cycles::UVHandler> &cycles::Shader::GetUVHandler(TextureType type) const {return m_uvHandlers.at(umath::to_integral(type));}
void cycles::Shader::SetUVHandlers(const std::array<std::shared_ptr<UVHandler>,umath::to_integral(TextureType::Count)> &handlers) {m_uvHandlers = handlers;}
const std::array<std::shared_ptr<cycles::UVHandler>,umath::to_integral(cycles::Shader::TextureType::Count)> &cycles::Shader::GetUVHandlers() const {return m_uvHandlers;}

std::shared_ptr<cycles::CCLShader> cycles::Shader::GenerateCCLShader()
{
	auto cclShader = CCLShader::Create(*this);
	return SetupCCLShader(*cclShader) ? cclShader : nullptr;
}
std::shared_ptr<cycles::CCLShader> cycles::Shader::GenerateCCLShader(ccl::Shader &shader)
{
	auto cclShader = CCLShader::Create(*this,shader);
	return SetupCCLShader(*cclShader) ? cclShader : nullptr;
}
bool cycles::Shader::SetupCCLShader(CCLShader &cclShader)
{
	for(auto i=decltype(m_uvHandlers.size()){0u};i<m_uvHandlers.size();++i)
	{
		auto &uvHandler = m_uvHandlers.at(i);
		if(uvHandler == nullptr)
			continue;
		cclShader.m_uvSockets.at(i) = uvHandler->InitializeNodes(cclShader);
	}
	if(InitializeCCLShader(cclShader) == false)
		return false;
	return true;
}

void cycles::Shader::DoFinalize() {}

////////////////

std::shared_ptr<cycles::CCLShader> cycles::CCLShader::Create(Shader &shader,ccl::Shader &cclShader)
{
	cclShader.volume_sampling_method = ccl::VOLUME_SAMPLING_MULTIPLE_IMPORTANCE;

	ccl::ShaderGraph *graph = new ccl::ShaderGraph();
	auto pShader = std::shared_ptr<CCLShader>{new CCLShader{shader,cclShader,*graph}};
	pShader->m_bDeleteGraphIfUnused = true;

	for(auto *pNode : graph->nodes)
	{
		auto node = ShaderNode::Create(*pShader,*pNode);
		pShader->m_nodes.push_back(node);
	}
	shader.GetScene().AddShader(*pShader);
	return pShader;
}
std::shared_ptr<cycles::CCLShader> cycles::CCLShader::Create(Shader &shader)
{
	auto *cclShader = new ccl::Shader{}; // Object will be removed automatically by cycles
	cclShader->name = shader.GetName();
	shader.GetScene()->shaders.push_back(cclShader);
	return Create(shader,*cclShader);
}

cycles::CCLShader::CCLShader(Shader &shader,ccl::Shader &cclShader,ccl::ShaderGraph &cclShaderGraph)
	: m_shader{shader},m_cclShader{cclShader},m_cclGraph{cclShaderGraph}
{}

cycles::CCLShader::~CCLShader()
{
	if(m_bDeleteGraphIfUnused)
		delete &m_cclGraph;
}

ccl::Shader *cycles::CCLShader::operator->() {return &m_cclShader;}
ccl::Shader *cycles::CCLShader::operator*() {return &m_cclShader;}

void cycles::CCLShader::Finalize()
{
	m_bDeleteGraphIfUnused = false; // Graph will be deleted by Cycles
	m_cclShader.set_graph(&m_cclGraph);
	m_cclShader.tag_update(*GetShader().GetScene());
}

cycles::PShaderNode cycles::CCLShader::AddNode(const std::string &type,const std::string &name)
{
	auto *nodeType = ccl::NodeType::find(ccl::ustring{type});
	auto *snode = nodeType ? static_cast<ccl::ShaderNode*>(nodeType->create(nodeType)) : nullptr;
	if(snode == nullptr)
	{
		Con::cerr<<"ERROR: Unable to create node of type '"<<type<<"': Invalid type!"<<Con::endl;
		return nullptr;
	}
	snode->name = name;
	m_cclGraph.add(snode);

	auto node = ShaderNode::Create(*this,*snode);
	m_nodes.push_back(node);
	return node;
}

cycles::PShaderNode cycles::CCLShader::FindNode(const std::string &name) const
{
	auto it = std::find_if(m_nodes.begin(),m_nodes.end(),[&name](const PShaderNode &node) {
		return (*node)->name == name;
		});
	return (it != m_nodes.end()) ? *it : nullptr;
}

bool cycles::CCLShader::ValidateSocket(const std::string &nodeName,const std::string &socketName,bool output) const
{
	auto node = FindNode(nodeName);
	if(node == nullptr)
	{
		std::string msg = "Validation failure: Shader '" +std::string{m_cclShader.name} +"' has no node of name '" +nodeName +"'!";
		Con::cerr<<msg<<Con::endl;
		throw std::invalid_argument{msg};
		return false;
	}
	if(output)
	{
		auto *output = node->FindOutput(socketName);
		if(output == nullptr)
		{
			std::string msg = "Validation failure: Node '" +nodeName +"' (" +std::string{(*node)->type->name} +") of shader '" +std::string{m_cclShader.name} +"' has no output of name '" +socketName +"'!";
			Con::cerr<<msg<<Con::endl;
			throw std::invalid_argument{msg};
			return false;
		}
	}
	else
	{
		auto *input = node->FindInput(socketName);
		if(input == nullptr)
		{
			std::string msg = "Validation failure: Node '" +nodeName +"' (" +std::string{(*node)->type->name} +") of shader '" +std::string{m_cclShader.name} +"' has no input of name '" +socketName +"'!";
			Con::cerr<<msg<<Con::endl;
			throw std::invalid_argument{msg};
			return false;
		}
	}
	return true;
}

static ccl::ShaderInput *find_link(ccl::ShaderOutput &output,ccl::ShaderInput &input,std::unordered_set<ccl::ShaderOutput*> &iteratedOutputs)
{
	auto it = iteratedOutputs.find(&output);
	if(it != iteratedOutputs.end())
		return nullptr; // Prevent potential infinite recursion
	iteratedOutputs.insert(&output);
	for(auto *link : output.links)
	{
		if(link == &input)
			return link;
		if(link->parent == nullptr)
			continue;
		for(auto *output : link->parent->outputs)
		{
			auto *linkChld = find_link(*output,input,iteratedOutputs);
			if(linkChld)
				return link;
		}
	}
	return false;
}
// Finds the link from output to input, regardless of whether they're linked directly or through a chain
static ccl::ShaderInput *find_link(ccl::ShaderOutput &output,ccl::ShaderInput &input)
{
	std::unordered_set<ccl::ShaderOutput*> iteratedOutputs {};
	return find_link(output,input,iteratedOutputs);
}

void cycles::CCLShader::Disconnect(const Socket &socket)
{
	if(ValidateSocket(socket.nodeName,socket.socketName,socket.IsOutput()) == false)
		return;
	auto node = FindNode(socket.nodeName);
	if(socket.IsOutput())
	{
		auto *output = node->FindOutput(socket.socketName);
		m_cclGraph.disconnect(output);
	}
	else
	{
		auto *input = node->FindInput(socket.socketName);
		m_cclGraph.disconnect(input);
	}
}
bool cycles::CCLShader::Link(
	const std::string &fromNodeName,const std::string &fromSocketName,
	const std::string &toNodeName,const std::string &toSocketName,
	bool breakExistingLinks
)
{
	if(ValidateSocket(fromNodeName,fromSocketName,true) == false)
		return false;
	if(ValidateSocket(toNodeName,toSocketName,false) == false)
		return false;
	auto srcNode = FindNode(fromNodeName);
	auto dstNode = FindNode(toNodeName);
	auto *output = srcNode->FindOutput(fromSocketName);
	auto *input = dstNode->FindInput(toSocketName);
	if(breakExistingLinks)
	{
		// Break the link if it already exists
		auto *lnk = find_link(*output,*input);
		if(lnk)
		{
			auto it = std::find(output->links.begin(),output->links.end(),lnk);
			if(it != output->links.end())
				output->links.erase(it);
		}
		input->link = nullptr;
	}
	m_cclGraph.connect(output,input);
	return true;
}
bool cycles::CCLShader::Link(const Socket &fromSocket,const Socket &toSocket,bool breakExistingLinks)
{
	return Link(fromSocket.nodeName,fromSocket.socketName,toSocket.nodeName,toSocket.socketName,breakExistingLinks);
}
bool cycles::CCLShader::Link(const NumberSocket &fromSocket,const Socket &toSocket)
{
	return Link(
		fromSocket.m_socket.has_value() ? *fromSocket.m_socket : *AddConstantNode(fromSocket.m_value).outValue.m_socket,
		toSocket
	);
}
bool cycles::CCLShader::Link(const Socket &fromSocket,const NumberSocket &toSocket)
{
	return Link(
		fromSocket,
		toSocket.m_socket.has_value() ? *toSocket.m_socket : *AddConstantNode(toSocket.m_value).outValue.m_socket
	);
}
bool cycles::CCLShader::Link(const NumberSocket &fromSocket,const NumberSocket &toSocket)
{
	return Link(
		fromSocket.m_socket.has_value() ? *fromSocket.m_socket : *AddConstantNode(fromSocket.m_value).outValue.m_socket,
		toSocket.m_socket.has_value() ? *toSocket.m_socket : *AddConstantNode(toSocket.m_value).outValue.m_socket
	);
}

std::string cycles::CCLShader::GetCurrentInternalNodeName() const {return "internal_" +std::to_string(m_nodes.size());}

cycles::OutputNode cycles::CCLShader::GetOutputNode() const {return {*const_cast<CCLShader*>(this),"output"};}
cycles::MathNode cycles::CCLShader::AddMathNode()
{
	// Add a dummy math node
	auto name = GetCurrentInternalNodeName();
	auto &nodeV0 = *static_cast<ccl::MathNode*>(**AddNode("math",name));
	nodeV0.type = ccl::NodeMathType::NODE_MATH_ADD;
	nodeV0.value1 = 0.f;
	nodeV0.value2 = 0.f;
	return {*this,name,nodeV0};
}
cycles::MathNode cycles::CCLShader::AddMathNode(const NumberSocket &socket0,const NumberSocket &socket1,ccl::NodeMathType mathOp)
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeV0 = *static_cast<ccl::MathNode*>(**AddNode("math",name));
	MathNode nodeMath {*this,name,nodeV0};
	nodeV0.type = mathOp;

	if(socket0.m_socket.has_value())
		Link(*socket0.m_socket,nodeMath.inValue1);
	else
		nodeV0.value1 = socket0.m_value;

	if(socket1.m_socket.has_value())
		Link(*socket1.m_socket,nodeMath.inValue2);
	else
		nodeV0.value2 = socket1.m_value;
	return nodeMath;
}
cycles::MathNode cycles::CCLShader::AddConstantNode(float f)
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeV0 = *static_cast<ccl::MathNode*>(**AddNode("math",name));
	MathNode nodeMath {*this,name,nodeV0};
	nodeV0.type = ccl::NodeMathType::NODE_MATH_ADD;
	nodeMath.SetValue1(f);
	nodeMath.SetValue2(0.f);
	return nodeMath;
}
cycles::ImageTextureNode cycles::CCLShader::AddImageTextureNode(const std::string &fileName,const std::optional<Socket> &uvSocket,bool color)
{
	auto name = GetCurrentInternalNodeName();
	auto &cclNode = *static_cast<ccl::ImageTextureNode*>(**AddNode("image_texture",name));
	cclNode.filename = fileName;
	cclNode.colorspace = color ? ccl::u_colorspace_srgb : ccl::u_colorspace_raw;
	cycles::ImageTextureNode nodeImageTexture {*this,name};
	if(uvSocket.has_value())
		Link(*uvSocket,nodeImageTexture.inUVW);
	return {*this,name};
}
cycles::ImageTextureNode cycles::CCLShader::AddColorImageTextureNode(const std::string &fileName,const std::optional<Socket> &uvSocket) {return AddImageTextureNode(fileName,uvSocket,true);}
cycles::ImageTextureNode cycles::CCLShader::AddGradientImageTextureNode(const std::string &fileName,const std::optional<Socket> &uvSocket) {return AddImageTextureNode(fileName,uvSocket,false);}
cycles::NormalMapNode cycles::CCLShader::AddNormalMapImageTextureNode(const std::string &fileName,const std::string &meshName,const std::optional<Socket> &uvSocket,NormalMapNode::Space space)
{
	auto nodeImgNormal = AddGradientImageTextureNode(fileName,uvSocket);
	auto nodeNormalMap = AddNormalMapNode();
	nodeNormalMap.SetSpace(space);
	nodeNormalMap.SetAttribute(meshName);

	constexpr auto flipYAxis = false;
	if(flipYAxis)
	{
		// We need to invert the y-axis for cycles, so we separate the rgb components, invert the g channel and put them back together
		// Separate rgb components of input image
		auto nodeNormalRGB = AddSeparateRGBNode(nodeImgNormal);

		// Invert y-axis of normal
		auto nodeInvertY = 1.f -nodeNormalRGB.outG;

		// Re-combine rgb components
		auto nodeNormalInverted = AddCombineRGBNode(nodeNormalRGB.outR,nodeInvertY,nodeNormalRGB.outB);
		Link(nodeNormalInverted,nodeNormalMap.inColor);
	}
	else
		Link(nodeImgNormal,nodeNormalMap.inColor);
	return nodeNormalMap;
}
cycles::EnvironmentTextureNode cycles::CCLShader::AddEnvironmentTextureNode(const std::string &fileName)
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeImageTexture = *static_cast<ccl::EnvironmentTextureNode*>(**AddNode("environment_texture",name));
	nodeImageTexture.filename = fileName;
	nodeImageTexture.colorspace = ccl::u_colorspace_srgb;
	nodeImageTexture.projection = ccl::NodeEnvironmentProjection::NODE_ENVIRONMENT_EQUIRECTANGULAR;
	return {*this,name};
}
cycles::SeparateXYZNode cycles::CCLShader::AddSeparateXYZNode(const Socket &srcSocket)
{
	auto name = GetCurrentInternalNodeName();
	auto &cclNode = *static_cast<ccl::SeparateXYZNode*>(**AddNode("separate_xyz",name));
	cycles::SeparateXYZNode nodeSeparateXYZ {*this,name,cclNode};
	Link(srcSocket,nodeSeparateXYZ.inVector);
	return nodeSeparateXYZ;
}
cycles::CombineXYZNode cycles::CCLShader::AddCombineXYZNode(const std::optional<const NumberSocket> &x,const std::optional<const NumberSocket> &y,const std::optional<const NumberSocket> &z)
{
	auto name = GetCurrentInternalNodeName();
	auto &cclNode = *static_cast<ccl::CombineXYZNode*>(**AddNode("combine_xyz",name));
	cclNode.x = 0.f;
	cclNode.y = 0.f;
	cclNode.z = 0.f;
	cycles::CombineXYZNode node {*this,name,cclNode};
	if(x.has_value())
		Link(*x,node.inX);
	if(y.has_value())
		Link(*y,node.inY);
	if(z.has_value())
		Link(*z,node.inZ);
	return node;
}
cycles::SeparateRGBNode cycles::CCLShader::AddSeparateRGBNode(const Socket &srcSocket)
{
	auto name = GetCurrentInternalNodeName();
	auto &cclNode = *static_cast<ccl::SeparateRGBNode*>(**AddNode("separate_rgb",name));
	cycles::SeparateRGBNode nodeSeparateRGB {*this,name,cclNode};
	Link(srcSocket,nodeSeparateRGB.inColor);
	return nodeSeparateRGB;
}
cycles::CombineRGBNode cycles::CCLShader::AddCombineRGBNode(const std::optional<const NumberSocket> &x,const std::optional<const NumberSocket> &y,const std::optional<const NumberSocket> &z)
{
	auto name = GetCurrentInternalNodeName();
	auto &cclNode = *static_cast<ccl::CombineRGBNode*>(**AddNode("combine_rgb",name));
	cclNode.r = 0.f;
	cclNode.g = 0.f;
	cclNode.b = 0.f;
	cycles::CombineRGBNode node {*this,name,cclNode};
	if(x.has_value())
		Link(*x,node.inR);
	if(y.has_value())
		Link(*y,node.inG);
	if(z.has_value())
		Link(*z,node.inB);
	return node;
}
cycles::GeometryNode cycles::CCLShader::AddGeometryNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeGeometry = *static_cast<ccl::GeometryNode*>(**AddNode("geometry",name));
	return {*this,name};
}

cycles::NormalMapNode cycles::CCLShader::AddNormalMapNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeNormalMap = *static_cast<ccl::NormalMapNode*>(**AddNode("normal_map",name));
	return {*this,name,nodeNormalMap};
}

cycles::MixClosureNode cycles::CCLShader::AddMixClosureNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeMixClosure = *static_cast<ccl::MixClosureNode*>(**AddNode("mix_closure",name));
	return {*this,name,nodeMixClosure};
}

cycles::BackgroundNode cycles::CCLShader::AddBackgroundNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeBackground = *static_cast<ccl::BackgroundNode*>(**AddNode("background_shader",name));
	return {*this,name,nodeBackground};
}

cycles::TextureCoordinateNode cycles::CCLShader::AddTextureCoordinateNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeTexCoord = *static_cast<ccl::TextureCoordinateNode*>(**AddNode("texture_coordinate",name));
	return {*this,name,nodeTexCoord};
}

cycles::MappingNode cycles::CCLShader::AddMappingNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeMapping = *static_cast<ccl::MappingNode*>(**AddNode("mapping",name));
	return {*this,name,nodeMapping};
}

cycles::ColorNode cycles::CCLShader::AddColorNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeMapping = *static_cast<ccl::ColorNode*>(**AddNode("color",name));
	return {*this,name,nodeMapping};
}

cycles::EmissionNode cycles::CCLShader::AddEmissionNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeEmission = *static_cast<ccl::EmissionNode*>(**AddNode("emission",name));
	return {*this,name};
}

cycles::MixNode cycles::CCLShader::AddMixNode(const Socket &socketColor1,const Socket &socketColor2,MixNode::Type type,float fac)
{
	auto name = GetCurrentInternalNodeName();
	auto &cclNode = *static_cast<ccl::MixNode*>(**AddNode("mix",name));
	MixNode nodeMix {*this,name,cclNode};
	nodeMix.SetFactor(fac);
	nodeMix.SetType(type);
	Link(socketColor1,nodeMix.inColor1);
	Link(socketColor2,nodeMix.inColor2);
	return nodeMix;
}

cycles::PrincipledBSDFNode cycles::CCLShader::AddPrincipledBSDFNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodePrincipledBSDF = *static_cast<ccl::PrincipledBsdfNode*>(**AddNode("principled_bsdf",name));
	return {*this,name,nodePrincipledBSDF};
}

cycles::ToonBSDFNode cycles::CCLShader::AddToonBSDFNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeToonBSDF = *static_cast<ccl::ToonBsdfNode*>(**AddNode("toon_bsdf",name));
	return {*this,name,nodeToonBSDF};
}

cycles::GlassBSDFNode cycles::CCLShader::AddGlassBSDFNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeGlassBsdf = *static_cast<ccl::GlassBsdfNode*>(**AddNode("glass_bsdf",name));
	return {*this,name,nodeGlassBsdf};
}

cycles::TransparentBsdfNode cycles::CCLShader::AddTransparentBSDFNode()
{
	auto name = GetCurrentInternalNodeName();
	auto &nodeTransparentBsdf = *static_cast<ccl::TransparentBsdfNode*>(**AddNode("transparent_bsdf",name));
	return {*this,name,nodeTransparentBsdf};
}

cycles::MixClosureNode cycles::CCLShader::AddTransparencyClosure(const Socket &colorSocket,const NumberSocket &alphaSocket)
{
	auto nodeTransparentBsdf = AddTransparentBSDFNode();
	nodeTransparentBsdf.SetColor(Vector3{1.f,1.f,1.f}); // Fully transparent

	auto nodeMixTransparency = AddMixClosureNode();
	Link(alphaSocket,nodeMixTransparency.inFac); // Alpha transparency
	Link(nodeTransparentBsdf.outBsdf,nodeMixTransparency.inClosure1);
	Link(colorSocket,nodeMixTransparency.inClosure2);
	return nodeMixTransparency;
}

cycles::Shader &cycles::CCLShader::GetShader() const {return m_shader;}

const std::optional<cycles::Socket> &cycles::CCLShader::GetUVSocket(Shader::TextureType type) const {return m_uvSockets.at(umath::to_integral(type));}

////////////////

void cycles::ShaderPBR::SetWrinkleStretchMap(const std::string &wrinkleStretchMap) {m_wrinkleStretchMap = wrinkleStretchMap;}
void cycles::ShaderPBR::SetWrinkleCompressMap(const std::string &wrinkleCompressMap) {m_wrinkleCompressMap = wrinkleCompressMap;}

void cycles::ShaderPBR::SetMetallic(float metallic) {m_metallic = metallic;}
void cycles::ShaderPBR::SetSpecular(float specular) {m_specular = specular;}
void cycles::ShaderPBR::SetSpecularTint(float specularTint) {m_specularTint = specularTint;}
void cycles::ShaderPBR::SetAnisotropic(float anisotropic) {m_anisotropic = anisotropic;}
void cycles::ShaderPBR::SetAnisotropicRotation(float anisotropicRotation) {m_anisotropicRotation = anisotropicRotation;}
void cycles::ShaderPBR::SetSheen(float sheen) {m_sheen = sheen;}
void cycles::ShaderPBR::SetSheenTint(float sheenTint) {m_sheenTint = sheenTint;}
void cycles::ShaderPBR::SetClearcoat(float clearcoat) {m_clearcoat = clearcoat;}
void cycles::ShaderPBR::SetClearcoatRoughness(float clearcoatRoughness) {m_clearcoatRoughness = clearcoatRoughness;}
void cycles::ShaderPBR::SetIOR(float ior) {m_ior = ior;}
void cycles::ShaderPBR::SetTransmission(float transmission) {m_transmission = transmission;}
void cycles::ShaderPBR::SetTransmissionRoughness(float transmissionRoughness) {m_transmissionRoughness = transmissionRoughness;}

void cycles::ShaderPBR::SetSubsurface(float subsurface) {m_subsurface = subsurface;}
void cycles::ShaderPBR::SetSubsurfaceColor(const Vector3 &color) {m_subsurfaceColor = color;}
void cycles::ShaderPBR::SetSubsurfaceMethod(PrincipledBSDFNode::SubsurfaceMethod method) {m_subsurfaceMethod = method;}
void cycles::ShaderPBR::SetSubsurfaceRadius(const Vector3 &radius) {m_subsurfaceRadius = radius;}

////////////////

void cycles::ShaderModuleAlbedo::SetTransparent(Shader &shader,bool transparent)
{
	shader.SetFlags(Shader::Flags::Transparent,transparent);
}
void cycles::ShaderModuleAlbedo::SetEmissionFromAlbedoAlpha(Shader &shader,bool b)
{
	shader.SetFlags(Shader::Flags::EmissionFromAlbedoAlpha,b);
}
void cycles::ShaderModuleAlbedo::SetAlbedoMap(const std::string &albedoMap) {m_albedoMap = albedoMap;}
const std::optional<std::string> &cycles::ShaderModuleAlbedo::GetAlbedoMap() const {return m_albedoMap;}
std::optional<cycles::ImageTextureNode> cycles::ShaderModuleAlbedo::AddAlbedoMap(CCLShader &shader)
{
	if(m_albedoNode.has_value())
		return m_albedoNode;
	if(m_albedoMap.has_value() == false)
		return {};
	auto nodeAlbedo = shader.AddColorImageTextureNode(*m_albedoMap,shader.GetUVSocket(Shader::TextureType::Albedo));
	m_albedoNode = nodeAlbedo;
	return nodeAlbedo;
}
void cycles::ShaderModuleAlbedo::LinkAlbedo(const Socket &color,const NumberSocket &alpha)
{
	if(m_albedoNode.has_value() == false)
		return;
	auto &shader = color.GetShader();
	shader.Link(m_albedoNode->outColor,color);
	if(shader.GetShader().HasFlag(Shader::Flags::Transparent))
		shader.Link(m_albedoNode->outAlpha,alpha);
}
void cycles::ShaderModuleAlbedo::LinkAlbedoToBSDF(const Socket &bsdf)
{
	if(m_albedoNode.has_value() == false)
		return;
	auto &shader = bsdf.GetShader();
	if(shader.GetShader().HasFlag(Shader::Flags::Transparent))
	{
		auto nodeTransparentBsdf = shader.AddTransparencyClosure(m_albedoNode->outColor,m_albedoNode->outAlpha);
		shader.Link(nodeTransparentBsdf,bsdf);
	}
	else
		shader.Link(m_albedoNode->outColor,bsdf);
}

////////////////

bool cycles::ShaderGlass::InitializeCCLShader(CCLShader &cclShader)
{
	auto nodeBsdf = cclShader.AddGlassBSDFNode();
	// Default settings (Taken from Blender)
	nodeBsdf.SetIOR(1.45f);
	nodeBsdf.SetDistribution(GlassBSDFNode::Distribution::Beckmann);

	// Albedo map
	if(AddAlbedoMap(cclShader).has_value())
		LinkAlbedoToBSDF(nodeBsdf);

	// Normal map
	if(AddNormalMap(cclShader))
		LinkNormal(nodeBsdf.inNormal);

	// Roughness map
	if(AddRoughnessMap(cclShader))
		LinkRoughness(nodeBsdf.inRoughness);

	cclShader.Link(nodeBsdf,cclShader.GetOutputNode().inSurface);
	return true;
}

////////////////

bool cycles::ShaderGeneric::InitializeCCLShader(CCLShader &cclShader) {return true;}

////////////////

bool cycles::ShaderAlbedo::InitializeCCLShader(CCLShader &cclShader)
{
	// Albedo map
	if(AddAlbedoMap(cclShader).has_value())
		LinkAlbedoToBSDF(cclShader.GetOutputNode().inSurface);
	return true;
}

////////////////

bool cycles::ShaderColorTest::InitializeCCLShader(CCLShader &cclShader)
{
	auto colorNode = cclShader.AddColorNode();
	colorNode.SetColor({0.f,1.f,0.f});
	cclShader.Link(colorNode,cclShader.GetOutputNode().inSurface);
	return true;
}

////////////////

bool cycles::ShaderNormal::InitializeCCLShader(CCLShader &cclShader)
{
	// Normal map
	if(AddNormalMap(cclShader).has_value())
		LinkNormalToBSDF(cclShader.GetOutputNode().inSurface);
	return true;
}

////////////////

bool cycles::ShaderToon::InitializeCCLShader(CCLShader &cclShader)
{
	auto nodeBsdf = cclShader.AddToonBSDFNode();
	nodeBsdf.SetSize(0.5f);
	nodeBsdf.SetSmooth(0.f);

	// Albedo map
	if(AddAlbedoMap(cclShader).has_value())
		LinkAlbedoToBSDF(nodeBsdf);

	// Normal map
	if(AddNormalMap(cclShader))
		LinkNormal(nodeBsdf.inNormal);
	cclShader.Link(nodeBsdf,cclShader.GetOutputNode().inSurface);
	return true;
}

////////////////

void cycles::ShaderModuleNormal::SetNormalMap(const std::string &normalMap) {m_normalMap = normalMap;}
const std::optional<std::string> &cycles::ShaderModuleNormal::GetNormalMap() const {return m_normalMap;}
void cycles::ShaderModuleNormal::SetNormalMapSpace(NormalMapNode::Space space) {m_space = space;}
cycles::NormalMapNode::Space cycles::ShaderModuleNormal::GetNormalMapSpace() const {return m_space;}
std::optional<cycles::Socket> cycles::ShaderModuleNormal::AddNormalMap(CCLShader &shader)
{
	if(m_normalSocket.has_value())
		return m_normalSocket;
	if(m_normalMap.has_value()) // Use normal map
		m_normalSocket = shader.AddNormalMapImageTextureNode(*m_normalMap,shader.GetShader().GetMeshName(),shader.GetUVSocket(Shader::TextureType::Normal),GetNormalMapSpace());
	else // Use geometry normals
		m_normalSocket = shader.AddGeometryNode().outNormal;
	return m_normalSocket;
}
void cycles::ShaderModuleNormal::LinkNormalToBSDF(const Socket &bsdf)
{
	if(m_normalSocket.has_value() == false)
		return;
	auto &shader = bsdf.GetShader();
	auto socketOutput = *m_normalSocket;
	if(shader.GetShader().HasFlag(Shader::Flags::Transparent))
	{
		// Object uses translucency, which means we have to take the alpha of the albedo map into account.
		// Transparent normals don't make any sense, so we'll just render the normals as fully opaque if the alpha
		// is >= 0.5
		auto nodeAlbedo = AddAlbedoMap(shader);
		if(nodeAlbedo.has_value())
		{
			auto nodeRound = shader.AddMathNode(nodeAlbedo->outAlpha,0.f /* unused */,ccl::NodeMathType::NODE_MATH_ROUND);
			socketOutput = shader.AddTransparencyClosure(socketOutput,nodeRound).outClosure;
		}
	}
	shader.Link(socketOutput,bsdf);
}
void cycles::ShaderModuleNormal::LinkNormal(const Socket &normal)
{
	if(m_normalSocket.has_value() == false)
		return;
	normal.GetShader().Link(*m_normalSocket,normal);
}

////////////////

void cycles::ShaderModuleMetalness::SetMetalnessFactor(float metalnessFactor) {m_metalnessFactor = metalnessFactor;}
void cycles::ShaderModuleMetalness::SetMetalnessMap(const std::string &metalnessMap) {m_metalnessMap = metalnessMap;}
std::optional<cycles::NumberSocket> cycles::ShaderModuleMetalness::AddMetalnessMap(CCLShader &shader)
{
	if(m_metalnessSocket.has_value())
		return m_metalnessSocket;
	if(m_metalnessMap.has_value() == false)
	{
		// If no metalness map is available, just use metalness factor directly
		if(m_metalnessFactor.has_value())
		{
			m_metalnessSocket = shader.AddConstantNode(*m_metalnessFactor);
			return m_metalnessSocket;
		}
		return {};
	}
	auto nodeMetalness = shader.AddGradientImageTextureNode(*m_metalnessMap,shader.GetUVSocket(Shader::TextureType::Metalness));

	// We only need one channel value, so we'll just grab the red channel
	auto nodeMetalnessRGB = shader.AddSeparateRGBNode(nodeMetalness.outColor);

	auto socketMetalness = nodeMetalnessRGB.outR;
	if(m_metalnessFactor.has_value())
	{
		// Material has a metalness factor, which we need to multiply with the value from the metalness texture
		socketMetalness = shader.AddMathNode(socketMetalness,*m_metalnessFactor,ccl::NodeMathType::NODE_MATH_MULTIPLY).outValue;
	}

	m_metalnessSocket = socketMetalness;
	return socketMetalness;
}
void cycles::ShaderModuleMetalness::LinkMetalness(const NumberSocket &metalness)
{
	if(m_metalnessSocket.has_value() == false)
		return;
	metalness.GetShader().Link(*m_metalnessSocket,metalness);
}

////////////////

void cycles::ShaderModuleRoughness::SetRoughness(float roughness) {m_roughness = roughness;}
void cycles::ShaderModuleRoughness::SetRoughnessMap(const std::string &roughnessMap) {m_roughnessMap = roughnessMap;}
void cycles::ShaderModuleRoughness::SetSpecularMap(const std::string &specularMap) {m_specularMap = specularMap;}
std::optional<cycles::NumberSocket> cycles::ShaderModuleRoughness::AddRoughnessMap(CCLShader &shader)
{
	if(m_roughnessSocket.has_value())
		return m_roughnessSocket;
	if(m_roughnessMap.has_value() == false && m_specularMap.has_value() == false)
	{
		// If no roughness map is available, just use roughness factor directly
		if(m_roughnessFactor.has_value())
		{
			m_roughnessSocket = shader.AddConstantNode(*m_roughnessFactor);
			return m_roughnessSocket;
		}
		return {};
	}
	std::string roughnessMap {};
	auto isSpecularMap = false;
	if(m_roughnessMap.has_value())
		roughnessMap = *m_roughnessMap;
	else
	{
		roughnessMap = *m_specularMap;
		isSpecularMap = true;
	}

	auto nodeImgRoughness = shader.AddGradientImageTextureNode(*m_roughnessMap,shader.GetUVSocket(Shader::TextureType::Roughness));

	// We only need one channel value, so we'll just grab the red channel
	auto nodeRoughnessRGB = shader.AddSeparateRGBNode(nodeImgRoughness.outColor);

	auto socketRoughness = nodeRoughnessRGB.outR;
	if(isSpecularMap)
	{
		// We also have to invert the specular value
		socketRoughness = 1.f -socketRoughness;
	}
	if(m_roughnessFactor.has_value())
	{
		// Material has a roughness factor, which we need to multiply with the value from the roughness texture
		socketRoughness = shader.AddMathNode(socketRoughness,*m_roughnessFactor,ccl::NodeMathType::NODE_MATH_MULTIPLY).outValue;
	}
	m_roughnessSocket = socketRoughness;
	return m_roughnessSocket;
}
void cycles::ShaderModuleRoughness::LinkRoughness(const NumberSocket &roughness)
{
	if(m_roughnessSocket.has_value() == false)
		return;
	roughness.GetShader().Link(*m_roughnessSocket,roughness);
}

////////////////

void cycles::ShaderModuleEmission::SetEmissionMap(const std::string &emissionMap) {m_emissionMap = emissionMap;}
void cycles::ShaderModuleEmission::SetEmissionFactor(float factor) {m_emissionFactor = factor;}
std::optional<cycles::Socket> cycles::ShaderModuleEmission::AddEmissionMap(CCLShader &shader)
{
	if(m_emissionSocket.has_value())
		return m_emissionSocket;
	if(m_emissionMap.has_value() == false)
		return {};
	auto nodeImgEmission = shader.AddColorImageTextureNode(*m_emissionMap,shader.GetUVSocket(Shader::TextureType::Emission));
	if(shader.GetShader().HasFlag(Shader::Flags::EmissionFromAlbedoAlpha))
	{
		// Glow intensity
		auto nodeGlowRGB = shader.AddCombineRGBNode();

		auto glowIntensity = m_emissionFactor;
		nodeGlowRGB.SetR(glowIntensity);
		nodeGlowRGB.SetG(glowIntensity);
		nodeGlowRGB.SetB(glowIntensity);

		// Multiply glow color with intensity
		auto nodeMixEmission = shader.AddMixNode(nodeImgEmission,nodeGlowRGB,MixNode::Type::Multiply,1.f);

		// Grab alpha from glow map and create an RGB color from it
		auto nodeAlphaRGB = shader.AddCombineRGBNode(nodeImgEmission.outAlpha,nodeImgEmission.outAlpha,nodeImgEmission.outAlpha);

		// Multiply alpha with glow color
		auto nodeMixEmissionAlpha = shader.AddMixNode(nodeMixEmission,nodeAlphaRGB,MixNode::Type::Multiply,1.f);
		return nodeMixEmissionAlpha;
	}
	return nodeImgEmission;
}
void cycles::ShaderModuleEmission::LinkEmission(const Socket &emission)
{
	if(m_emissionSocket.has_value() == false)
		return;
	emission.GetShader().Link(*m_emissionSocket,emission);
}

////////////////

bool cycles::ShaderPBR::InitializeCCLShader(CCLShader &cclShader)
{
	// Note: We always need the albedo texture information for the translucency.
	// Whether metalness/roughness/etc. affect baking in any way is unclear (probably not),
	// but it also doesn't hurt to have them.
	auto albedoNode = AddAlbedoMap(cclShader);
	if(albedoNode.has_value() == false)
		return false;

	auto nodeBsdf = cclShader.AddPrincipledBSDFNode();
	nodeBsdf.SetMetallic(m_metallic);
	nodeBsdf.SetSpecular(m_specular);
	nodeBsdf.SetSpecularTint(m_specularTint);
	nodeBsdf.SetAnisotropic(m_anisotropic);
	nodeBsdf.SetAnisotropicRotation(m_anisotropicRotation);
	nodeBsdf.SetSheen(m_sheen);
	nodeBsdf.SetSheenTint(m_sheenTint);
	nodeBsdf.SetClearcoat(m_clearcoat);
	nodeBsdf.SetClearcoatRoughness(m_clearcoatRoughness);
	nodeBsdf.SetIOR(m_ior);
	nodeBsdf.SetTransmission(m_transmission);
	nodeBsdf.SetTransmissionRoughness(m_transmissionRoughness);

	// Subsurface scattering
	nodeBsdf.SetSubsurface(m_subsurface);
	nodeBsdf.SetSubsurfaceColor(m_subsurfaceColor);
	nodeBsdf.SetSubsurfaceMethod(m_subsurfaceMethod);
	nodeBsdf.SetSubsurfaceRadius(m_subsurfaceRadius);

	// Albedo map
	LinkAlbedo(nodeBsdf.inBaseColor,nodeBsdf.inAlpha);

	// Normal map
	if(AddNormalMap(cclShader).has_value())
		LinkNormal(nodeBsdf.inNormal);

	// Metalness map
	if(AddMetalnessMap(cclShader).has_value())
		LinkMetalness(nodeBsdf.inMetallic);

	// Roughness map
	if(AddRoughnessMap(cclShader).has_value())
		LinkRoughness(nodeBsdf.inRoughness);

	// Emission map
	if(AddEmissionMap(cclShader).has_value())
		LinkEmission(nodeBsdf.inEmission);

	// Wrinkle maps
	if(m_wrinkleCompressMap.has_value())
	{
		// TODO
	}

	if(m_wrinkleStretchMap.has_value())
	{
		// TODO
	}

	enum class DebugMode : uint8_t
	{
		None = 0u,
		Metalness,
		Specular,
		Albedo,
		Normal,
		Roughness,
		Emission,
		Subsurface
	};

	static auto debugMode = DebugMode::None;
	switch(debugMode)
	{
	case DebugMode::Metalness:
	{
		auto color = cclShader.AddCombineRGBNode();
		LinkMetalness(color.inR);
		LinkMetalness(color.inG);
		LinkMetalness(color.inB);
		cclShader.Link(color,cclShader.GetOutputNode().inSurface);
		return true;
	}
	case DebugMode::Specular:
		cclShader.Link(cclShader.AddCombineRGBNode(m_specular,m_specular,m_specular),cclShader.GetOutputNode().inSurface);
		return true;
	case DebugMode::Albedo:
		LinkAlbedoToBSDF(cclShader.GetOutputNode().inSurface);
		return true;
	case DebugMode::Normal:
		LinkNormalToBSDF(cclShader.GetOutputNode().inSurface);
		return true;
	case DebugMode::Roughness:
	{
		auto color = cclShader.AddCombineRGBNode();
		LinkRoughness(color.inR);
		LinkRoughness(color.inG);
		LinkRoughness(color.inB);
		cclShader.Link(color,cclShader.GetOutputNode().inSurface);
		return true;
	}
	case DebugMode::Emission:
	{
		auto color = cclShader.AddCombineRGBNode();
		LinkEmission(color.inR);
		LinkEmission(color.inG);
		LinkEmission(color.inB);
		cclShader.Link(color,cclShader.GetOutputNode().inSurface);
		return true;
	}
	case DebugMode::Subsurface:
	{
		auto color = cclShader.AddCombineRGBNode(m_subsurfaceColor.r,m_subsurfaceColor.g,m_subsurfaceColor.b);
		cclShader.Link(color,cclShader.GetOutputNode().inSurface);
		return true;
	}
	}

	cclShader.Link(nodeBsdf,cclShader.GetOutputNode().inSurface);
	return true;
}

////////////////

cycles::PShaderNode cycles::ShaderNode::Create(CCLShader &shader,ccl::ShaderNode &shaderNode)
{
	return PShaderNode{new ShaderNode{shader,shaderNode}};
}
cycles::ShaderNode::ShaderNode(CCLShader &shader,ccl::ShaderNode &shaderNode)
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

///////////

cycles::UVHandlerEye::UVHandlerEye(const Vector4 &irisProjU,const Vector4 &irisProjV,float dilationFactor,float maxDilationFactor,float irisUvRadius)
	: m_irisProjU{irisProjU},m_irisProjV{irisProjV},m_dilationFactor{dilationFactor},m_maxDilationFactor{maxDilationFactor},m_irisUvRadius{irisUvRadius}
{}
std::optional<cycles::Socket> cycles::UVHandlerEye::InitializeNodes(CCLShader &shader)
{
	auto nodeGeometry = shader.AddGeometryNode();
	auto nodeSeparateXYZ = shader.AddSeparateXYZNode(nodeGeometry.outPosition);
	auto cyclesUnitsToPragma = static_cast<float>(util::metres_to_units(1.f));
	auto x = nodeSeparateXYZ.outX *cyclesUnitsToPragma;
	auto y = nodeSeparateXYZ.outY *cyclesUnitsToPragma;
	auto z = nodeSeparateXYZ.outZ *cyclesUnitsToPragma;
	auto nodeUvX = NumberSocket::dot({x,y,z,1.f},{m_irisProjU.x,m_irisProjU.y,m_irisProjU.z,m_irisProjU.w});
	auto nodeUvY = NumberSocket::dot({x,y,z,1.f},{m_irisProjV.x,m_irisProjV.y,m_irisProjV.z,m_irisProjV.w});
		
	// Pupil dilation
	auto pupilCenterToBorder = (NumberSocket::len({nodeUvX,nodeUvY}) /m_irisUvRadius).clamp(0.f,1.f);
	auto factor = shader.AddConstantNode(1.f).lerp(pupilCenterToBorder,umath::clamp(m_dilationFactor,0.f,m_maxDilationFactor) *2.5f -1.25f);
	nodeUvX = nodeUvX *factor;
	nodeUvY = nodeUvY *factor;

	nodeUvX = (nodeUvX +1.f) /2.f;
	nodeUvY = (nodeUvY +1.f) /2.f;
	return shader.AddCombineXYZNode(nodeUvX,nodeUvY);
};
#pragma optimize("",on)
