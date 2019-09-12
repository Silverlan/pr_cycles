#include "pr_cycles/scene.hpp"
#include "pr_cycles/mesh.hpp"
#include "pr_cycles/camera.hpp"
#include "pr_cycles/shader.hpp"
#include "pr_cycles/object.hpp"
#include "pr_cycles/light.hpp"
#include <render/buffers.h>
#include <render/scene.h>
#include <render/session.h>
#include <render/shader.h>
#include <render/camera.h>
#include <render/light.h>
#include <render/mesh.h>
#include <render/graph.h>
#include <render/nodes.h>
#include <render/object.h>
#include <render/background.h>
#include <render/integrator.h>
#include <render/svm.h>
#include <render/bake.h>
#include <optional>
#include <pragma/c_engine.h>
#include <pragma/util/util_game.hpp>
#include <prosper_context.hpp>
#include <buffers/prosper_uniform_resizable_buffer.hpp>
#include <pragma/clientstate/clientstate.h>
#include <pragma/model/model.h>
#include <pragma/model/modelmesh.h>
#include <pragma/entities/baseentity.h>
#include <pragma/entities/entity_component_system_t.hpp>
#include <pragma/entities/components/c_animated_component.hpp>
#include <pragma/entities/components/c_vertex_animated_component.hpp>
#include <sharedutils/util_file.h>
#include <texturemanager/texture.h>
#include <cmaterialmanager.h>
#include <pr_dds.hpp>

// ccl happens to have the same include guard name as sharedutils, so we have to undef it here
#undef __UTIL_STRING_H__
#include <sharedutils/util_string.h>

using namespace pragma::modules;

#pragma optimize("",off)

extern DLLCENGINE CEngine *c_engine;
extern DLLCLIENT ClientState *client;
extern DLLCLIENT CGame *c_game;

cycles::PScene cycles::Scene::Create(const std::function<void(const uint8_t*,int,int,int)> &outputHandler,uint32_t sampleCount,bool hdrOutput,bool denoise)
{
	std::optional<ccl::DeviceInfo> device = {};
	for(auto &devInfo : ccl::Device::available_devices(ccl::DeviceTypeMask::DEVICE_MASK_CUDA | ccl::DeviceTypeMask::DEVICE_MASK_OPENCL | ccl::DeviceTypeMask::DEVICE_MASK_CPU))
	{
		switch(devInfo.type)
		{
		case ccl::DeviceType::DEVICE_CUDA:
		case ccl::DeviceType::DEVICE_OPENCL:
			// TODO: GPU devices currently don't seem to work, they just create a new instance of the program?
			//device = devInfo; // GPU device; We'll just use this one
			//goto endLoop;
			break;
		default:
			device = devInfo; // CPU device; Select it, but continue looking for a better device (i.e. GPU)
		}
	}
endLoop:

	if(device.has_value() == false)
		return nullptr; // No device available

	ccl::SessionParams sessionParams {};
	sessionParams.shadingsystem = ccl::SHADINGSYSTEM_SVM;
	sessionParams.device = *device;
	sessionParams.progressive = true; // TODO: This should be set to false, but doing so causes a crash during rendering
	sessionParams.background = true; // true; // TODO: Has to be set to false for GPU rendering
	if(hdrOutput)
		sessionParams.display_buffer_linear = true;
	if(denoise)
	{
		sessionParams.full_denoising = true;
		sessionParams.run_denoising = true;
	}
	sessionParams.start_resolution = 64;
	sessionParams.samples = 1225;//sampleCount;
	// We need the scene-pointer in the callback-function, however the function has to be
	// defined before the scene is created, so we create a shared pointer that will be initialized after
	// the scene.
	auto ptrScene = std::make_shared<Scene*>(nullptr);
	auto ptrCclSession = std::make_shared<ccl::Session*>(nullptr);
	if(hdrOutput == false)
	{
		sessionParams.write_render_cb = [ptrScene,ptrCclSession,outputHandler,denoise](const ccl::uchar *pixels,int w,int h,int channels) -> bool {
			if(channels != INPUT_CHANNEL_COUNT)
				return false;
			std::vector<std::array<uint8_t,OUTPUT_CHANNEL_COUNT>> flippedData {};
			flippedData.resize(w *h);

			for(auto x=decltype(h){0};x<h;++x)
			{
				for(auto y=decltype(w){0};y<w;++y)
				{
					auto offset = x *w +y;
					auto *pxData = pixels +offset *INPUT_CHANNEL_COUNT;

					// Out-offset flips the image on the y-axis
					auto outOffset = (h -x -1) *w +y;
					auto *outPixelData = reinterpret_cast<uint8_t*>(flippedData.data()) +outOffset *OUTPUT_CHANNEL_COUNT;
					for(uint8_t i=0;i<OUTPUT_CHANNEL_COUNT;++i)
					{
						if(i < (OUTPUT_CHANNEL_COUNT -1))
							outPixelData[i] = (i < INPUT_CHANNEL_COUNT) ? pxData[i] : 0;
						else
							outPixelData[i] = std::numeric_limits<uint8_t>::max();
					}
				}
			}

			auto *data = reinterpret_cast<uint8_t*>(flippedData.data());
			std::vector<uint8_t> denoisedData;
			if(denoise)
			{
				DenoiseInfo denoiseInfo {};
				denoiseInfo.hdr = false;
				denoiseInfo.width = w;
				denoiseInfo.height = h;
				(*ptrScene)->Denoise(denoiseInfo,flippedData.data(),denoisedData);
				data = denoisedData.data();
			}

			outputHandler(data,w,h,channels);
			return true;
		};
	}
	else
	{
		// We need to define a write callback, otherwise the session's display object will not be initialized.
		sessionParams.write_render_cb = [ptrScene,ptrCclSession,outputHandler,denoise](const ccl::uchar *pixels,int w,int h,int channels) -> bool {
			// We need to access the 'tonemap' method of ccl::Session, but since it's a protected method, we have to use a wrapper to
			// gain access to it
			class SessionWrapper
				: ccl::Session
			{
			public:
				using ccl::Session::tonemap;
			};

			// This is a work-around, since cycles does not appear to provide any way of retrieving HDR-colors of the rendered image directly.
			delete (*ptrCclSession)->display;

			(*ptrCclSession)->display = new ccl::DisplayBuffer((*ptrCclSession)->device, true);
			(*ptrCclSession)->display->reset((*ptrCclSession)->buffers->params);
			reinterpret_cast<SessionWrapper*>(*ptrCclSession)->tonemap((*ptrCclSession)->params.samples);

			w = (*ptrCclSession)->display->draw_width;
			h = (*ptrCclSession)->display->draw_height;
			auto *hdrPixels = (*ptrCclSession)->display->rgba_half.copy_from_device(0,w,h);

			std::vector<std::array<uint16_t,OUTPUT_CHANNEL_COUNT>> hdrData {};
			hdrData.resize(w *h);

			for(auto x=decltype(h){0};x<h;++x)
			{
				for(auto y=decltype(w){0};y<w;++y)
				{
					auto offset = x *w +y;
					auto *pxData = reinterpret_cast<ccl::half*>(hdrPixels +offset);

					// Out-offset flips the image on the y-axis
					auto outOffset = (h -x -1) *w +y;
					auto *outPixelData = reinterpret_cast<uint16_t*>(hdrData.data()) +outOffset *OUTPUT_CHANNEL_COUNT;
					for(uint8_t i=0;i<OUTPUT_CHANNEL_COUNT;++i)
					{
						if(i < (OUTPUT_CHANNEL_COUNT -1))
							outPixelData[i] = (i < INPUT_CHANNEL_COUNT) ? pxData[i] : 0;
						else
							outPixelData[i] = ccl::float_to_half(std::numeric_limits<float>::max());
					}
				}
			}

			auto *data = reinterpret_cast<uint8_t*>(hdrData.data());
			std::vector<uint8_t> denoisedData;
			if(denoise)
			{
				DenoiseInfo denoiseInfo {};
				denoiseInfo.hdr = true;
				denoiseInfo.width = w;
				denoiseInfo.height = h;
				(*ptrScene)->Denoise(denoiseInfo,hdrData.data(),denoisedData);
				data = denoisedData.data();
			}

			outputHandler(data,w,h,OUTPUT_CHANNEL_COUNT);
			return true;
		};
	}
	auto session = std::make_unique<ccl::Session>(sessionParams);
	*ptrCclSession = session.get();

	ccl::SceneParams sceneParams {};
	sceneParams.shadingsystem = ccl::SHADINGSYSTEM_SVM;

	auto *cclScene = new ccl::Scene{sceneParams,session->device}; // Object will be removed automatically by cycles
	cclScene->params.bvh_type = ccl::SceneParams::BVH_STATIC;

	auto *pSession = session.get();
	auto scene = PScene{new Scene{std::move(session),*cclScene}};
	auto *pScene = scene.get();
	pSession->progress.set_update_callback([pScene]() {
		if(pScene->m_progressCallback)
			pScene->m_progressCallback(pScene->m_session->progress.get_progress());
	});
	scene->m_camera = Camera::Create(*scene);
	*ptrScene = pScene;
	return scene;
}

