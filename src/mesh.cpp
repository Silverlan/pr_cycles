#include "pr_cycles/mesh.hpp"
#include "pr_cycles/scene.hpp"
#include "pr_cycles/shader.hpp"
#include <render/mesh.h>
#include <render/scene.h>

using namespace pragma::modules;

#pragma optimize("",off)
static const std::string TANGENT_POSTFIX = ".tangent";
static const std::string TANGENT_SIGN_POSTIFX = ".tangent_sign";
cycles::PMesh cycles::Mesh::Create(Scene &scene,const std::string &name,uint64_t numVerts,uint64_t numTris)
{
	auto *mesh = new ccl::Mesh{}; // Object will be removed automatically by cycles
	mesh->name = name;
	auto *attrN = mesh->attributes.add(ccl::ATTR_STD_VERTEX_NORMAL);
	if(attrN)
		attrN->resize(numVerts);

	auto *attrUV = mesh->attributes.add(ccl::ATTR_STD_UV);
	if(attrUV)
		attrUV->resize(numTris *3);

	auto *attrT = mesh->attributes.add(ccl::ATTR_STD_UV_TANGENT);
	if(attrT)
	{
		attrT->resize(numTris *3);
		attrT->name = name +TANGENT_POSTFIX;
	}

	auto *attrTS = mesh->attributes.add(ccl::ATTR_STD_UV_TANGENT_SIGN);
	if(attrTS)
	{
		attrTS->resize(numTris *3);
		attrTS->name = name +TANGENT_SIGN_POSTIFX;
	}

	mesh->reserve_mesh(numVerts,numTris);
	scene->meshes.push_back(mesh);
	auto meshWrapper = PMesh{new Mesh{scene,*mesh,numVerts,numTris}};
	meshWrapper->m_perVertexUvs.reserve(numVerts);
	meshWrapper->m_perVertexTangents.reserve(numVerts);
	meshWrapper->m_perVertexTangentSigns.reserve(numVerts);
	return meshWrapper;
}

cycles::Mesh::Mesh(Scene &scene,ccl::Mesh &mesh,uint64_t numVerts,uint64_t numTris)
	: SceneObject{scene},m_mesh{mesh},m_numVerts{numVerts},
	m_numTris{numTris}
{
	auto *normals = m_mesh.attributes.find(ccl::ATTR_STD_VERTEX_NORMAL);
	m_normals = normals ? normals->data_float4() : nullptr;

	auto *uvs = m_mesh.attributes.find(ccl::ATTR_STD_UV);
	m_uvs = uvs ? uvs->data_float2() : nullptr;

	auto *tangents = m_mesh.attributes.find(ccl::ATTR_STD_UV_TANGENT);
	m_tangents = tangents ? tangents->data_float4() : nullptr;

	auto *tangentSigns = m_mesh.attributes.find(ccl::ATTR_STD_UV_TANGENT_SIGN);
	m_tangentSigns = tangentSigns ? tangentSigns->data_float() : nullptr;
}

util::WeakHandle<cycles::Mesh> cycles::Mesh::GetHandle()
{
	return util::WeakHandle<cycles::Mesh>{shared_from_this()};
}

const ccl::float4 *cycles::Mesh::GetNormals() const {return m_normals;}
const ccl::float4 *cycles::Mesh::GetTangents() const {return m_tangents;}
const float *cycles::Mesh::GetTangentSigns() const {return m_tangentSigns;}
const ccl::float2 *cycles::Mesh::GetUVs() const {return m_uvs;}
uint64_t cycles::Mesh::GetVertexCount() const {return m_numVerts;}
uint64_t cycles::Mesh::GetTriangleCount() const {return m_numTris;}

static ccl::float4 to_float4(const ccl::float3 &v)
{
	return ccl::float4{v.x,v.y,v.z,0.f};
}

bool cycles::Mesh::AddVertex(const Vector3 &pos,const Vector3 &n,const Vector3 &t,const Vector2 &uv)
{
	auto idx = m_mesh.verts.size();
	if(idx >= m_numVerts)
		return false;
	if(m_normals)
		m_normals[idx] = to_float4(Scene::ToCyclesNormal(n));
	m_mesh.add_vertex(Scene::ToCyclesPosition(pos));
	m_perVertexUvs.push_back(uv);
	m_perVertexTangents.push_back(t);
	return true;
}

bool cycles::Mesh::AddTriangle(uint32_t idx0,uint32_t idx1,uint32_t idx2,uint32_t shaderIndex)
{
	// Winding order has to be inverted for cycles
#ifndef ENABLE_TEST_AMBIENT_OCCLUSION
	umath::swap(idx1,idx2);
#endif

	auto numCurMeshTriIndices = m_mesh.triangles.size();
	auto idx = numCurMeshTriIndices /3;
	if(idx >= m_numTris)
		return false;
	constexpr auto smooth = true;
	m_mesh.add_triangle(idx0,idx1,idx2,shaderIndex,smooth);

	if(m_uvs == nullptr)
		return false;
	if(idx0 >= m_perVertexUvs.size() || idx1 >= m_perVertexUvs.size() || idx2 >= m_perVertexUvs.size())
		return false;
	auto &uv0 = m_perVertexUvs.at(idx0);
	auto &uv1 = m_perVertexUvs.at(idx1);
	auto &uv2 = m_perVertexUvs.at(idx2);
	auto offset = numCurMeshTriIndices;
	m_uvs[offset] = Scene::ToCyclesUV(uv0);
	m_uvs[offset +1] = Scene::ToCyclesUV(uv1);
	m_uvs[offset +2] = Scene::ToCyclesUV(uv2);

	auto &t0 = m_perVertexTangents.at(idx0);
	auto &t1 = m_perVertexTangents.at(idx1);
	auto &t2 = m_perVertexTangents.at(idx2);
	m_tangents[offset] = to_float4(Scene::ToCyclesNormal(t0));
	m_tangents[offset +1] = to_float4(Scene::ToCyclesNormal(t1));
	m_tangents[offset +2] = to_float4(Scene::ToCyclesNormal(t2));

	m_tangentSigns[offset] = m_tangentSigns[offset +1] = m_tangentSigns[offset +2] = 1.f;
	return true;
}

uint32_t cycles::Mesh::AddShader(Shader &shader)
{
	m_mesh.used_shaders.push_back(*shader);
	return m_mesh.used_shaders.size() -1;
}

ccl::Mesh *cycles::Mesh::operator->() {return &m_mesh;}
ccl::Mesh *cycles::Mesh::operator*() {return &m_mesh;}
#pragma optimize("",on)
