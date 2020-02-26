#include "pr_cycles/nodes.hpp"
#include "pr_cycles/scene.hpp"
#include "pr_cycles/shader.hpp"
#include <render/nodes.h>
#include <render/shader.h>

using namespace pragma::modules;

cycles::Socket::Socket()
	: m_shader{nullptr}
{}
cycles::Socket::Socket(CCLShader &shader,const std::string &nodeName,const std::string &socketName,bool output)
	: m_shader{&shader},nodeName{nodeName},socketName{socketName},m_bOutput{output}
{
	shader.ValidateSocket(nodeName,socketName,output);
}
cycles::CCLShader &cycles::Socket::GetShader() const {return *m_shader;}

bool cycles::Socket::IsOutput() const {return m_bOutput;}
bool cycles::Socket::IsInput() const {return !IsOutput();}

cycles::MathNode cycles::Socket::operator+(float value) const {return m_shader->AddMathNode(*this,value,ccl::NodeMathType::NODE_MATH_ADD);}
cycles::MathNode cycles::Socket::operator+(const Socket &socket) const {return m_shader->AddMathNode(*this,socket,ccl::NodeMathType::NODE_MATH_ADD);}

cycles::MathNode cycles::Socket::operator-(float value) const {return m_shader->AddMathNode(*this,value,ccl::NodeMathType::NODE_MATH_SUBTRACT);}
cycles::MathNode cycles::Socket::operator-(const Socket &socket) const {return m_shader->AddMathNode(*this,socket,ccl::NodeMathType::NODE_MATH_SUBTRACT);}

cycles::MathNode cycles::Socket::operator*(float value) const {return m_shader->AddMathNode(*this,value,ccl::NodeMathType::NODE_MATH_MULTIPLY);}
cycles::MathNode cycles::Socket::operator*(const Socket &socket) const {return m_shader->AddMathNode(*this,socket,ccl::NodeMathType::NODE_MATH_MULTIPLY);}

cycles::MathNode cycles::Socket::operator/(float value) const {return m_shader->AddMathNode(*this,value,ccl::NodeMathType::NODE_MATH_DIVIDE);}
cycles::MathNode cycles::Socket::operator/(const Socket &socket) const {return m_shader->AddMathNode(*this,socket,ccl::NodeMathType::NODE_MATH_DIVIDE);}

cycles::Node::Node(CCLShader &shader)
	: m_shader{&shader}
{}

cycles::CCLShader &cycles::Node::GetShader() const {return *m_shader;}

cycles::MathNode::MathNode(CCLShader &shader,const std::string &nodeName,ccl::MathNode &node)
	: Node{shader},NumberSocket{0.f},inValue1{shader,nodeName,"value1",false},inValue2{shader,nodeName,"value2",false},
	outValue{Socket{shader,nodeName,"value"}},m_node{&node}
{
	NumberSocket::m_socket = *outValue.m_socket;
	NumberSocket::m_shader = &shader;
}

cycles::NumberSocket::NumberSocket()
	: NumberSocket{0.f}
{}
cycles::NumberSocket::NumberSocket(const Socket &socket)
	: m_socket{socket}
{
	m_shader = &socket.GetShader();
}
cycles::NumberSocket::NumberSocket(float value)
	: m_value{value}
{}