cycles::Scene::Scene(std::unique_ptr<ccl::Session> session,ccl::Scene &scene)
	: m_session{std::move(session)},m_scene{scene}
{}

cycles::Scene::~Scene()
{
	m_session->wait();
	m_objects.clear();
	m_shaders.clear();
	m_camera = nullptr;
	m_session = nullptr;
}

util::WeakHandle<cycles::Scene> cycles::Scene::GetHandle()
{
	return util::WeakHandle<cycles::Scene>{shared_from_this()};
}

cycles::Camera &cycles::Scene::GetCamera() {return *m_camera;}

static std::optional<std::string> get_abs_error_texture_path()
{
	std::string errTexPath = "materials\\error.dds";
	std::string absPath;
	if(FileManager::FindAbsolutePath(errTexPath,absPath) == false)
		return absPath;
	return {};
}

static std::optional<std::string> prepare_texture(TextureInfo *texInfo,bool &outSuccess,bool &outConverted)
{
	outSuccess = false;
	outConverted = false;
	if(texInfo == nullptr)
		return {};
	auto tex = texInfo ? std::static_pointer_cast<Texture>(texInfo->texture) : nullptr;

	// Make sure texture has been fully loaded!
	if(tex == nullptr || tex->IsLoaded() == false)
		return get_abs_error_texture_path();
	/*if(tex->IsLoaded() == false)
	{
		TextureManager::LoadInfo loadInfo {};
		loadInfo.flags = TextureLoadFlags::LoadInstantly;
		static_cast<CMaterialManager&>(client->GetMaterialManager()).GetTextureManager().Load(*c_engine,texInfo->name,loadInfo);
		if(tex->IsLoaded() == false)
			return get_abs_error_texture_path();
	}
	*/
	auto texPath = "materials\\" +texInfo->name;
	ufile::remove_extension_from_filename(texPath);
	texPath += ".dds";
	// Check if DDS version of the texture already exists, in which case we can just use it directly!
	std::string absPath;
	if(FileManager::FindAbsolutePath(texPath,absPath))
	{
		outSuccess = true;
		return absPath;
	}

	// Texture does not have the right format to begin with or does not exist on the local hard drive.
	// We will have to create the texture file in the right format (if the texture object is valid).
	if(tex == nullptr)
		return get_abs_error_texture_path(); // Texture is not valid! Return error texture.

	auto &img = *tex->texture->GetImage();

	// Output path for the DDS-file we're about to create
	auto ddsPath = "addons/converted/materials/" +tex->name;
	ufile::remove_extension_from_filename(ddsPath); // DDS-writer will add the extension for us
	ImageWriteInfo imgWriteInfo {};
	imgWriteInfo.containerFormat = ImageWriteInfo::ContainerFormat::DDS; // Cycles doesn't support KTX
	if(tex->HasFlag(Texture::Flags::SRGB))
		imgWriteInfo.flags |= ImageWriteInfo::Flags::SRGB;

	// Try to determine appropriate formats
	if(tex->HasFlag(Texture::Flags::NormalMap))
	{
		imgWriteInfo.flags |= ImageWriteInfo::Flags::NormalMap;
		imgWriteInfo.inputFormat = ImageWriteInfo::InputFormat::R32G32B32A32_Float;
		imgWriteInfo.outputFormat = ImageWriteInfo::OutputFormat::NormalMap;
	}
	else
	{
		auto format = img.GetFormat();
		if(prosper::util::is_16bit_format(format))
		{
			imgWriteInfo.inputFormat = ImageWriteInfo::InputFormat::R16G16B16A16_Float;
			imgWriteInfo.outputFormat = ImageWriteInfo::OutputFormat::HDRColorMap;
		}
		else if(prosper::util::is_32bit_format(format) || prosper::util::is_64bit_format(format))
		{
			imgWriteInfo.inputFormat = ImageWriteInfo::InputFormat::R32G32B32A32_Float;
			imgWriteInfo.outputFormat = ImageWriteInfo::OutputFormat::HDRColorMap;
		}
		else
		{
			imgWriteInfo.inputFormat = ImageWriteInfo::InputFormat::R8G8B8A8_UInt;
			// TODO: Check the alpha channel values to determine whether we actually need a full alpha channel?
			imgWriteInfo.outputFormat = ImageWriteInfo::OutputFormat::ColorMapSmoothAlpha;
		}
		switch(format)
		{
		case Anvil::Format::BC1_RGBA_SRGB_BLOCK:
		case Anvil::Format::BC1_RGBA_UNORM_BLOCK:
		case Anvil::Format::BC1_RGB_SRGB_BLOCK:
		case Anvil::Format::BC1_RGB_UNORM_BLOCK:
			imgWriteInfo.outputFormat = ImageWriteInfo::OutputFormat::BC1;
			break;
		case Anvil::Format::BC2_SRGB_BLOCK:
		case Anvil::Format::BC2_UNORM_BLOCK:
			imgWriteInfo.outputFormat = ImageWriteInfo::OutputFormat::BC2;
			break;
		case Anvil::Format::BC3_SRGB_BLOCK:
		case Anvil::Format::BC3_UNORM_BLOCK:
			imgWriteInfo.outputFormat = ImageWriteInfo::OutputFormat::BC3;
			break;
		case Anvil::Format::BC4_SNORM_BLOCK:
		case Anvil::Format::BC4_UNORM_BLOCK:
			imgWriteInfo.outputFormat = ImageWriteInfo::OutputFormat::BC4;
			break;
		case Anvil::Format::BC5_SNORM_BLOCK:
		case Anvil::Format::BC5_UNORM_BLOCK:
			imgWriteInfo.outputFormat = ImageWriteInfo::OutputFormat::BC5;
			break;
		case Anvil::Format::BC6H_SFLOAT_BLOCK:
		case Anvil::Format::BC6H_UFLOAT_BLOCK:
			imgWriteInfo.inputFormat = ImageWriteInfo::InputFormat::R16G16B16A16_Float;
			imgWriteInfo.outputFormat = ImageWriteInfo::OutputFormat::BC6;
			break;
		case Anvil::Format::BC7_SRGB_BLOCK:
		case Anvil::Format::BC7_UNORM_BLOCK:
			imgWriteInfo.inputFormat = ImageWriteInfo::InputFormat::R16G16B16A16_Float;
			imgWriteInfo.outputFormat = ImageWriteInfo::OutputFormat::BC7;
			break;
		}
	}
	absPath = "";
	// Save the DDS image and make sure the file exists
	if(c_game->SaveImage(*tex->texture->GetImage(),ddsPath,imgWriteInfo) && FileManager::FindAbsolutePath(ddsPath +".dds",absPath))
	{
		outSuccess = true;
		outConverted = true;
		return absPath;
	}
	// Something went wrong, fall back to error texture!
	return get_abs_error_texture_path();
}

