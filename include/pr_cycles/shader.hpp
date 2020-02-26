#ifndef __PR_CYCLES_SHADER_HPP__
#define __PR_CYCLES_SHADER_HPP__

#include "scene_object.hpp"
#include "nodes.hpp"
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <mathutil/umath.h>
#include <sharedutils/util_event_reply.hpp>

namespace ccl
{
	class Scene; class Shader; class ShaderGraph; class ShaderNode; class ShaderInput; class ShaderOutput;
	enum NodeMathType : int32_t;
	enum AttributeStandard : int32_t;
};
namespace pragma::modules::cycles
{
	class Scene;
	class Shader;
	class ShaderNode;
	using PShaderNode = std::shared_ptr<ShaderNode>;
	using PShader = std::shared_ptr<Shader>;
	class Socket;
	class CCLShader;
	class UVHandler;
	class Shader
		: public SceneObject,
		public std::enable_shared_from_this<Shader>
	{
	public:
		template<class TShader>
			static std::shared_ptr<TShader> Create(Scene &scene,const std::string &name);

		enum class Flags : uint8_t
		{
			None = 0u,
			Transparent = 1u,
			EmissionFromAlbedoAlpha = Transparent<<1u
		};

		enum class TextureType : uint8_t
		{
			Albedo = 0u,
			Normal,
			Roughness,
			Metalness,
			Emission,
			Specular,
			
			Count
		};

		util::WeakHandle<Shader> GetHandle();

		virtual void DoFinalize() override;

		Scene &GetScene() const;
		const std::string &GetName() const;
		const std::string &GetMeshName() const;
		void SetMeshName(const std::string &meshName);
		bool HasFlag(Flags flags) const;
		void SetFlags(Flags flags,bool enabled);
		void SetUVHandler(TextureType type,const std::shared_ptr<UVHandler> &uvHandler);
		const std::shared_ptr<UVHandler> &GetUVHandler(TextureType type) const;

		void SetUVHandlers(const std::array<std::shared_ptr<UVHandler>,umath::to_integral(TextureType::Count)> &handlers);
		const std::array<std::shared_ptr<UVHandler>,umath::to_integral(TextureType::Count)> &GetUVHandlers() const;

		std::shared_ptr<CCLShader> GenerateCCLShader();
		std::shared_ptr<CCLShader> GenerateCCLShader(ccl::Shader &cclShader);
	protected:
		Shader(Scene &scene,const std::string &name);
		bool SetupCCLShader(CCLShader &cclShader);
		virtual bool InitializeCCLShader(CCLShader &cclShader)=0;

		std::string m_name;
		std::string m_meshName;
		Flags m_flags = Flags::None;
		Scene &m_scene;
		std::array<std::shared_ptr<UVHandler>,umath::to_integral(TextureType::Count)> m_uvHandlers = {};
	};

