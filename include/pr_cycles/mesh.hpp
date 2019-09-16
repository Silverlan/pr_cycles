#ifndef __PR_CYCLES_MESH_HPP__
#define __PR_CYCLES_MESH_HPP__

#include "scene_object.hpp"
#include <memory>
#include <mathutil/uvec.h>

namespace ccl {class Mesh; class Attribute; struct float4; struct float3; struct float2;};
namespace pragma::modules::cycles
{
	class Shader;
	class Scene;
	class Mesh;
	using PMesh = std::shared_ptr<Mesh>;
	class Mesh
		: public SceneObject,
		public std::enable_shared_from_this<Mesh>
	{
	public:
		static PMesh Create(Scene &scene,const std::string &name,uint64_t numVerts,uint64_t numTris);
		util::WeakHandle<Mesh> GetHandle();

		const ccl::float4 *GetNormals() const;
		const ccl::float4 *GetTangents() const;
		const float *GetTangentSigns() const;
		const ccl::float2 *GetUVs() const;
		const ccl::float2 *GetLightmapUVs() const;
		void SetLightmapUVs(std::vector<ccl::float2> &&lightmapUvs);
		uint64_t GetVertexCount() const;
		uint64_t GetTriangleCount() const;

		bool AddVertex(const Vector3 &pos,const Vector3 &n,const Vector3 &t,const Vector2 &uv);
		bool AddTriangle(uint32_t idx0,uint32_t idx1,uint32_t idx2,uint32_t shaderIndex);
		uint32_t AddShader(Shader &shader);
		ccl::Mesh *operator->();
		ccl::Mesh *operator*();
	private:
		Mesh(Scene &scene,ccl::Mesh &mesh,uint64_t numVerts,uint64_t numTris);
		std::vector<Vector2> m_perVertexUvs = {};
		std::vector<Vector3> m_perVertexTangents = {};
		std::vector<float> m_perVertexTangentSigns = {};
		ccl::Mesh &m_mesh;
		ccl::float4 *m_normals = nullptr;
		ccl::float4 *m_tangents = nullptr;
		float *m_tangentSigns = nullptr;
		ccl::float2 *m_uvs = nullptr;
		std::vector<ccl::float2> m_lightmapUvs = {};
		uint64_t m_numVerts = 0ull;
		uint64_t m_numTris = 0ull;
	};
};

#endif