cycles::CCLShader &cycles::NumberSocket::GetShader() const {return *m_shader;}
cycles::NumberSocket cycles::NumberSocket::operator+(const NumberSocket &value) const
{
	auto *shader = m_shader ? m_shader : value.m_shader;
	if(shader == nullptr)
	{
		// Both args are literal numbers; Just apply the operation directly
		return m_value +value.m_value;
	}
	return shader->AddMathNode(*this,value,ccl::NodeMathType::NODE_MATH_ADD);
}
cycles::NumberSocket cycles::NumberSocket::operator-(const NumberSocket &value) const
{
	auto *shader = m_shader ? m_shader : value.m_shader;
	if(shader == nullptr)
	{
		// Both args are literal numbers; Just apply the operation directly
		return m_value -value.m_value;
	}
	return shader->AddMathNode(*this,value,ccl::NodeMathType::NODE_MATH_SUBTRACT);
}
cycles::NumberSocket cycles::NumberSocket::operator*(const NumberSocket &value) const
{
	auto *shader = m_shader ? m_shader : value.m_shader;
	if(shader == nullptr)
	{
		// Both args are literal numbers; Just apply the operation directly
		return m_value *value.m_value;
	}
	return shader->AddMathNode(*this,value,ccl::NodeMathType::NODE_MATH_MULTIPLY);
}
cycles::NumberSocket cycles::NumberSocket::operator/(const NumberSocket &value) const
{
	auto *shader = m_shader ? m_shader : value.m_shader;
	if(shader == nullptr)
	{
		// Both args are literal numbers; Just apply the operation directly
		return m_value /value.m_value;
	}
	return shader->AddMathNode(*this,value,ccl::NodeMathType::NODE_MATH_DIVIDE);
}
cycles::NumberSocket cycles::NumberSocket::pow(const NumberSocket &exponent) const
{
	auto *shader = m_shader ? m_shader : exponent.m_shader;
	if(shader == nullptr)
	{
		// Both args are literal numbers; Just apply the operation directly
		return umath::pow(m_value,exponent.m_value);
	}
	return shader->AddMathNode(*this,exponent,ccl::NodeMathType::NODE_MATH_POWER);
}
cycles::NumberSocket cycles::NumberSocket::sqrt() const
{
	if(m_shader == nullptr)
		return umath::sqrt(m_value);
	return m_shader->AddMathNode(*this,0.f,ccl::NodeMathType::NODE_MATH_SQRT);
}
cycles::NumberSocket cycles::NumberSocket::clamp(const NumberSocket &min,const NumberSocket &max) const
{
	auto *shader = m_shader ? m_shader : min.m_shader ? min.m_shader : max.m_shader;
	if(shader == nullptr)
	{
		// Both args are literal numbers; Just apply the operation directly
		return umath::clamp(m_value,min.m_value,max.m_value);
	}
	return shader->AddMathNode(
		shader->AddMathNode(*this,min,ccl::NodeMathType::NODE_MATH_MAXIMUM),
		max,
		ccl::NodeMathType::NODE_MATH_MINIMUM
	);
}
cycles::NumberSocket cycles::NumberSocket::lerp(const NumberSocket &to,const NumberSocket &by) const
{
	return *this *(1.f -by) +to *by;
}
cycles::NumberSocket cycles::NumberSocket::len(const std::array<const NumberSocket,2> &v)
{
	return (v.at(0) *v.at(0) +v.at(1) *v.at(1)).sqrt();
}
cycles::NumberSocket cycles::NumberSocket::dot(
	const std::array<const NumberSocket,4> &v0,
	const std::array<const NumberSocket,4> &v1
)
{
	return v0.at(0) *v1.at(0) +
		v0.at(1) *v1.at(1) +
		v0.at(2) *v1.at(2) +
		v0.at(3) *v1.at(3);
}

void cycles::MathNode::SetValue1(float value) {m_node->value1 = value;}
void cycles::MathNode::SetValue2(float value) {m_node->value2 = value;}
cycles::MathNode::operator const cycles::NumberSocket&() const {return outValue;}
cycles::NumberSocket operator+(float value,const cycles::NumberSocket &socket) {return socket +value;}
cycles::NumberSocket operator-(float value,const cycles::NumberSocket &socket)
{
	if(&socket.GetShader() == nullptr)
		return value -socket.m_value;
	return socket.GetShader().AddMathNode(value,socket,ccl::NodeMathType::NODE_MATH_SUBTRACT);
}
cycles::NumberSocket operator*(float value,const cycles::NumberSocket &socket) {return socket *value;}
cycles::NumberSocket operator/(float value,const cycles::NumberSocket &socket)
{
	if(&socket.GetShader() == nullptr)
		return value /socket.m_value;
	return socket.GetShader().AddMathNode(value,socket,ccl::NodeMathType::NODE_MATH_DIVIDE);
}