static std::optional<std::string> prepare_texture(ccl::Scene &scene,TextureInfo *texInfo)
{
	if(texInfo == nullptr)
		return {};
	auto success = false;
	auto converted = false;
	auto result = prepare_texture(texInfo,success,converted);
	if(success == false)
	{
		Con::cwar<<"WARNING: Unable to prepare texture '";
		if(texInfo)
			Con::cwar<<texInfo->name;
		else
			Con::cwar<<"Unknown";
		Con::cwar<<"'! Using error texture instead..."<<Con::endl;
	}
	else
	{
		if(converted)
			Con::cout<<"Converted texture '"<<texInfo->name<<"' to DDS!"<<Con::endl;

		ccl::ImageMetaData metaData;
		if(scene.image_manager->get_image_metadata(*result,nullptr,ccl::u_colorspace_raw,metaData) == false)
		{
			Con::cwar<<"WARNING: Texture '"<<texInfo->name<<"' has format which is incompatible with cycles! Falling back to error texture..."<<Con::endl;
			result = get_abs_error_texture_path();
			if(scene.image_manager->get_image_metadata(*result,nullptr,ccl::u_colorspace_raw,metaData) == false)
			{
				Con::cwar<<"WARNING: Error texture also not compatible! Falling back to untextured!"<<Con::endl;
				result = {};
			}
		}
	}

	return result;
}

bool cycles::Scene::IsValidTexture(const std::string &filePath) const
{
	std::string ext;
	if(ufile::get_extension(filePath,&ext) == false || ustring::compare(ext,"dds",false) == false)
		return false;
	return FileManager::Exists(filePath,fsys::SearchFlags::Local);
}

