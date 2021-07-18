/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#ifdef RT_ENABLE_SUBDIVISION
#include "pr_cycles/subdivision.hpp"
#include <mathutil/umath.h>

#pragma comment(lib,"E:/projects/OpenSubdiv/build_winx64/lib/RelWithDebInfo/osdCPU.lib")

struct OsdVertexWeight
{
	OsdVertexWeight()
	{
		Clear();
	}

	OsdVertexWeight(OsdVertexWeight const & src)
		: vw{src.vw}
	{}

	void Clear()
	{
		vw = {};
	}

	void AddWithWeight(OsdVertexWeight const & src, float weight)
	{
		// ??
	}
	umath::VertexWeight vw {};
};

void pragma::modules::cycles::subdivide_mesh(
	const std::vector<umath::Vertex> &verts,const std::vector<int32_t> &tris,std::vector<umath::Vertex> &outVerts,std::vector<int32_t> &outTris,uint32_t subDivLevel,
	const std::vector<std::shared_ptr<BaseChannelData>> &miscAttributes
)
{
	std::vector<std::shared_ptr<BaseChannelData>> vertexAttributes {};
	vertexAttributes.reserve(miscAttributes.size() +3);
	auto vertexData = std::make_shared<ChannelData<OsdVertex>>([](BaseChannelData &cd,FaceVertexIndex faceVertexIndex,umath::Vertex &v,int idx) {v.position = static_cast<OsdVertex*>(cd.GetElementPtr(idx))->value;});
	vertexAttributes.push_back(vertexData);

	auto uvData = std::make_shared<ChannelData<OsdUV>>([](BaseChannelData &cd,FaceVertexIndex faceVertexIndex,umath::Vertex &v,int idx) {v.uv = static_cast<OsdUV*>(cd.GetElementPtr(idx))->value;});
	vertexAttributes.push_back(uvData);
	uvData->ReserveBuffer(verts.size());

	auto normData = std::make_shared<ChannelData<OsdVertex>>([](BaseChannelData &cd,FaceVertexIndex faceVertexIndex,umath::Vertex &v,int idx) {v.normal = static_cast<OsdVertex*>(cd.GetElementPtr(idx))->value;});
	vertexAttributes.push_back(normData);
	normData->ReserveBuffer(verts.size());

	for(auto &attr : miscAttributes)
		vertexAttributes.push_back(attr);

	for(auto i=decltype(verts.size()){0u};i<verts.size();++i)
	{
		auto &v = verts.at(i);
		uvData->buffer.push_back({v.uv});
		normData->buffer.push_back({v.normal});
	}

	std::vector<int32_t> originalVertexIndexToUniqueIndex;
	originalVertexIndexToUniqueIndex.resize(verts.size());
	vertexData->ReserveBuffer(verts.size());
	constexpr float VERTEX_EPSILON = 0.02f;
	for(auto i=decltype(verts.size()){0u};i<verts.size();++i)
	{
		auto &v0 = verts.at(i);

		auto wasInserted = false;
		for(auto j=decltype(verts.size()){0u};j<i;++j)
		{
			auto &v1 = verts.at(j);
			auto d = uvec::distance_sqr(v0.position,v1.position);
			if(d < VERTEX_EPSILON)
			{
				wasInserted = true;
				originalVertexIndexToUniqueIndex.at(i) = originalVertexIndexToUniqueIndex.at(j);
				break;
			}
		}
		if(wasInserted)
			continue;
		vertexData->buffer.push_back({});
		vertexData->buffer.back().value = v0.position;
		originalVertexIndexToUniqueIndex.at(i) = vertexData->buffer.size() -1;
	}

	std::cout<<"Reduced mesh vertex count from "<<verts.size()<<" to "<<vertexData->buffer.size()<<std::endl;

	auto triIndicesForUniqueVerts = tris;
	for(auto &idx : triIndicesForUniqueVerts)
		idx = originalVertexIndexToUniqueIndex.at(idx);

	auto type = OpenSubdiv::Sdc::SCHEME_LOOP; // Loop is better suited for triangulated meshes (http://graphics.pixar.com/opensubdiv/docs/subdivision_surfaces.html#schemes-and-options)

	OpenSubdiv::Sdc::Options options;
	options.SetVtxBoundaryInterpolation(OpenSubdiv::Sdc::Options::VTX_BOUNDARY_EDGE_ONLY);
	options.SetFVarLinearInterpolation(OpenSubdiv::Sdc::Options::FVAR_LINEAR_NONE);
	options.SetTriangleSubdivision(OpenSubdiv::Sdc::Options::TriangleSubdivision::TRI_SUB_SMOOTH);

	constexpr uint32_t numVertsPerFace = 3;
	auto numFaces = triIndicesForUniqueVerts.size() /numVertsPerFace;

	std::vector<int> iVertsPerFace {};
	iVertsPerFace.resize(numFaces,numVertsPerFace);

	OpenSubdiv::Far::TopologyDescriptor desc;
	desc.numVertices = verts.size();
	desc.numFaces = numFaces;
	desc.numVertsPerFace = iVertsPerFace.data();
	desc.vertIndicesPerFace = triIndicesForUniqueVerts.data();

	OpenSubdiv::Far::TopologyDescriptor::FVarChannel channelInfo {};
	channelInfo.numValues = verts.size();
	channelInfo.valueIndices = tris.data();
	auto numChannels = vertexAttributes.size() -1;
	std::vector<OpenSubdiv::Far::TopologyDescriptor::FVarChannel> channels {};
	channels.resize(numChannels,channelInfo);

	desc.numFVarChannels = channels.size();
	desc.fvarChannels = channels.data();

	auto *refiner = OpenSubdiv::Far::TopologyRefinerFactory<OpenSubdiv::Far::TopologyDescriptor>::Create(desc,OpenSubdiv::Far::TopologyRefinerFactory<OpenSubdiv::Far::TopologyDescriptor>::Options(type,options));

	OpenSubdiv::Far::TopologyRefiner::UniformOptions refineOptions(subDivLevel);
	refineOptions.fullTopologyInLastLevel = true;
	refiner->RefineUniform(refineOptions);

	for(auto i=decltype(vertexAttributes.size()){0u};i<vertexAttributes.size();++i)
		vertexAttributes.at(i)->ResizeBuffer((i == 0) ? refiner->GetNumVerticesTotal() : refiner->GetNumFVarValuesTotal(i -1));

	OpenSubdiv::Far::PrimvarRefiner primvarRefiner{*refiner};
	std::vector<uint8_t*> attrPtrs {};
	attrPtrs.reserve(vertexAttributes.size());
	for(auto &attr : vertexAttributes)
		attrPtrs.push_back(static_cast<uint8_t*>(attr->GetDataPtr()));
	for(auto level=decltype(subDivLevel){1};level<=subDivLevel;++level)
	{
		for(auto i=decltype(attrPtrs.size()){0u};i<attrPtrs.size();++i)
		{
			auto *src = attrPtrs.at(i);
			auto n = (i == 0) ? refiner->GetLevel(level -1).GetNumVertices() : refiner->GetLevel(level -1).GetNumFVarValues(i -1);
			auto *dst = src +n *vertexAttributes.at(i)->GetElementSize();
			vertexAttributes.at(i)->Interpolate(primvarRefiner,level,src,dst,i);
			attrPtrs.at(i) = dst;
		}
	}

	auto &refLastLevel = refiner->GetLevel(subDivLevel);
	std::vector<int> numResultAttrs {};
	std::vector<int> firstOfLastAttrs {};
	numResultAttrs.resize(vertexAttributes.size());
	firstOfLastAttrs.resize(vertexAttributes.size());
	for(auto i=decltype(vertexAttributes.size()){0u};i<vertexAttributes.size();++i)
	{
		numResultAttrs.at(i) = (i == 0) ? refLastLevel.GetNumVertices() : refLastLevel.GetNumFVarValues(i -1);
		firstOfLastAttrs.at(i) = (i == 0) ? (refiner->GetNumVerticesTotal() -numResultAttrs.at(i)) : (refiner->GetNumFVarValuesTotal(i -1) -numResultAttrs.at(i));
	}
	
	auto numResultFaces = refLastLevel.GetNumFaces();
	outVerts.reserve(numResultFaces *3);
	outTris.reserve(numResultFaces *3);
	for(auto &attr : vertexAttributes)
		attr->PrepareResultData(numResultFaces);
	for(auto face=decltype(numResultFaces){0u};face<numResultFaces;++face)
	{
		std::vector<OpenSubdiv::Far::ConstIndexArray> attrIndices;
		attrIndices.reserve(numResultAttrs.size());
		for(auto i=decltype(numResultAttrs.size()){0u};i<numResultAttrs.size();++i)
			attrIndices.push_back((i == 0) ? refLastLevel.GetFaceVertices(face) : refLastLevel.GetFaceFVarValues(face,i -1));

		for(uint8_t i=0;i<3;++i)
		{
			umath::Vertex v {};
			for(auto j=decltype(numResultAttrs.size()){0u};j<numResultAttrs.size();++j)
			{
				auto &attr = vertexAttributes.at(j);
				auto idx = firstOfLastAttrs.at(j) +attrIndices.at(j)[i];
				attr->Apply(face *3 +i,v,idx);
			}
			auto it = std::find_if(outVerts.begin(),outVerts.end(),[&v,VERTEX_EPSILON](const umath::Vertex &vOther) {
				return vOther.Equal(v,VERTEX_EPSILON);
			});
			if(it == outVerts.end())
			{
				outVerts.push_back(v);
				it = outVerts.end() -1;
			}
			outTris.push_back(it -outVerts.begin());
		}
	}

	std::cout<<"Reduced final vertex count from "<<refLastLevel.GetNumVertices()<<" to "<<outVerts.size()<<std::endl;

#if 0
	outVerts.reserve(numVerts);
	if(optOutVertWeights)
		optOutVertWeights->reserve(numVerts);
	for(auto vert=decltype(numVerts){0u};vert<numVerts;++vert)
	{
		auto &pos = vertPosBuffer.at(firstOfLastVerts +vert);
		auto &uv = uvBuffer.at(firstOfLastUvs +vert);
		auto &n = normalBuffer.at(firstOfLastNormals +vert);
		outVerts.push_back({});
		outVerts.back().position = pos.position;
		outVerts.back().uv = uv.uv;
		outVerts.back().normal = n.position;

		if(optOutVertWeights)
			optOutVertWeights->push_back(vertWeightBuffer.at(firstOfLastWeights +vert).vw);
	}

	//auto type = OpenSubdiv::Sdc::SCHEME_LOOP;
	switch(type)
	{
	case OpenSubdiv::Sdc::SCHEME_LOOP:
	{
		outTris.reserve(numResultFaces *3);
		for(auto face=decltype(numResultFaces){0u};face<numResultFaces;++face)
		{
			auto indices = refLastLevel.GetFaceVertices(face);
			auto fuvs = refLastLevel.GetFaceFVarValues(face,umath::to_integral(Channel::UV));
			auto fuvs = refLastLevel.GetFaceFVarValues(face,umath::to_integral(Channel::Normal));
			assert(indices.size() == 3);

			outTris.push_back(indices[0]);
			outTris.push_back(indices[1]);
			outTris.push_back(indices[2]);
		}
		break;
	}
	case OpenSubdiv::Sdc::SCHEME_CATMARK:
	{
		outTris.reserve(numResultFaces *2 *3);
		for(auto face=decltype(numResultFaces){0u};face<numResultFaces;++face)
		{
			auto indices = refLastLevel.GetFaceVertices(face);
			assert(indices.size() == 4);

			outTris.push_back(indices[0]);
			outTris.push_back(indices[1]);
			outTris.push_back(indices[2]);
		
			outTris.push_back(indices[0]);
			outTris.push_back(indices[2]);
			outTris.push_back(indices[3]);
		}
		break;
	}
	}
#endif
}

#endif
