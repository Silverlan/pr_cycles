/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

module;

#ifdef RT_ENABLE_SUBDIVISION
#include <iostream>
#include <mathutil/vertex.hpp>
#include <mathutil/uvec.h>
#include <vector>
#include <functional>
#include <memory>
#include <opensubdiv/far/topologyDescriptor.h>
#include <opensubdiv/far/primvarRefiner.h>

export module pragma.modules.scenekit:subdivision;

export namespace pragma::modules::scenekit {
	using FaceVertexIndex = uint32_t;
	struct BaseChannelData {
		BaseChannelData(const std::function<void(BaseChannelData &, FaceVertexIndex, umath::Vertex &, int)> &fApply, const std::function<void(uint32_t)> &prepareResultData = nullptr) : m_apply {fApply}, m_prepareResultData {prepareResultData} {}
		virtual void ResizeBuffer(size_t size) = 0;
		virtual void ReserveBuffer(size_t size) = 0;
		virtual void *GetDataPtr() = 0;
		void *GetElementPtr(uint32_t idx) { return static_cast<uint8_t *>(GetDataPtr()) + (idx * GetElementSize()); }
		virtual uint32_t GetElementSize() const = 0;
		virtual void Interpolate(OpenSubdiv::Far::PrimvarRefiner &primvarRefiner, int32_t level, void *src, void *dst, int channel) = 0;
		void Apply(FaceVertexIndex face, umath::Vertex &v, int idx)
		{
			if(m_apply)
				m_apply(*this, face, v, idx);
		}
		void PrepareResultData(uint32_t numFaces)
		{
			if(m_prepareResultData)
				m_prepareResultData(numFaces);
		}
	  private:
		std::function<void(BaseChannelData &, FaceVertexIndex, umath::Vertex &, int)> m_apply = nullptr;
		std::function<void(uint32_t)> m_prepareResultData = nullptr;
	};

	template<typename T>
	struct OsdGenericAttribute {
		OsdGenericAttribute(const T &v = {}) : value {v} {}

		OsdGenericAttribute(OsdGenericAttribute<T> const &src) : value {src.value} {}

		void Clear() { value = {}; }

		void AddWithWeight(OsdGenericAttribute<T> const &src, float weight) { value += weight * src.value; }
		T value;
	};

	template<class TOsdType>
	struct ChannelData : public BaseChannelData {
		ChannelData(const std::function<void(BaseChannelData &, FaceVertexIndex, umath::Vertex &, int)> &fApply, const std::function<void(uint32_t)> &prepareResultData = nullptr) : BaseChannelData {fApply, prepareResultData} {}
		virtual void ResizeBuffer(size_t size) override { buffer.resize(size); }
		virtual void ReserveBuffer(size_t size) override { buffer.reserve(size); }
		virtual void *GetDataPtr() override { return buffer.data(); }
		virtual uint32_t GetElementSize() const { return sizeof(TOsdType); }
		virtual void Interpolate(OpenSubdiv::Far::PrimvarRefiner &primvarRefiner, int32_t level, void *psrc, void *pdst, int channel) override
		{
			auto *src = static_cast<TOsdType *>(psrc);
			auto *dst = static_cast<TOsdType *>(pdst);
			if(channel == 0)
				primvarRefiner.Interpolate(level, src, dst);
			else
				primvarRefiner.InterpolateFaceVarying(level, src, dst, channel - 1);
		}
		std::vector<TOsdType> buffer {};
	};
	using OsdVertex = OsdGenericAttribute<Vector3>;
	using OsdUV = OsdGenericAttribute<Vector2>;
	using OsdFloatAttr = OsdGenericAttribute<float>;
	void subdivide_mesh(const std::vector<umath::Vertex> &verts, const std::vector<int32_t> &tris, std::vector<umath::Vertex> &outVerts, std::vector<int32_t> &outTris, uint32_t subDivLevel, const std::vector<std::shared_ptr<BaseChannelData>> &miscAttributes = {});
};
#endif