ccl::ImageTextureNode *cycles::Scene::AssignTexture(Shader &shader,const std::string &texIdentifier,const std::string &texFilePath) const
{
	auto *node = static_cast<ccl::ImageTextureNode*>(**shader.AddNode("image_texture",texIdentifier));
	node->filename = texFilePath;
	return node;
}

void cycles::Scene::LinkNormalMap(Shader &shader,Material &mat,const std::string &meshName,const std::string &toNodeName,const std::string &toSocketName)
{
	// Normal map
	auto normalTexPath = prepare_texture(m_scene,mat.GetNormalMap());
	if(normalTexPath.has_value() == false)
		return;
	auto *nodeImgNormal = AssignTexture(shader,"img_normal_map",*normalTexPath);
	if(nodeImgNormal == nullptr)
		return;
	nodeImgNormal->colorspace = ccl::u_colorspace_raw;

	// We need to invert the y-axis for cycles, so we separate the rgb components, invert the g channel and put them back together
	// Separate rgb components of input image
	auto *nodeSeparate = static_cast<ccl::SeparateRGBNode*>(**shader.AddNode("separate_rgb","separate_normal"));
	shader.Link("img_normal_map","color","separate_normal","color");

	// Invert y-axis of normal
	auto *nodeInvert = static_cast<ccl::MathNode*>(**shader.AddNode("math","invert_normal_y"));
	nodeInvert->type = ccl::NodeMathType::NODE_MATH_SUBTRACT;
	nodeInvert->value1 = 1.f;
	shader.Link("separate_normal","g","invert_normal_y","value2");

	// Re-combine rgb components
	auto *nodeCombine = static_cast<ccl::CombineRGBNode*>(**shader.AddNode("combine_rgb","combine_normal"));
	shader.Link("separate_normal","r","combine_normal","r");
	shader.Link("invert_normal_y","value","combine_normal","g");
	shader.Link("separate_normal","b","combine_normal","b");

	auto *nodeNormalMap = static_cast<ccl::NormalMapNode*>(**shader.AddNode("normal_map","nmap"));
	nodeNormalMap->space = ccl::NodeNormalMapSpace::NODE_NORMAL_MAP_TANGENT;
	nodeNormalMap->attribute = meshName;
	shader.Link("combine_normal","image","nmap","color");

	shader.Link("nmap","normal",toNodeName,toSocketName);
}

cycles::PShader cycles::Scene::CreateShader(const std::string &meshName,Model &mdl,ModelSubMesh &subMesh)
{
	// Make sure all textures have finished loading
	static_cast<CMaterialManager&>(client->GetMaterialManager()).GetTextureManager().WaitForTextures();

	auto texIdx = subMesh.GetTexture();
	auto *mat = mdl.GetMaterial(texIdx);
	auto *diffuseMap = mat ? mat->GetDiffuseMap() : nullptr;
	auto diffuseTexPath = prepare_texture(m_scene,diffuseMap);
	if(diffuseTexPath.has_value() == false)
		return nullptr;
	auto shader = cycles::Shader::Create(*this,"floor");

	enum class ShaderType : uint8_t
	{
		Disney = 0u,
		Toon
	};
	constexpr auto shaderType = ShaderType::Disney;

	const std::string bsdfName = "bsdf_scene";
	if(shaderType == ShaderType::Toon)
	{
		auto nodeBsdf = shader->AddNode("toon_bsdf",bsdfName);
		auto *pNodeBsdf = static_cast<ccl::ToonBsdfNode*>(**nodeBsdf);

		// Albedo map
		auto *nodeAlbedo = AssignTexture(*shader,"albedo",*diffuseTexPath);
		if(nodeAlbedo)
			shader->Link("albedo","color",bsdfName,"color");
		LinkNormalMap(*shader,*mat,meshName,bsdfName,"normal");
	}
	else
	{
		auto nodeBsdf = shader->AddNode("principled_bsdf",bsdfName);
		auto *pNodeBsdf = static_cast<ccl::PrincipledBsdfNode*>(**nodeBsdf);

		// Albedo map
		auto *nodeAlbedo = AssignTexture(*shader,"albedo",*diffuseTexPath);
		if(nodeAlbedo)
			shader->Link("albedo","color",bsdfName,"base_color");

		LinkNormalMap(*shader,*mat,meshName,bsdfName,"normal");

		// Metalness map
		auto metalnessTexPath = prepare_texture(m_scene,mat->GetTextureInfo("metalness_map"));
		if(metalnessTexPath)
		{
			auto *nodeMetalness = AssignTexture(*shader,"img_metalness_map",*metalnessTexPath);
			if(nodeMetalness)
			{
				// We only need one channel value, so we'll just grab the red channel
				auto *nodeSeparate = static_cast<ccl::SeparateRGBNode*>(**shader->AddNode("separate_rgb","separate_metalness"));
				shader->Link("img_metalness_map","color","separate_metalness","color");

				// Use float as metallic value
				shader->Link("separate_metalness","r",bsdfName,"metallic");
			}
		}

		// Roughness map
		auto roughnessTexPath = prepare_texture(m_scene,mat->GetTextureInfo("roughness_map"));
		if(roughnessTexPath)
		{
			auto *nodeRoughness = AssignTexture(*shader,"img_roughness_map",*roughnessTexPath);
			if(nodeRoughness)
			{
				// We only need one channel value, so we'll just grab the red channel
				auto *nodeSeparate = static_cast<ccl::SeparateRGBNode*>(**shader->AddNode("separate_rgb","separate_roughness"));
				shader->Link("img_roughness_map","color","separate_roughness","color");

				// Use float as roughness value
				shader->Link("separate_roughness","r",bsdfName,"roughness");
			}
		}

		// Emission map
		auto emissionTexPath = prepare_texture(m_scene,mat->GetGlowMap());
		if(emissionTexPath)
		{
			auto *nodeEmission = AssignTexture(*shader,"emission",*emissionTexPath);
			if(nodeEmission)
				shader->Link("emission","color",bsdfName,"emission");
		}
	}
	//shader->Link(bsdfName,"bsdf","output","surface");

	{
		auto nodeAo = shader->AddNode("ambient_occlusion","ao");
		auto *pNodeAo = static_cast<ccl::AmbientOcclusionNode*>(**nodeAo);
		shader->Link("albedo","color","ao","color");
		shader->Link("nmap","normal","ao","normal");

		auto nodeEmission = shader->AddNode("emission","ao_emission");
		auto *pNodeEmission = static_cast<ccl::EmissionNode*>(**nodeEmission);
		shader->Link("ao","ao","ao_emission","color");

		shader->Link("ao_emission","emission","output","surface");
	}


	return shader;
}