cycles::SeparateXYZNode::SeparateXYZNode(CCLShader &shader,const std::string &nodeName,ccl::SeparateXYZNode &node)
	: Node{shader},inVector{shader,nodeName,"vector",false},outX{Socket{shader,nodeName,"x"}},outY{Socket{shader,nodeName,"y"}},outZ{Socket{shader,nodeName,"z"}},
	m_node{&node}
{}

void cycles::SeparateXYZNode::SetVector(const Vector3 &v) {m_node->vector = {v.x,v.y,v.z};}

cycles::CombineXYZNode::CombineXYZNode(CCLShader &shader,const std::string &nodeName,ccl::CombineXYZNode &node)
	: Node{shader},outVector{shader,nodeName,"vector"},inX{shader,nodeName,"x",false},inY{shader,nodeName,"y",false},inZ{shader,nodeName,"z",false},
	m_node{&node}
{}

void cycles::CombineXYZNode::SetX(float x) {m_node->x = x;}
void cycles::CombineXYZNode::SetY(float y) {m_node->y = y;}
void cycles::CombineXYZNode::SetZ(float z) {m_node->z = z;}

cycles::CombineXYZNode::operator const cycles::Socket&() const {return outVector;}

cycles::SeparateRGBNode::SeparateRGBNode(CCLShader &shader,const std::string &nodeName,ccl::SeparateRGBNode &node)
	: Node{shader},inColor{shader,nodeName,"color",false},outR{Socket{shader,nodeName,"r"}},outG{Socket{shader,nodeName,"g"}},outB{Socket{shader,nodeName,"b"}},
	m_node{&node}
{}

void cycles::SeparateRGBNode::SetColor(const Vector3 &c) {m_node->color = {c.r,c.g,c.b};}

cycles::CombineRGBNode::CombineRGBNode(CCLShader &shader,const std::string &nodeName,ccl::CombineRGBNode &node)
	: Node{shader},outColor{shader,nodeName,"image"},inR{shader,nodeName,"r",false},inG{shader,nodeName,"g",false},inB{shader,nodeName,"b",false},
	m_node{&node}
{}

cycles::CombineRGBNode::operator const cycles::Socket&() const {return outColor;}

void cycles::CombineRGBNode::SetR(float r) {m_node->r = r;}
void cycles::CombineRGBNode::SetG(float g) {m_node->g = g;}
void cycles::CombineRGBNode::SetB(float b) {m_node->b = b;}

cycles::GeometryNode::GeometryNode(CCLShader &shader,const std::string &nodeName)
	: Node{shader},inNormal{shader,nodeName,"normal_osl",false},
	outPosition{shader,nodeName,"position"},
	outNormal{shader,nodeName,"normal"},
	outTangent{shader,nodeName,"tangent"},
	outTrueNormal{shader,nodeName,"true_normal"},
	outIncoming{shader,nodeName,"incoming"},
	outParametric{shader,nodeName,"parametric"},
	outBackfacing{Socket{shader,nodeName,"backfacing"}},
	outPointiness{Socket{shader,nodeName,"pointiness"}}
{}

cycles::ImageTextureNode::ImageTextureNode(CCLShader &shader,const std::string &nodeName)
	: Node{shader},inUVW{shader,nodeName,"vector",false},outColor{shader,nodeName,"color"},outAlpha{Socket{shader,nodeName,"alpha"}}
{}

cycles::ImageTextureNode::operator const cycles::Socket&() const {return outColor;}

cycles::EnvironmentTextureNode::EnvironmentTextureNode(CCLShader &shader,const std::string &nodeName)
	: Node{shader},inVector{shader,nodeName,"vector",false},outColor{shader,nodeName,"color"},outAlpha{Socket{shader,nodeName,"alpha"}}
{}