	class CCLShader
		: public std::enable_shared_from_this<CCLShader>
	{
	public:
		static std::shared_ptr<CCLShader> Create(Shader &shader);
		static std::shared_ptr<CCLShader> Create(Shader &shader,ccl::Shader &cclShader);

		~CCLShader();
		void Finalize();
		PShaderNode AddNode(const std::string &type,const std::string &name);
		PShaderNode FindNode(const std::string &name) const;
		bool Link(
			const std::string &fromNodeName,const std::string &fromSocketName,
			const std::string &toNodeName,const std::string &toSocketName,
			bool breakExistingLinks=false
		);
		bool Link(const Socket &fromSocket,const Socket &toSocket,bool breakExistingLinks=false);
		bool Link(const NumberSocket &fromSocket,const Socket &toSocket);
		bool Link(const Socket &fromSocket,const NumberSocket &toSocket);
		bool Link(const NumberSocket &fromSocket,const NumberSocket &toSocket);
		void Disconnect(const Socket &socket);
		bool ValidateSocket(const std::string &nodeName,const std::string &socketName,bool output=true) const;

		OutputNode GetOutputNode() const;
		MathNode AddMathNode();
		MathNode AddMathNode(const NumberSocket &n0,const NumberSocket &n1,ccl::NodeMathType mathOp);
		MathNode AddConstantNode(float f);
		ImageTextureNode AddColorImageTextureNode(const std::string &fileName,const std::optional<Socket> &uvSocket={});
		ImageTextureNode AddGradientImageTextureNode(const std::string &fileName,const std::optional<Socket> &uvSocket={});
		NormalMapNode AddNormalMapImageTextureNode(const std::string &fileName,const std::string &meshName,const std::optional<Socket> &uvSocket={},NormalMapNode::Space space=NormalMapNode::Space::Tangent);
		EnvironmentTextureNode AddEnvironmentTextureNode(const std::string &fileName);
		SeparateXYZNode AddSeparateXYZNode(const Socket &srcSocket);
		CombineXYZNode AddCombineXYZNode(const std::optional<const NumberSocket> &x={},const std::optional<const NumberSocket> &y={},const std::optional<const NumberSocket> &z={});
		SeparateRGBNode AddSeparateRGBNode(const Socket &srcSocket);
		CombineRGBNode AddCombineRGBNode(const std::optional<const NumberSocket> &r={},const std::optional<const NumberSocket> &g={},const std::optional<const NumberSocket> &b={});
		GeometryNode AddGeometryNode();
		NormalMapNode AddNormalMapNode();
		MixClosureNode AddMixClosureNode();
		MixNode AddMixNode(const Socket &socketColor1,const Socket &socketColor2,MixNode::Type type=MixNode::Type::Mix,const std::optional<const NumberSocket> &fac={});
		BackgroundNode AddBackgroundNode();
		TextureCoordinateNode AddTextureCoordinateNode();
		MappingNode AddMappingNode();
		ColorNode AddColorNode();
		AttributeNode AddAttributeNode(ccl::AttributeStandard attrType);
		EmissionNode AddEmissionNode();
		NumberSocket AddVertexAlphaNode();

		PrincipledBSDFNode AddPrincipledBSDFNode();
		ToonBSDFNode AddToonBSDFNode();
		GlassBSDFNode AddGlassBSDFNode();
		TransparentBsdfNode AddTransparentBSDFNode();
		MixClosureNode AddTransparencyClosure(const Socket &colorSocket,const NumberSocket &alphaSocket);

		Shader &GetShader() const;
		const std::optional<Socket> &GetUVSocket(Shader::TextureType type) const;

		ccl::Shader *operator->();
		ccl::Shader *operator*();
	protected:
		CCLShader(Shader &shader,ccl::Shader &cclShader,ccl::ShaderGraph &cclShaderGraph);
		ImageTextureNode AddImageTextureNode(const std::string &fileName,const std::optional<Socket> &uvSocket,bool color);
		std::string GetCurrentInternalNodeName() const;
	private:
		friend Shader;
		Shader &m_shader;
		ccl::Shader &m_cclShader;
		ccl::ShaderGraph &m_cclGraph;
		std::vector<PShaderNode> m_nodes = {};
		std::array<std::optional<Socket>,umath::to_integral(Shader::TextureType::Count)> m_uvSockets = {};
		bool m_bDeleteGraphIfUnused = false;
	};

	class ShaderNode
		: public std::enable_shared_from_this<ShaderNode>
	{
	public:
		static PShaderNode Create(CCLShader &shader,ccl::ShaderNode &shaderNode);
		util::WeakHandle<ShaderNode> GetHandle();
		ccl::ShaderNode *operator->();
		ccl::ShaderNode *operator*();

		template<typename T>
			bool SetInputArgument(const std::string &inputName,const T &arg);
	private:
		friend CCLShader;
		ShaderNode(CCLShader &shader,ccl::ShaderNode &shaderNode);
		ccl::ShaderInput *FindInput(const std::string &inputName);
		ccl::ShaderOutput *FindOutput(const std::string &outputName);
		CCLShader &m_shader;
		ccl::ShaderNode &m_shaderNode;
	};

	class ShaderAlbedoSet
	{
	public:
		virtual ~ShaderAlbedoSet()=default;
		void SetAlbedoMap(const std::string &albedoMap);
		const std::optional<std::string> &GetAlbedoMap() const;
		const std::optional<ImageTextureNode> &GetAlbedoNode() const;

		std::optional<ImageTextureNode> AddAlbedoMap(CCLShader &shader);
	private:
		std::optional<std::string> m_albedoMap;
		std::optional<ImageTextureNode> m_albedoNode = {};
	};

	class ShaderModuleAlbedo
	{
	public:
		virtual ~ShaderModuleAlbedo()=default;