void cycles::Scene::AddEntity(BaseEntity &ent)
{
	if(ent.IsPlayer() == false)
		return;
	auto mdl = ent.GetModel();
	if(mdl == nullptr)
		return;
	std::vector<std::shared_ptr<ModelMesh>> lodMeshes {};
	std::vector<uint32_t> bodyGroups {};
	bodyGroups.resize(mdl->GetBodyGroupCount());
	mdl->GetBodyGroupMeshes(bodyGroups,0,lodMeshes);

	// Pre-calculate number of vertices and triangles
	uint64_t numVerts = 0ull;
	uint64_t numTris = 0ull;
	for(auto &lodMesh : lodMeshes)
	{
		for(auto &subMesh : lodMesh->GetSubMeshes())
		{
			numVerts += subMesh->GetVertexCount();
			numTris += subMesh->GetTriangleCount();
		}
	}

	auto animC = ent.GetComponent<CAnimatedComponent>();
	
	// Create the mesh
	// TODO: If multiple entities are using same model, CACHE the mesh(es)! (Unless they're animated)
	std::string name = "ent_" +std::to_string(ent.GetLocalIndex());
	auto mesh = Mesh::Create(*this,name,numVerts,numTris);
	uint32_t meshTriIndexStartOffset = 0;
	for(auto &lodMesh : lodMeshes)
	{
		for(auto &subMesh : lodMesh->GetSubMeshes())
		{
			auto shader = CreateShader(name,*mdl,*subMesh);
			if(shader == nullptr)
				continue;
			auto shaderIdx = mesh->AddShader(*shader);
			auto &verts = subMesh->GetVertices();
			for(auto vertIdx=decltype(verts.size()){0u};vertIdx<verts.size();++vertIdx)
			{
				auto &v = verts.at(vertIdx);
				Vector3 pos;
				if(animC.valid() && animC->GetLocalVertexPosition(*subMesh,vertIdx,pos))
					mesh->AddVertex(pos,v.normal,v.tangent,v.uv); // TODO: Apply animation matrices to normal!
				else
					mesh->AddVertex(v.position,v.normal,v.tangent,v.uv);
			}

			auto &tris = subMesh->GetTriangles();
			for(auto i=decltype(tris.size()){0u};i<tris.size();i+=3)
				mesh->AddTriangle(meshTriIndexStartOffset +tris.at(i),meshTriIndexStartOffset +tris.at(i +1),meshTriIndexStartOffset +tris.at(i +2),shaderIdx);
			meshTriIndexStartOffset += verts.size();
		}
	}

	// Create the object using the mesh
	physics::Transform t;
	ent.GetPose(t);
	auto o = Object::Create(*this,*mesh);
	o->SetPos(t.GetOrigin());
	o->SetRotation(t.GetRotation());
}
struct BakePixel {
	int primitive_id, object_id;
	float uv[2];
	float du_dx, du_dy;
	float dv_dx, dv_dy;
};

static void populate_bake_data(ccl::BakeData *data,
	const int object_id,
	BakePixel *pixel_array,
	const int num_pixels)
{
	BakePixel *bp = pixel_array;

	int i;
	for (i = 0; i < num_pixels; i++) {
		if (bp->object_id == object_id) {
			data->set(i, bp->primitive_id, bp->uv, bp->du_dx, bp->du_dy, bp->dv_dx, bp->dv_dy);
		}
		else {
			data->set_null(i);
		}
		++bp;
	}
}

typedef struct ZSpan {
	int rectx, recty; /* range for clipping */

	int miny1, maxy1, miny2, maxy2;             /* actual filled in range */
	const float *minp1, *maxp1, *minp2, *maxp2; /* vertex pointers detect min/max range in */
	std::vector<float> span1, span2;
} ZSpan;

typedef struct BakeDataZSpan {
	BakePixel *pixel_array;
	int primitive_id;
	//BakeImage *bk_image;
	uint32_t bakeImageWidth;
	uint32_t bakeImageHeight;
	std::vector<ZSpan> zspan;
	float du_dx, du_dy;
	float dv_dx, dv_dy;
} BakeDataZSpan;

static void bake_differentials(BakeDataZSpan *bd,
	const float *uv1,
	const float *uv2,
	const float *uv3)
{
	float A;

	/* assumes dPdu = P1 - P3 and dPdv = P2 - P3 */
	A = (uv2[0] - uv1[0]) * (uv3[1] - uv1[1]) - (uv3[0] - uv1[0]) * (uv2[1] - uv1[1]);

	if (fabsf(A) > FLT_EPSILON) {
		A = 0.5f / A;

		bd->du_dx = (uv2[1] - uv3[1]) * A;
		bd->dv_dx = (uv3[1] - uv1[1]) * A;

		bd->du_dy = (uv3[0] - uv2[0]) * A;
		bd->dv_dy = (uv1[0] - uv3[0]) * A;
	}
	else {
		bd->du_dx = bd->du_dy = 0.0f;
		bd->dv_dx = bd->dv_dy = 0.0f;
	}
}

static void zbuf_init_span(ZSpan *zspan)
{
	zspan->miny1 = zspan->miny2 = zspan->recty + 1;
	zspan->maxy1 = zspan->maxy2 = -1;
	zspan->minp1 = zspan->maxp1 = zspan->minp2 = zspan->maxp2 = NULL;
}

