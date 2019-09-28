/*
* Copyright 2011-2013 Blender Foundation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#ifndef __PR_CYCLES_UTIL_BAKING_HPP__
#define __PR_CYCLES_UTIL_BAKING_HPP__

#include "render/bake.h"
#include <sharedutils/util_image_buffer.hpp>
#include <cinttypes>
#include <vector>

namespace pragma::modules::cycles
{
	class Object;
	namespace baking
	{
		// Note: These are various utility functions from the blender repository, which are required
		// for baking with cycles.
		struct BakePixel {
			int primitive_id, object_id;
			float uv[2];
			float du_dx, du_dy;
			float dv_dx, dv_dy;
		};

		typedef struct ImBuf {
			int x, y;
			std::shared_ptr<util::ImageBuffer> rect;
		} ImBuf;

		void prepare_bake_data(cycles::Object &o,BakePixel *pixelArray,uint32_t numPixels,uint32_t imgWidth,uint32_t imgHeight,bool useLightmapUvs=false);
		void populate_bake_data(ccl::BakeData *data,
			const int object_id,
			BakePixel *pixel_array,
			const int num_pixels);
		unsigned char unit_float_to_uchar_clamp(float val);
		void RE_bake_mask_fill(const std::vector<BakePixel> pixel_array, const size_t num_pixels, char *mask);
		void RE_bake_margin(ImBuf *ibuf, std::vector<uint8_t> &mask, const int margin);
	};
}

#endif