cycles::EnvironmentTextureNode::operator const cycles::Socket&() const {return outColor;}

cycles::MixClosureNode::MixClosureNode(CCLShader &shader,const std::string &nodeName,ccl::MixClosureNode &node)
	: Node{shader},inFac{Socket{shader,nodeName,"fac",false}},inClosure1{shader,nodeName,"closure1",false},inClosure2{shader,nodeName,"closure2",false},outClosure{shader,nodeName,"closure"},
	m_node{&node}
{}

cycles::MixClosureNode::operator const cycles::Socket&() const {return outClosure;}

void cycles::MixClosureNode::SetFactor(float fac) {m_node->fac = fac;}

cycles::BackgroundNode::BackgroundNode(CCLShader &shader,const std::string &nodeName,ccl::BackgroundNode &node)
	: Node{shader},inColor{shader,nodeName,"color",false},inStrength{Socket{shader,nodeName,"strength",false}},inSurfaceMixWeight{Socket{shader,nodeName,"surface_mix_weight",false}},
	outBackground{shader,nodeName,"background"},m_node{&node}
{}

cycles::BackgroundNode::operator const cycles::Socket&() const {return outBackground;}

void cycles::BackgroundNode::SetColor(const Vector3 &color) {m_node->color = {color.r,color.g,color.b};}
void cycles::BackgroundNode::SetStrength(float strength) {m_node->strength = strength;}
void cycles::BackgroundNode::SetSurfaceMixWeight(float surfaceMixWeight) {m_node->surface_mix_weight = surfaceMixWeight;}

cycles::TextureCoordinateNode::TextureCoordinateNode(CCLShader &shader,const std::string &nodeName,ccl::TextureCoordinateNode &node)
	: Node{shader},inNormal{shader,nodeName,"normal_osl",false},outGenerated{shader,nodeName,"generated"},outNormal{shader,nodeName,"normal"},
	outUv{shader,nodeName,"uv"},outObject{shader,nodeName,"object"},outCamera{shader,nodeName,"camera"},outWindow{shader,nodeName,"window"},
	outReflection{shader,nodeName,"reflection"},m_node{&node}
{}

cycles::MappingNode::MappingNode(CCLShader &shader,const std::string &nodeName,ccl::MappingNode &node)
	: Node{shader},inVector{shader,nodeName,"vector",false},outVector{shader,nodeName,"vector"},m_node{&node}
{}

cycles::MappingNode::operator const cycles::Socket&() const {return outVector;}

void cycles::MappingNode::SetType(Type type)
{
	switch(type)
	{
	case Type::Point:
		m_node->tex_mapping.type = ccl::TextureMapping::Type::POINT;
		break;
	case Type::Texture:
		m_node->tex_mapping.type = ccl::TextureMapping::Type::TEXTURE;
		break;
	case Type::Vector:
		m_node->tex_mapping.type = ccl::TextureMapping::Type::VECTOR;
		break;
	case Type::Normal:
		m_node->tex_mapping.type = ccl::TextureMapping::Type::NORMAL;
		break;
	}
}
void cycles::MappingNode::SetRotation(const EulerAngles &ang)
{
	auto cclAngles = ang;
	cclAngles.p -= 90.f;
	m_node->tex_mapping.rotation = {
		static_cast<float>(umath::deg_to_rad(-cclAngles.p)),
		static_cast<float>(umath::deg_to_rad(cclAngles.r)),
		static_cast<float>(umath::deg_to_rad(-cclAngles.y))
	};
}

cycles::EmissionNode::EmissionNode(CCLShader &shader,const std::string &nodeName)
	: Node{shader},inColor{shader,nodeName,"color",false},inStrength{Socket{shader,nodeName,"strength",false}},inSurfaceMixWeight{Socket{shader,nodeName,"surface_mix_weight",false}},
	outEmission{shader,nodeName,"emission"}
{}

cycles::EmissionNode::operator const cycles::Socket&() const {return outEmission;}