float min_ff(float a, float b)
{
	return (a < b) ? a : b;
}
int min_ii(int a, int b)
{
	return (a < b) ? a : b;
}
int max_ii(int a, int b)
{
	return (b < a) ? a : b;
}
float max_ff(float a, float b)
{
	return (a > b) ? a : b;
}
static void zbuf_add_to_span(ZSpan *zspan, const float v1[2], const float v2[2])
{
	const float *minv, *maxv;
	float *span;
	float xx1, dx0, xs0;
	int y, my0, my2;

	if (v1[1] < v2[1]) {
		minv = v1;
		maxv = v2;
	}
	else {
		minv = v2;
		maxv = v1;
	}

	my0 = ceil(minv[1]);
	my2 = floor(maxv[1]);

	if (my2 < 0 || my0 >= zspan->recty) {
		return;
	}

	/* clip top */
	if (my2 >= zspan->recty) {
		my2 = zspan->recty - 1;
	}
	/* clip bottom */
	if (my0 < 0) {
		my0 = 0;
	}

	if (my0 > my2) {
		return;
	}
	/* if (my0>my2) should still fill in, that way we get spans that skip nicely */

	xx1 = maxv[1] - minv[1];
	if (xx1 > FLT_EPSILON) {
		dx0 = (minv[0] - maxv[0]) / xx1;
		xs0 = dx0 * (minv[1] - my2) + minv[0];
	}
	else {
		dx0 = 0.0f;
		xs0 = min_ff(minv[0], maxv[0]);
	}

	/* empty span */
	if (zspan->maxp1 == NULL) {
		span = zspan->span1.data();
	}
	else { /* does it complete left span? */
		if (maxv == zspan->minp1 || minv == zspan->maxp1) {
			span = zspan->span1.data();
		}
		else {
			span = zspan->span2.data();
		}
	}

	if (span == zspan->span1.data()) {
		//      printf("left span my0 %d my2 %d\n", my0, my2);
		if (zspan->minp1 == NULL || zspan->minp1[1] > minv[1]) {
			zspan->minp1 = minv;
		}
		if (zspan->maxp1 == NULL || zspan->maxp1[1] < maxv[1]) {
			zspan->maxp1 = maxv;
		}
		if (my0 < zspan->miny1) {
			zspan->miny1 = my0;
		}
		if (my2 > zspan->maxy1) {
			zspan->maxy1 = my2;
		}
	}
	else {
		//      printf("right span my0 %d my2 %d\n", my0, my2);
		if (zspan->minp2 == NULL || zspan->minp2[1] > minv[1]) {
			zspan->minp2 = minv;
		}
		if (zspan->maxp2 == NULL || zspan->maxp2[1] < maxv[1]) {
			zspan->maxp2 = maxv;
		}
		if (my0 < zspan->miny2) {
			zspan->miny2 = my0;
		}
		if (my2 > zspan->maxy2) {
			zspan->maxy2 = my2;
		}
	}

	for (y = my2; y >= my0; y--, xs0 += dx0) {
		/* xs0 is the xcoord! */
		span[y] = xs0;
	}
}

void zspan_scanconvert(ZSpan *zspan,
	void *handle,
	float *v1,
	float *v2,
	float *v3,
	void (*func)(void *, int, int, float, float))
{
	float x0, y0, x1, y1, x2, y2, z0, z1, z2;
	float u, v, uxd, uyd, vxd, vyd, uy0, vy0, xx1;
	const float *span1, *span2;
	int i, j, x, y, sn1, sn2, rectx = zspan->rectx, my0, my2;

	/* init */
	zbuf_init_span(zspan);

	/* set spans */
	zbuf_add_to_span(zspan, v1, v2);
	zbuf_add_to_span(zspan, v2, v3);
	zbuf_add_to_span(zspan, v3, v1);

	/* clipped */
	if (zspan->minp2 == NULL || zspan->maxp2 == NULL) {
		return;
	}

	my0 = max_ii(zspan->miny1, zspan->miny2);
	my2 = min_ii(zspan->maxy1, zspan->maxy2);

	//  printf("my %d %d\n", my0, my2);
	if (my2 < my0) {
		return;
	}

	/* ZBUF DX DY, in floats still */
	x1 = v1[0] - v2[0];
	x2 = v2[0] - v3[0];
	y1 = v1[1] - v2[1];
	y2 = v2[1] - v3[1];

	z1 = 1.0f; /* (u1 - u2) */
	z2 = 0.0f; /* (u2 - u3) */

	x0 = y1 * z2 - z1 * y2;
	y0 = z1 * x2 - x1 * z2;
	z0 = x1 * y2 - y1 * x2;

	if (z0 == 0.0f) {
		return;
	}

	xx1 = (x0 * v1[0] + y0 * v1[1]) / z0 + 1.0f;
	uxd = -(double)x0 / (double)z0;
	uyd = -(double)y0 / (double)z0;
	uy0 = ((double)my2) * uyd + (double)xx1;

	z1 = -1.0f; /* (v1 - v2) */
	z2 = 1.0f;  /* (v2 - v3) */

	x0 = y1 * z2 - z1 * y2;
	y0 = z1 * x2 - x1 * z2;

	xx1 = (x0 * v1[0] + y0 * v1[1]) / z0;
	vxd = -(double)x0 / (double)z0;
	vyd = -(double)y0 / (double)z0;
	vy0 = ((double)my2) * vyd + (double)xx1;

	/* correct span */
	span1 = zspan->span1.data() + my2;
	span2 = zspan->span2.data() + my2;

	for (i = 0, y = my2; y >= my0; i++, y--, span1--, span2--) {

		sn1 = floor(min_ff(*span1, *span2));
		sn2 = floor(max_ff(*span1, *span2));
		sn1++;

		if (sn2 >= rectx) {
			sn2 = rectx - 1;
		}
		if (sn1 < 0) {
			sn1 = 0;
		}

		u = (((double)sn1 * uxd) + uy0) - (i * uyd);
		v = (((double)sn1 * vxd) + vy0) - (i * vyd);

		for (j = 0, x = sn1; x <= sn2; j++, x++) {
			func(handle, x, y, u + (j * uxd), v + (j * vxd));
		}
	}
}

