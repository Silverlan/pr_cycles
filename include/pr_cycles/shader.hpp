#ifndef __PR_CYCLES_SHADER_HPP__
#define __PR_CYCLES_SHADER_HPP__

#include "scene_object.hpp"
#include "nodes.hpp"
#include <alpha_mode.hpp>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <mathutil/umath.h>
#include <mathutil/color.h>
#include <sharedutils/util_event_reply.hpp>

namespace ccl
{
	class Scene; class Shader; class ShaderGraph; class ShaderNode; class ShaderInput; class ShaderOutput;
	enum NodeMathType : int32_t;
	enum AttributeStandard : int32_t;
};
namespace pragma::modules::cycles
{
	enum class Channel : uint8_t
	{
		Red = 0,
		Green,
		Blue,
		Alpha
	};

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
			EmissionFromAlbedoAlpha = 1u,
			AdditiveByColor = EmissionFromAlbedoAlpha<<1u
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
		Flags GetFlags() const;
		void SetUVHandler(TextureType type,const std::shared_ptr<UVHandler> &uvHandler);
		const std::shared_ptr<UVHandler> &GetUVHandler(TextureType type) const;

		void SetAlphaMode(AlphaMode alphaMode,float alphaCutoff=0.5f);
		AlphaMode GetAlphaMode() const;
		float GetAlphaCutoff() const;

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
		AlphaMode m_alphaMode = AlphaMode::Opaque;
		float m_alphaCutoff = 0.5f;
		Scene &m_scene;
		std::array<std::shared_ptr<UVHandler>,umath::to_integral(TextureType::Count)> m_uvHandlers = {};
	};

	class ShaderModuleSpriteSheet;
	enum class SpriteSheetFrame : uint8_t
	{
		First = 0,
		Second
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
		CameraDataNode AddCameraDataNode();
		NormalMapNode AddNormalMapNode();
		LightPathNode AddLightPathNode();
		MixClosureNode AddMixClosureNode();
		MixNode AddMixNode(const Socket &socketColor1,const Socket &socketColor2,MixNode::Type type=MixNode::Type::Mix,const std::optional<const NumberSocket> &fac={});
		MixNode AddMixNode(MixNode::Type type=MixNode::Type::Mix);
		BackgroundNode AddBackgroundNode();
		TextureCoordinateNode AddTextureCoordinateNode();
		MappingNode AddMappingNode();
		ColorNode AddColorNode();
		AttributeNode AddAttributeNode(ccl::AttributeStandard attrType);
		EmissionNode AddEmissionNode();
		NumberSocket AddVertexAlphaNode();
		NumberSocket AddWrinkleFactorNode();

		PrincipledBSDFNode AddPrincipledBSDFNode();
		ToonBSDFNode AddToonBSDFNode();
		GlassBSDFNode AddGlassBSDFNode();
		TransparentBsdfNode AddTransparentBSDFNode();
		DiffuseBsdfNode AddDiffuseBSDFNode();
		MixClosureNode AddTransparencyClosure(const Socket &colorSocket,const NumberSocket &alphaSocket,AlphaMode alphaMode,float alphaCutoff=0.5f);
		NumberSocket ApplyAlphaMode(const NumberSocket &alphaSocket,AlphaMode alphaMode,float alphaCutoff=0.5f);

		Shader &GetShader() const;
		std::optional<Socket> GetUVSocket(Shader::TextureType type,ShaderModuleSpriteSheet *shaderModSpriteSheet=nullptr,SpriteSheetFrame frame=SpriteSheetFrame::First);

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

	class ShaderModuleAlbedo;
	class ShaderAlbedoSet
	{
	public:
		virtual ~ShaderAlbedoSet()=default;

		void SetAlbedoMap(const std::string &albedoMap);
		const std::optional<std::string> &GetAlbedoMap() const;

		void SetColorFactor(const Vector4 &colorFactor);
		const Vector4 &GetColorFactor() const;

		const std::optional<ImageTextureNode> &GetAlbedoNode() const;

		std::optional<ImageTextureNode> AddAlbedoMap(ShaderModuleAlbedo &albedoModule,CCLShader &shader);
	private:
		std::optional<std::string> m_albedoMap {};
		std::optional<ImageTextureNode> m_albedoNode {};
		Vector4 m_colorFactor = {1.f,1.f,1.f,1.f};
	};

	class ShaderModuleSpriteSheet
	{
	public:
		struct SpriteSheetData
		{
			std::pair<Vector2,Vector2> uv0;
			std::string albedoMap2 {};
			std::pair<Vector2,Vector2> uv1;
			float interpFactor = 0.f;
		};
		void SetSpriteSheetData(
			const Vector2 &uv0Min,const Vector2 &uv0Max,
			const std::string &albedoMap2,const Vector2 &uv1Min,const Vector2 &uv1Max,
			float interpFactor
		);
		void SetSpriteSheetData(const SpriteSheetData &spriteSheetData);
		const std::optional<SpriteSheetData> &GetSpriteSheetData() const;
	private:
		std::optional<SpriteSheetData> m_spriteSheetData {};
	};

	class ShaderModuleAlbedo
	{
	public:
		virtual ~ShaderModuleAlbedo()=default;

		void SetEmissionFromAlbedoAlpha(Shader &shader,bool b);

		const ShaderAlbedoSet &GetAlbedoSet() const;
		ShaderAlbedoSet &GetAlbedoSet();

		const ShaderAlbedoSet &GetAlbedoSet2() const;
		ShaderAlbedoSet &GetAlbedoSet2();

		void SetUseVertexAlphasForBlending(bool useAlphasForBlending);
		bool ShouldUseVertexAlphasForBlending() const;

		void SetWrinkleStretchMap(const std::string &wrinkleStretchMap);
		void SetWrinkleCompressMap(const std::string &wrinkleCompressMap);

		void LinkAlbedo(const Socket &color,const NumberSocket &alpha,bool useAlphaIfFlagSet=true);
		void LinkAlbedoToBSDF(const Socket &bsdf);
	protected:
		virtual void InitializeAlbedoColor(Socket &inOutColor);
		virtual void InitializeAlbedoAlpha(const Socket &inAlbedoColor,NumberSocket &inOutAlpha);
	private:
		bool SetupAlbedoNodes(CCLShader &shader,Socket &outColor,NumberSocket &outAlpha);
		ShaderAlbedoSet m_albedoSet = {};
		ShaderAlbedoSet m_albedoSet2 = {};
		std::optional<std::string> m_wrinkleStretchMap;
		std::optional<std::string> m_wrinkleCompressMap;
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
		void SetMetalnessMap(const std::string &metalnessMap,Channel channel=Channel::Blue);
		void SetMetalnessFactor(float metalnessFactor);

		std::optional<NumberSocket> AddMetalnessMap(CCLShader &shader);
		void LinkMetalness(const NumberSocket &metalness);
	private:
		std::optional<std::string> m_metalnessMap;
		Channel m_metalnessChannel = Channel::Blue;
		std::optional<NumberSocket> m_metalnessSocket = {};
		std::optional<float> m_metalnessFactor = {};
	};

	class ShaderModuleRoughness
	{
	public:
		virtual ~ShaderModuleRoughness()=default;
		void SetRoughnessMap(const std::string &roughnessMap,Channel channel=Channel::Green);
		void SetSpecularMap(const std::string &specularMap,Channel channel=Channel::Green);

		void SetRoughnessFactor(float roughness);

		std::optional<NumberSocket> AddRoughnessMap(CCLShader &shader);
		void LinkRoughness(const NumberSocket &roughness);
	private:
		std::optional<std::string> m_roughnessMap;
		std::optional<std::string> m_specularMap;
		Channel m_roughnessChannel = Channel::Green;

		std::optional<NumberSocket> m_roughnessSocket = {};
		std::optional<float> m_roughnessFactor = {};
	};

	class ShaderModuleEmission
	{
	public:
		virtual ~ShaderModuleEmission()=default;
		void SetEmissionMap(const std::string &emissionMap);
		void SetEmissionFactor(const Vector3 &factor);
		const Vector3 &GetEmissionFactor() const;
		void SetEmissionIntensity(float intensity);
		float GetEmissionIntensity() const;
		const std::optional<std::string> &GetEmissionMap() const;

		std::optional<Socket> AddEmissionMap(CCLShader &shader);
		void LinkEmission(const Socket &emission);
	protected:
		virtual void InitializeEmissionColor(Socket &inOutColor);
	private:
		std::optional<std::string> m_emissionMap;
		Vector3 m_emissionFactor = {0.f,0.f,0.f};
		float m_emissionIntensity = 1.f;

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
		public ShaderModuleAlbedo,
		public ShaderModuleSpriteSheet
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
		public ShaderModuleNormal,
		public ShaderModuleSpriteSheet
	{
	protected:
		virtual bool InitializeCCLShader(CCLShader &cclShader) override;
		using Shader::Shader;
	};

	class ShaderDepth
		: public Shader,
		public ShaderModuleAlbedo,
		public ShaderModuleSpriteSheet
	{
	public:
		void SetFarZ(float farZ);
	protected:
		virtual bool InitializeCCLShader(CCLShader &cclShader) override;
		using Shader::Shader;
	private:
		float m_farZ = 1.f;
	};

	class ShaderToon
		: public Shader,
		public ShaderModuleNormal,
		public ShaderModuleSpriteSheet
	{
	protected:
		virtual bool InitializeCCLShader(CCLShader &cclShader) override;
		using Shader::Shader;
	};

	class ShaderGlass
		: public Shader,
		public ShaderModuleNormal,
		public ShaderModuleRoughness,
		public ShaderModuleSpriteSheet
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
		public ShaderModuleEmission,
		public ShaderModuleSpriteSheet
	{
	public:
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
		enum class RenderFlags : uint32_t
		{
			None = 0u,
			AdditiveByColor = 1u
		};

		using ShaderPBR::ShaderPBR;
		void SetRenderFlags(RenderFlags flags);
		void SetColor(const Color &color);
		const Color &GetColor() const;
	protected:
		virtual bool InitializeCCLShader(CCLShader &cclShader) override;
		virtual util::EventReply InitializeTransparency(CCLShader &cclShader,ImageTextureNode &albedoNode,const NumberSocket &alphaSocket) const override;
		virtual void InitializeAlbedoColor(Socket &inOutColor) override;
		virtual void InitializeAlbedoAlpha(const Socket &inAlbedoColor,NumberSocket &inOutAlpha) override;
		virtual void InitializeEmissionColor(Socket &inOutColor) override;
	private:
		RenderFlags m_renderFlags = RenderFlags::None;
		Color m_color = Color::White;
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
REGISTER_BASIC_BITWISE_OPERATORS(pragma::modules::cycles::ShaderParticle::RenderFlags)

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