cycles::ColorNode::ColorNode(CCLShader &shader,const std::string &nodeName,ccl::ColorNode &node)
	: Node{shader},outColor{shader,nodeName,"color"},
	m_node{&node}
{}

cycles::ColorNode::operator const cycles::Socket&() const {return outColor;}

void cycles::ColorNode::SetColor(const Vector3 &color) {m_node->value = {color.r,color.g,color.b};}

::
cycles::AttributeNode::AttributeNode(CCLShader &shader,const std::string &nodeName,ccl::AttributeNode &node)
	: Node{shader},outColor{shader,nodeName,"color"},outVector{shader,nodeName,"vector"},outFactor{shader,nodeName,"fac"},
	m_node{&node}
{}
void cycles::AttributeNode::SetAttribute(ccl::AttributeStandard attrType)
{
	m_node->attribute = ccl::Attribute::standard_name(attrType);
}

cycles::MixNode::MixNode(CCLShader &shader,const std::string &nodeName,ccl::MixNode &node)
	: Node{shader},inFac{Socket{shader,nodeName,"fac",false}},inColor1{shader,nodeName,"color1",false},inColor2{shader,nodeName,"color2",false},outColor{shader,nodeName,"color"},
	m_node{&node}
{}

cycles::MixNode::operator const cycles::Socket&() const {return outColor;}

void cycles::MixNode::SetType(Type type)
{
	switch(type)
	{
	case Type::Mix:
		m_node->type = ccl::NODE_MIX_BLEND;
		break;
	case Type::Add:
		m_node->type = ccl::NODE_MIX_ADD;
		break;
	case Type::Multiply:
		m_node->type = ccl::NODE_MIX_MUL;
		break;
	case Type::Screen:
		m_node->type = ccl::NODE_MIX_SCREEN;
		break;
	case Type::Overlay:
		m_node->type = ccl::NODE_MIX_OVERLAY;
		break;
	case Type::Subtract:
		m_node->type = ccl::NODE_MIX_SUB;
		break;
	case Type::Divide:
		m_node->type = ccl::NODE_MIX_DIV;
		break;
	case Type::Difference:
		m_node->type = ccl::NODE_MIX_DIFF;
		break;
	case Type::Darken:
		m_node->type = ccl::NODE_MIX_DARK;
		break;
	case Type::Lighten:
		m_node->type = ccl::NODE_MIX_LIGHT;
		break;
	case Type::Dodge:
		m_node->type = ccl::NODE_MIX_DODGE;
		break;
	case Type::Burn:
		m_node->type = ccl::NODE_MIX_BURN;
		break;
	case Type::Hue:
		m_node->type = ccl::NODE_MIX_HUE;
		break;
	case Type::Saturation:
		m_node->type = ccl::NODE_MIX_SAT;
		break;
	case Type::Value:
		m_node->type = ccl::NODE_MIX_VAL;
		break;
	case Type::Color:
		m_node->type = ccl::NODE_MIX_COLOR;
		break;
	case Type::SoftLight:
		m_node->type = ccl::NODE_MIX_SOFT;
		break;
	case Type::LinearLight:
		m_node->type = ccl::NODE_MIX_LINEAR;
		break;
	}
}
void cycles::MixNode::SetUseClamp(bool useClamp) {m_node->use_clamp = useClamp;}
void cycles::MixNode::SetFactor(float fac) {m_node->fac = fac;}
void cycles::MixNode::SetColor1(const Vector3 &color1) {m_node->color1 = {color1.r,color1.g,color1.b};}
void cycles::MixNode::SetColor2(const Vector3 &color2) {m_node->color2 = {color2.r,color2.g,color2.b};}

cycles::TransparentBsdfNode::TransparentBsdfNode(CCLShader &shader,const std::string &nodeName,ccl::TransparentBsdfNode &node)
	: Node{shader},inColor{shader,nodeName,"color",false},inSurfaceMixWeight{Socket{shader,nodeName,"surface_mix_weight",false}},outBsdf{shader,nodeName,"bsdf"},
	m_node{&node}
{}