void copy_v2_fl2(float v[2], float x, float y)
{
	v[0] = x;
	v[1] = y;
}

static void store_bake_pixel(void *handle, int x, int y, float u, float v)
{
	BakeDataZSpan *bd = (BakeDataZSpan *)handle;
	BakePixel *pixel;

	const int width = bd->bakeImageWidth;
	const size_t offset = 0;
	const int i = offset + y * width + x;

	pixel = &bd->pixel_array[i];
	pixel->primitive_id = bd->primitive_id;

	/* At this point object_id is always 0, since this function runs for the
	* low-poly mesh only. The object_id lookup indices are set afterwards. */

	copy_v2_fl2(pixel->uv, u, v);

	pixel->du_dx = bd->du_dx;
	pixel->du_dy = bd->du_dy;
	pixel->dv_dx = bd->dv_dx;
	pixel->dv_dy = bd->dv_dy;
	pixel->object_id = 0; // TODO
}

constexpr uint32_t OUTPUT_IMAGE_WIDTH = 512;
constexpr uint32_t OUTPUT_IMAGE_HEIGHT = 512;
static void prepare_bake_data(cycles::Mesh &mesh,BakePixel *pixelArray,uint32_t numPixels)
{
	/* initialize all pixel arrays so we know which ones are 'blank' */
	for(auto i=decltype(numPixels){0u};i<numPixels;++i)
	{
		pixelArray[i].primitive_id = -1;
		pixelArray[i].object_id = -1;
	}


	BakeDataZSpan bd;
	bd.pixel_array = pixelArray;
	uint32_t numBakeImages = 1u;
	bd.zspan.resize(numBakeImages);

	for(auto &zspan : bd.zspan)
	{
		zspan.rectx = OUTPUT_IMAGE_WIDTH;
		zspan.recty = OUTPUT_IMAGE_HEIGHT;

		zspan.span1.resize(zspan.recty);
		zspan.span2.resize(zspan.recty);
	}

	auto *uvs = mesh.GetUVs();
	auto *cclMesh = *mesh;
	auto numTris = cclMesh->triangles.size() /3;
	for(auto i=decltype(numTris){0u};i<numTris;++i)
	{
		int32_t imageId = 0;
		bd.bakeImageWidth = OUTPUT_IMAGE_WIDTH;
		bd.bakeImageHeight = OUTPUT_IMAGE_HEIGHT;
		bd.primitive_id = i;

		float vec[3][2];
		auto *tri = &cclMesh->triangles[i *3];
		for(uint8_t j=0;j<3;++j)
		{
			const float *uv = reinterpret_cast<const float*>(&uvs[tri[j]]);

			/* Note, workaround for pixel aligned UVs which are common and can screw up our
			* intersection tests where a pixel gets in between 2 faces or the middle of a quad,
			* camera aligned quads also have this problem but they are less common.
			* Add a small offset to the UVs, fixes bug #18685 - Campbell */
			vec[j][0] = uv[0] * (float)bd.bakeImageWidth - (0.5f + 0.001f);
			vec[j][1] = uv[1] * (float)bd.bakeImageHeight - (0.5f + 0.002f);
		}

		bake_differentials(&bd, vec[0], vec[1], vec[2]);
		zspan_scanconvert(&bd.zspan[imageId], (void *)&bd, vec[0], vec[1], vec[2], store_bake_pixel);
	}
}

