#ifndef __PR_CYCLES_MESH_HPP__
#define __PR_CYCLES_MESH_HPP__

#include "scene_object.hpp"
#include "nodes.hpp"
#include <memory>
#include <optional>
#include <mathutil/uvec.h>
#include <kernel/kernel_types.h>

namespace ccl {class Mesh; class Attribute; struct float4; struct float3; struct float2;};
namespace pragma::modules::cycles
{
	class Shader;
	class CCLShader;
	class Scene;
	class Mesh;
	using PMesh = std::shared_ptr<Mesh>;
	using PShader = std::shared_ptr<Shader>;
	class Mesh
		: public SceneObject,
		public std::enable_shared_from_this<Mesh>
	{
	public:
		enum class Flags : uint8_t
		{
			None = 0u,
			HasAlphas = 1u,
			HasWrinkles = HasAlphas<<1u
		};
		static constexpr ccl::AttributeStandard ALPHA_ATTRIBUTE_TYPE = ccl::AttributeStandard::ATTR_STD_POINTINESS;

		static PMesh Create(Scene &scene,const std::string &name,uint64_t numVerts,uint64_t numTris,Flags flags=Flags::None);
		util::WeakHandle<Mesh> GetHandle();

		const ccl::float4 *GetNormals() const;
		const ccl::float4 *GetTangents() const;
		const float *GetTangentSigns() const;
		const float *GetAlphas() const;
		const float *GetWrinkleFactors() const;
		const ccl::float2 *GetUVs() const;
		const ccl::float2 *GetLightmapUVs() const;
		const std::vector<PShader> &GetSubMeshShaders() const;
		std::vector<PShader> &GetSubMeshShaders();
		void SetLightmapUVs(std::vector<ccl::float2> &&lightmapUvs);
		uint64_t GetVertexCount() const;
		uint64_t GetTriangleCount() const;
		uint32_t GetVertexOffset() const;
		std::string GetName() const;
		bool HasAlphas() const;
		bool HasWrinkles() const;

		bool AddVertex(const Vector3 &pos,const Vector3 &n,const Vector3 &t,const Vector2 &uv);
		bool AddAlpha(float alpha);
		bool AddWrinkleFactor(float wrinkle);
		bool AddTriangle(uint32_t idx0,uint32_t idx1,uint32_t idx2,uint32_t shaderIndex);
		uint32_t AddSubMeshShader(Shader &shader);
		ccl::Mesh *operator->();
		ccl::Mesh *operator*();
	private:
		Mesh(Scene &scene,ccl::Mesh &mesh,uint64_t numVerts,uint64_t numTris,Flags flags=Flags::None);
		virtual void DoFinalize() override;
		std::vector<Vector2> m_perVertexUvs = {};
		std::vector<Vector3> m_perVertexTangents = {};
		std::vector<float> m_perVertexTangentSigns = {};
		std::vector<float> m_perVertexAlphas = {};
		std::vector<PShader> m_subMeshShaders = {};
		ccl::Mesh &m_mesh;
		ccl::float4 *m_normals = nullptr;
		ccl::float4 *m_tangents = nullptr;
		float *m_tangentSigns = nullptr;
		ccl::float2 *m_uvs = nullptr;
		float *m_alphas = nullptr;
		std::vector<ccl::float2> m_lightmapUvs = {};
		uint64_t m_numVerts = 0ull;
		uint64_t m_numTris = 0ull;
		Flags m_flags = Flags::None;
	};
};
REGISTER_BASIC_BITWISE_OPERATORS(pragma::modules::cycles::Mesh::Flags)

#endif