cycles::TransparentBsdfNode::operator const cycles::Socket&() const {return outBsdf;}

void cycles::TransparentBsdfNode::SetColor(const Vector3 &color) {m_node->color = {color.r,color.g,color.b};}
void cycles::TransparentBsdfNode::SetSurfaceMixWeight(float weight) {m_node->surface_mix_weight = weight;}

cycles::NormalMapNode::NormalMapNode(CCLShader &shader,const std::string &nodeName,ccl::NormalMapNode &normalMapNode)
	: Node{shader},inStrength{Socket{shader,nodeName,"strength",false}},inColor{shader,nodeName,"color",false},outNormal{shader,nodeName,"normal"},
	m_node{&normalMapNode}
{}

cycles::NormalMapNode::operator const cycles::Socket&() const {return outNormal;}

void cycles::NormalMapNode::SetStrength(float strength) {m_node->strength = strength;}
void cycles::NormalMapNode::SetColor(const Vector3 &color) {m_node->color = {color.r,color.g,color.b};}
void cycles::NormalMapNode::SetSpace(Space space)
{
	switch(space)
	{
	case Space::Tangent:
		m_node->space = ccl::NodeNormalMapSpace::NODE_NORMAL_MAP_TANGENT;
		break;
	case Space::Object:
		m_node->space = ccl::NodeNormalMapSpace::NODE_NORMAL_MAP_OBJECT;
		break;
	case Space::World:
		m_node->space = ccl::NodeNormalMapSpace::NODE_NORMAL_MAP_WORLD;
		break;
	}
}
void cycles::NormalMapNode::SetAttribute(const std::string &attribute) {m_node->attribute = attribute;}

cycles::PrincipledBSDFNode::PrincipledBSDFNode(CCLShader &shader,const std::string &nodeName,ccl::PrincipledBsdfNode &principledBSDF)
	: Node{shader},
	inBaseColor{shader,nodeName,"base_color",false},
	inSubsurfaceColor{shader,nodeName,"subsurface_color",false},
	inMetallic{Socket{shader,nodeName,"metallic",false}},
	inSubsurface{Socket{shader,nodeName,"subsurface",false}},
	inSubsurfaceRadius{Socket{shader,nodeName,"subsurface_radius",false}},
	inSpecular{Socket{shader,nodeName,"specular",false}},
	inRoughness{Socket{shader,nodeName,"roughness",false}},
	inSpecularTint{Socket{shader,nodeName,"specular_tint",false}},
	inAnisotropic{Socket{shader,nodeName,"anisotropic",false}},
	inSheen{Socket{shader,nodeName,"sheen",false}},
	inSheenTint{Socket{shader,nodeName,"sheen_tint",false}},
	inClearcoat{Socket{shader,nodeName,"clearcoat",false}},
	inClearcoatRoughness{Socket{shader,nodeName,"clearcoat_roughness",false}},
	inIOR{Socket{shader,nodeName,"ior",false}},
	inTransmission{Socket{shader,nodeName,"transmission",false}},
	inTransmissionRoughness{Socket{shader,nodeName,"transmission_roughness",false}},
	inAnisotropicRotation{Socket{shader,nodeName,"anisotropic_rotation",false}},
	inEmission{shader,nodeName,"emission",false},
	inAlpha{Socket{shader,nodeName,"alpha",false}},
	inNormal{shader,nodeName,"normal",false},
	inClearcoatNormal{shader,nodeName,"clearcoat_normal",false},
	inTangent{shader,nodeName,"tangent",false},
	inSurfaceMixWeight{Socket{shader,nodeName,"surface_mix_weight",false}},
	outBSDF{shader,nodeName,"bsdf"},
	m_node{&principledBSDF}
{}

cycles::PrincipledBSDFNode::operator const cycles::Socket&() const {return outBSDF;}