#include <pragma/util/util_tga.hpp>
void cycles::Scene::Start()
{
	ccl::BufferParams bufferParams {};
	bufferParams.width = m_scene.camera->width;
	bufferParams.height = m_scene.camera->height;
	bufferParams.full_width = m_scene.camera->width;
	bufferParams.full_height = m_scene.camera->height;

	m_session->scene = &m_scene;
	m_session->reset(bufferParams,m_session->params.samples);
	
	m_camera->Finalize();
	// Note: Lights and objects have to be initialized before shaders, because they may
	// create additional shaders.
	for(auto &light : m_lights)
		light->Finalize();
	for(auto &o : m_objects)
		o->Finalize();
	for(auto &shader : m_shaders)
		shader->Finalize();
	//m_session->start();

	{
		m_scene.bake_manager->set_baking(true);
		m_session->load_kernels();

		ccl::Pass::add(ccl::PASS_LIGHT,m_scene.film->passes);

		m_scene.film->tag_update(&m_scene);
		m_scene.integrator->tag_update(&m_scene);

		m_scene.bake_manager->set_shader_limit(256,256);
		m_session->tile_manager.set_samples(m_session->params.samples);
		m_session->reset(bufferParams, m_session->params.samples);
/*
-		buffer_params	{width=960 height=540 full_x=0 ...}	ccl::BufferParams
width	960	int
height	540	int
full_x	0	int
full_y	0	int
full_width	960	int
full_height	540	int
-		passes	{ size=1 }	ccl::vector<ccl::Pass,ccl::GuardedAllocator<ccl::Pass> >
[capacity]	1	__int64
+		[allocator]	{...}	std::_Compressed_pair<ccl::GuardedAllocator<ccl::Pass>,std::_Vector_val<std::_Simple_types<ccl::Pass> >,1>
+		[0]	{type=PASS_COMBINED (1) components=4 filter=true ...}	ccl::Pass
+		[Raw View]	{...}	ccl::vector<ccl::Pass,ccl::GuardedAllocator<ccl::Pass> >
denoising_data_pass	false	bool
denoising_clean_pass	false	bool
denoising_prefiltered_pass	false	bool

*/

		auto shaderType = ccl::ShaderEvalType::SHADER_EVAL_AO;

		int bake_pass_filter =  ccl::BAKE_FILTER_AO;
		bake_pass_filter = ccl::BakeManager::shader_type_to_pass_filter(shaderType, bake_pass_filter);

		auto numPixels = OUTPUT_IMAGE_WIDTH *OUTPUT_IMAGE_HEIGHT;
		std::vector<BakePixel> pixelArray;
		pixelArray.resize(numPixels);
		prepare_bake_data(m_objects.front()->GetMesh(),pixelArray.data(),numPixels);

		ccl::BakeData *bake_data = NULL;
		bake_data = m_scene.bake_manager->init(0 /* object id */,0 /* triOffset */, numPixels /* numPixels */);
		populate_bake_data(bake_data, 0 /* objectId */, pixelArray.data(), numPixels);

		m_session->tile_manager.set_samples(m_session->params.samples);
		m_session->reset(bufferParams, m_session->params.samples);
		m_session->update_scene();

		auto *pSession = m_session.get();
		m_session->progress.set_update_callback([pSession]() {
			std::cout<<"Progress: "<<pSession->progress.get_progress()<<std::endl;
		});

		std::vector<float> result;
		result.resize(numPixels *4,100.f);
		auto r = m_scene.bake_manager->bake(m_scene.device,&m_scene.dscene,&m_scene,m_session->progress,shaderType,bake_pass_filter,bake_data,result.data());


		std::vector<uint8_t> pixels {};
		pixels.resize(numPixels *3);
		for(auto i=decltype(numPixels){0u};i<numPixels;++i)
		{
			auto *inData = result.data() +i *4;
			auto *outData = pixels.data() +i *3;
			for(uint8_t j=0;j<3;++j)
				outData[j] = static_cast<uint8_t>(umath::clamp(inData[j] *255.f,0.f,255.f));
		}
		util::tga::write_tga("test_ao.tga",OUTPUT_IMAGE_WIDTH,OUTPUT_IMAGE_HEIGHT,pixels);

		std::cout<<"Bake result: "<<r<<std::endl;



	/*	if (!session->progress.get_cancel() && bake_data) {
			scene->bake_manager->bake(scene->device,
				&scene->dscene,
				scene,
				session->progress,
				shader_type,
				bake_pass_filter,
				bake_data,
				result);
		}
		*/
	}
}

float cycles::Scene::GetProgress() const
{
	return m_session->progress.get_progress();
}
bool cycles::Scene::IsComplete() const
{
	return GetProgress() == 1.f;
}
bool cycles::Scene::IsCancelled() const {return m_bCancelled;}
void cycles::Scene::Cancel()
{
	m_bCancelled = true;
	m_session->set_pause(true);
}
void cycles::Scene::Wait()
{
	m_session->wait();
}
void cycles::Scene::SetProgressCallback(const std::function<void(float)> &progressCallback)
{
	m_progressCallback = progressCallback;
}

const std::vector<cycles::PShader> &cycles::Scene::GetShaders() const {return const_cast<Scene*>(this)->GetShaders();}
std::vector<cycles::PShader> &cycles::Scene::GetShaders() {return m_shaders;}
const std::vector<cycles::PObject> &cycles::Scene::GetObjects() const {return const_cast<Scene*>(this)->GetObjects();}
std::vector<cycles::PObject> &cycles::Scene::GetObjects() {return m_objects;}
const std::vector<cycles::PLight> &cycles::Scene::GetLights() const {return const_cast<Scene*>(this)->GetLights();}
std::vector<cycles::PLight> &cycles::Scene::GetLights() {return m_lights;}

ccl::Session *cycles::Scene::GetCCLSession() {return m_session.get();}

ccl::Scene *cycles::Scene::operator->() {return &m_scene;}
ccl::Scene *cycles::Scene::operator*() {return &m_scene;}

ccl::ShaderOutput *cycles::Scene::FindShaderNodeOutput(ccl::ShaderNode &node,const std::string &output)
{
	auto it = std::find_if(node.outputs.begin(),node.outputs.end(),[&output](const ccl::ShaderOutput *shOutput) {
		return ccl::string_iequals(shOutput->socket_type.name.string(),output);
	});
	return (it != node.outputs.end()) ? *it : nullptr;
}

ccl::ShaderNode *cycles::Scene::FindShaderNode(ccl::ShaderGraph &graph,const std::string &nodeName)
{
	auto it = std::find_if(graph.nodes.begin(),graph.nodes.end(),[&nodeName](const ccl::ShaderNode *node) {
		return node->name == nodeName;
	});
	return (it != graph.nodes.end()) ? *it : nullptr;
}

ccl::float3 cycles::Scene::ToCyclesPosition(const Vector3 &pos)
{
	auto scale = util::units_to_metres(1.f);
	ccl::float3 cpos {-pos.x,pos.y,pos.z};
	cpos *= scale;
	return cpos;
}

ccl::float3 cycles::Scene::ToCyclesNormal(const Vector3 &n)
{
	return ccl::float3{n.x,-n.y,-n.z};
}

ccl::float2 cycles::Scene::ToCyclesUV(const Vector2 &uv)
{
	return ccl::float2{uv.x,1.f -uv.y};
}

ccl::Transform cycles::Scene::ToCyclesTransform(const pragma::physics::Transform &t)
{
	Vector3 axis;
	float angle;
	uquat::to_axis_angle(t.GetRotation(),axis,angle);
	auto cclT = ccl::transform_identity();
	cclT = cclT *ccl::transform_rotate(angle,Scene::ToCyclesNormal(axis));
	cclT = ccl::transform_translate(Scene::ToCyclesPosition(t.GetOrigin())) *cclT;
	return cclT;
}

float cycles::Scene::ToCyclesLength(float len)
{
	auto scale = util::units_to_metres(1.f);
	return len *scale;
}
#pragma optimize("",on)