		void SetEmissionFromAlbedoAlpha(Shader &shader,bool b);
		void SetTransparent(Shader &shader,bool transparent);

		const ShaderAlbedoSet &GetAlbedoSet() const;
		ShaderAlbedoSet &GetAlbedoSet();

		const ShaderAlbedoSet &GetAlbedoSet2() const;
		ShaderAlbedoSet &GetAlbedoSet2();

		void SetUseVertexAlphasForBlending(bool useAlphasForBlending);
		bool ShouldUseVertexAlphasForBlending() const;

		void LinkAlbedo(const Socket &color,const NumberSocket &alpha,bool useAlphaIfFlagSet=true);
		void LinkAlbedoToBSDF(const Socket &bsdf);
	private:
		bool SetupAlbedoNodes(CCLShader &shader,Socket &outColor,NumberSocket &outAlpha);
		ShaderAlbedoSet m_albedoSet = {};
		ShaderAlbedoSet m_albedoSet2 = {};
		bool m_useVertexAlphasForBlending = false;
	};

	class ShaderModuleNormal
		: public ShaderModuleAlbedo
	{
	public:
		void SetNormalMap(const std::string &normalMap);
		const std::optional<std::string> &GetNormalMap() const;

		void SetNormalMapSpace(NormalMapNode::Space space);
		NormalMapNode::Space GetNormalMapSpace() const;

		std::optional<Socket> AddNormalMap(CCLShader &shader);
		void LinkNormal(const Socket &normal);
		void LinkNormalToBSDF(const Socket &bsdf);
	private:
		std::optional<std::string> m_normalMap;
		std::optional<Socket> m_normalSocket = {};
		NormalMapNode::Space m_space = NormalMapNode::Space::Tangent;
	};

	class ShaderModuleMetalness
	{
	public:
		virtual ~ShaderModuleMetalness()=default;
		void SetMetalnessMap(const std::string &metalnessMap);
		void SetMetalnessFactor(float metalnessFactor);

		std::optional<NumberSocket> AddMetalnessMap(CCLShader &shader);
		void LinkMetalness(const NumberSocket &metalness);
	private:
		std::optional<std::string> m_metalnessMap;
		std::optional<NumberSocket> m_metalnessSocket = {};
		std::optional<float> m_metalnessFactor = {};
	};

	class ShaderModuleRoughness
	{
	public:
		virtual ~ShaderModuleRoughness()=default;
		void SetRoughnessMap(const std::string &roughnessMap);
		void SetSpecularMap(const std::string &specularMap);

		void SetRoughnessFactor(float roughness);

		std::optional<NumberSocket> AddRoughnessMap(CCLShader &shader);
		void LinkRoughness(const NumberSocket &roughness);
	private:
		std::optional<std::string> m_roughnessMap;
		std::optional<std::string> m_specularMap;

		std::optional<NumberSocket> m_roughnessSocket = {};
		std::optional<float> m_roughnessFactor = {};
	};

	class ShaderModuleEmission
	{
	public:
		virtual ~ShaderModuleEmission()=default;
		void SetEmissionMap(const std::string &emissionMap);
		void SetEmissionFactor(float factor);

		std::optional<Socket> AddEmissionMap(CCLShader &shader);
		void LinkEmission(const Socket &emission);
	private:
		std::optional<std::string> m_emissionMap;
		float m_emissionFactor = 1.f;

		std::optional<Socket> m_emissionSocket = {};
	};

	class ShaderGeneric
		: public Shader
	{
	protected:
		virtual bool InitializeCCLShader(CCLShader &cclShader) override;
		using Shader::Shader;
	};

	class ShaderAlbedo
		: public Shader,
		public ShaderModuleAlbedo
	{
	protected:
		virtual bool InitializeCCLShader(CCLShader &cclShader) override;
		using Shader::Shader;
	};

	class ShaderColorTest
		: public Shader
	{
	protected:
		virtual bool InitializeCCLShader(CCLShader &cclShader) override;
		using Shader::Shader;
	};

	class ShaderNormal
		: public Shader,
		public ShaderModuleNormal
	{
	protected:
		virtual bool InitializeCCLShader(CCLShader &cclShader) override;
		using Shader::Shader;
	};

	class ShaderToon
		: public Shader,
		public ShaderModuleNormal
	{
	protected:
		virtual bool InitializeCCLShader(CCLShader &cclShader) override;
		using Shader::Shader;
	};