void cycles::PrincipledBSDFNode::SetBaseColor(const Vector3 &color) {m_node->base_color = {color.r,color.g,color.b};}
void cycles::PrincipledBSDFNode::SetSubsurfaceColor(const Vector3 &color) {m_node->subsurface_color = {color.r,color.g,color.b};}
void cycles::PrincipledBSDFNode::SetMetallic(float metallic) {m_node->metallic = metallic;}
void cycles::PrincipledBSDFNode::SetSubsurface(float subsurface) {m_node->subsurface = subsurface;}
void cycles::PrincipledBSDFNode::SetSubsurfaceRadius(const Vector3 &subsurfaceRadius) {m_node->subsurface_radius = cycles::Scene::ToCyclesPosition(subsurfaceRadius);}
void cycles::PrincipledBSDFNode::SetSpecular(float specular) {m_node->specular = specular;}
void cycles::PrincipledBSDFNode::SetRoughness(float roughness) {m_node->roughness = roughness;}
void cycles::PrincipledBSDFNode::SetSpecularTint(float specularTint) {m_node->specular_tint = specularTint;}
void cycles::PrincipledBSDFNode::SetAnisotropic(float anisotropic) {m_node->anisotropic = anisotropic;}
void cycles::PrincipledBSDFNode::SetSheen(float sheen) {m_node->sheen = sheen;}
void cycles::PrincipledBSDFNode::SetSheenTint(float sheenTint) {m_node->sheen_tint = sheenTint;}
void cycles::PrincipledBSDFNode::SetClearcoat(float clearcoat) {m_node->clearcoat = clearcoat;}
void cycles::PrincipledBSDFNode::SetClearcoatRoughness(float clearcoatRoughness) {m_node->clearcoat_roughness = clearcoatRoughness;}
void cycles::PrincipledBSDFNode::SetIOR(float ior) {m_node->ior = ior;}
void cycles::PrincipledBSDFNode::SetTransmission(float transmission) {m_node->transmission = transmission;}
void cycles::PrincipledBSDFNode::SetTransmissionRoughness(float transmissionRoughness) {m_node->transmission_roughness = transmissionRoughness;}
void cycles::PrincipledBSDFNode::SetAnisotropicRotation(float anisotropicRotation) {m_node->anisotropic_rotation = anisotropicRotation;}
void cycles::PrincipledBSDFNode::SetEmission(const Vector3 &emission) {m_node->emission = {emission.r,emission.g,emission.b};}
void cycles::PrincipledBSDFNode::SetAlpha(float alpha) {m_node->alpha = alpha;}
void cycles::PrincipledBSDFNode::SetNormal(const Vector3 &normal) {m_node->normal = cycles::Scene::ToCyclesNormal(normal);}
void cycles::PrincipledBSDFNode::SetClearcoatNormal(const Vector3 &normal) {m_node->clearcoat_normal = cycles::Scene::ToCyclesNormal(normal);}
void cycles::PrincipledBSDFNode::SetTangent(const Vector3 &tangent) {m_node->tangent = cycles::Scene::ToCyclesNormal(tangent);}
void cycles::PrincipledBSDFNode::SetSurfaceMixWeight(float weight) {m_node->surface_mix_weight = weight;}
void cycles::PrincipledBSDFNode::SetDistribution(Distribution distribution)
{
	switch(distribution)
	{
	case Distribution::GGX:
		m_node->distribution = ccl::CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID;
		break;
	case Distribution::MultiscaterGGX:
		m_node->distribution = ccl::CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID;
		break;
	}
}
void cycles::PrincipledBSDFNode::SetSubsurfaceMethod(SubsurfaceMethod method)
{
	switch(method)
	{
	case SubsurfaceMethod::Burley:
		m_node->distribution = ccl::CLOSURE_BSSRDF_PRINCIPLED_ID;
		break;
	case SubsurfaceMethod::RandomWalk:
		m_node->distribution = ccl::CLOSURE_BSSRDF_PRINCIPLED_RANDOM_WALK_ID;
		break;
	}
}

