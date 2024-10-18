/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

#ifndef __PR_CYCLES_HPP__
#define __PR_CYCLES_HPP__

#ifdef _WIN32
#define PRAGMA_EXPORT __declspec(dllexport)
#define PRAGMA_IMPORT __declspec(dllimport)
#else
#define PRAGMA_EXPORT __attribute__((visibility("default")))
#define PRAGMA_IMPORT
#endif

// #define ENABLE_MOTION_BLUR_TEST

#define ENABLE_TEST_AMBIENT_OCCLUSION
namespace prosper {
	class Texture;
	class IImage;
	class IPrimaryCommandBuffer;
	class IFence;
};
class BaseEntity;

namespace pragma {
	class CAnimatedComponent;
	class CLightMapComponent;
	class CParticleSystemComponent;
	class CSkyCameraComponent;
	class CModelComponent;
	class CSceneComponent;
	struct LightmapDataCache;
};
namespace umath {
	class Transform;
	class ScaledTransform;
};
namespace unirender {
	class Scene;
};
namespace util::bsp {
	struct LightMapInfo;
};
namespace uimg {
	class ImageBuffer;
};
class Model;
class ModelMesh;
class ModelSubMesh;
class Material;
class CParticle;
class DataStream;
namespace util {
	struct HairStrandData;
};

#endif