	class ShaderGlass
		: public Shader,
		public ShaderModuleNormal,
		public ShaderModuleRoughness
	{
	protected:
		virtual bool InitializeCCLShader(CCLShader &cclShader) override;
		using Shader::Shader;
	};

	class ShaderPBR
		: public Shader,
		public ShaderModuleNormal,
		public ShaderModuleRoughness,
		public ShaderModuleMetalness,
		public ShaderModuleEmission
	{
	public:
		void SetWrinkleStretchMap(const std::string &wrinkleStretchMap);
		void SetWrinkleCompressMap(const std::string &wrinkleCompressMap);

		void SetMetallic(float metallic);
		void SetSpecular(float specular);
		void SetSpecularTint(float specularTint);
		void SetAnisotropic(float anisotropic);
		void SetAnisotropicRotation(float anisotropicRotation);
		void SetSheen(float sheen);
		void SetSheenTint(float sheenTint);
		void SetClearcoat(float clearcoat);
		void SetClearcoatRoughness(float clearcoatRoughness);
		void SetIOR(float ior);
		void SetTransmission(float transmission);
		void SetTransmissionRoughness(float transmissionRoughness);

		// Subsurface scattering
		void SetSubsurface(float subsurface);
		void SetSubsurfaceColor(const Vector3 &color);
		void SetSubsurfaceMethod(PrincipledBSDFNode::SubsurfaceMethod method);
		void SetSubsurfaceRadius(const Vector3 &radius);
	protected:
		virtual bool InitializeCCLShader(CCLShader &cclShader) override;
		virtual util::EventReply InitializeTransparency(CCLShader &cclShader,ImageTextureNode &albedoNode,const NumberSocket &alphaSocket) const;
		using Shader::Shader;
	private:
		std::optional<std::string> m_wrinkleStretchMap;
		std::optional<std::string> m_wrinkleCompressMap;

		// Default settings (Taken from Blender)
		float m_metallic = 0.f;
		float m_specular = 0.5f;
		float m_specularTint = 0.f;
		float m_anisotropic = 0.f;
		float m_anisotropicRotation = 0.f;
		float m_sheen = 0.f;
		float m_sheenTint = 0.5f;
		float m_clearcoat = 0.f;
		float m_clearcoatRoughness = 0.03f;
		float m_ior = 1.45f;
		float m_transmission = 0.f;
		float m_transmissionRoughness = 0.f;

		// Subsurface scattering
		float m_subsurface = 0.f;
		Vector3 m_subsurfaceColor = {1.f,1.f,1.f};
		PrincipledBSDFNode::SubsurfaceMethod m_subsurfaceMethod = PrincipledBSDFNode::SubsurfaceMethod::Burley;
		Vector3 m_subsurfaceRadius = {0.f,0.f,0.f};
	};

	class ShaderParticle
		: public ShaderPBR
	{
	public:
		using ShaderPBR::ShaderPBR;
		void SetRenderFlags(uint32_t flags);
	protected:
		virtual util::EventReply InitializeTransparency(CCLShader &cclShader,ImageTextureNode &albedoNode,const NumberSocket &alphaSocket) const override;
	private:
		uint32_t m_renderFlags = 0u;
	};

	struct UVHandler
	{
		virtual std::optional<Socket> InitializeNodes(CCLShader &shader)=0;
	};

	struct UVHandlerEye
		: public UVHandler
	{
		UVHandlerEye(const Vector4 &irisProjU,const Vector4 &irisProjV,float dilationFactor,float maxDilationFactor,float irisUvRadius);
		virtual std::optional<Socket> InitializeNodes(CCLShader &shader) override;
	private:
		Vector4 m_irisProjU = {};
		Vector4 m_irisProjV = {};
		float m_dilationFactor = 0.5f;
		float m_maxDilationFactor = 1.0f;
		float m_irisUvRadius = 0.2f;
	};
};
REGISTER_BASIC_BITWISE_OPERATORS(pragma::modules::cycles::Shader::Flags)

template<class TShader>
	std::shared_ptr<TShader> pragma::modules::cycles::Shader::Create(Scene &scene,const std::string &name)
{
	auto pShader = PShader{new TShader{scene,name}};
	scene.m_shaders.push_back(pShader);
	return std::static_pointer_cast<TShader>(pShader);
}

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