cycles::ToonBSDFNode::ToonBSDFNode(CCLShader &shader,const std::string &nodeName,ccl::ToonBsdfNode &toonBsdf)
	: Node{shader},
	inColor{shader,nodeName,"color",false},
	inNormal{shader,nodeName,"normal",false},
	inSurfaceMixWeight{Socket{shader,nodeName,"surface_mix_weight",false}},
	inSize{Socket{shader,nodeName,"size",false}},
	inSmooth{Socket{shader,nodeName,"smooth",false}},
	outBSDF{shader,nodeName,"bsdf"},
	m_node{&toonBsdf}
{}

cycles::ToonBSDFNode::operator const cycles::Socket&() const {return outBSDF;}

void cycles::ToonBSDFNode::SetColor(const Vector3 &color) {m_node->color = {color.r,color.g,color.b};}
void cycles::ToonBSDFNode::SetNormal(const Vector3 &normal) {m_node->normal = Scene::ToCyclesNormal(normal);}
void cycles::ToonBSDFNode::SetSurfaceMixWeight(float weight) {m_node->surface_mix_weight = weight;}
void cycles::ToonBSDFNode::SetSize(float size) {m_node->size = size;}
void cycles::ToonBSDFNode::SetSmooth(float smooth) {m_node->smooth = smooth;}
void cycles::ToonBSDFNode::SetComponent(Component component)
{
	switch(component)
	{
	case Component::Diffuse:
		m_node->component = ccl::CLOSURE_BSDF_DIFFUSE_TOON_ID;
		break;
	case Component::Glossy:
		m_node->component = ccl::CLOSURE_BSDF_GLOSSY_TOON_ID;
		break;
	}
}

cycles::GlassBSDFNode::GlassBSDFNode(CCLShader &shader,const std::string &nodeName,ccl::GlassBsdfNode &toonBsdf)
	: Node{shader},
	inColor{shader,nodeName,"color",false},
	inNormal{shader,nodeName,"normal",false},
	inSurfaceMixWeight{Socket{shader,nodeName,"surface_mix_weight",false}},
	inRoughness{Socket{shader,nodeName,"roughness",false}},
	inIOR{Socket{shader,nodeName,"ior",false}},
	outBSDF{shader,nodeName,"bsdf"},
	m_node{&toonBsdf}
{}

cycles::GlassBSDFNode::operator const cycles::Socket&() const {return outBSDF;}

void cycles::GlassBSDFNode::SetColor(const Vector3 &color) {m_node->color = {color.r,color.g,color.b};}
void cycles::GlassBSDFNode::SetNormal(const Vector3 &normal) {m_node->normal = Scene::ToCyclesNormal(normal);}
void cycles::GlassBSDFNode::SetSurfaceMixWeight(float weight) {m_node->surface_mix_weight = weight;}
void cycles::GlassBSDFNode::SetRoughness(float roughness) {m_node->roughness = roughness;}
void cycles::GlassBSDFNode::SetIOR(float ior) {m_node->IOR = ior;}
void cycles::GlassBSDFNode::SetDistribution(Distribution distribution)
{
	switch(distribution)
	{
	case Distribution::Sharp:
		m_node->distribution = ccl::CLOSURE_BSDF_SHARP_GLASS_ID;
		break;
	case Distribution::Beckmann:
		m_node->distribution = ccl::CLOSURE_BSDF_MICROFACET_BECKMANN_GLASS_ID;
		break;
	case Distribution::GGX:
		m_node->distribution = ccl::CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID;
		break;
	case Distribution::MultiscatterGGX:
		m_node->distribution = ccl::CLOSURE_BSDF_MICROFACET_MULTI_GGX_GLASS_ID;
		break;
	}
}

cycles::OutputNode::OutputNode(CCLShader &shader,const std::string &nodeName)
	: Node{shader},
	inSurface{shader,nodeName,"surface",false},
	inVolume{shader,nodeName,"volume",false},
	inDisplacement{shader,nodeName,"displacement",false},
	inNormal{shader,nodeName,"normal",false}
{}
