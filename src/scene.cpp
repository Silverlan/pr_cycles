#include "pr_cycles/scene.hpp"
#include "pr_cycles/mesh.hpp"
#include "pr_cycles/camera.hpp"
#include "pr_cycles/shader.hpp"
#include "pr_cycles/object.hpp"
#include "pr_cycles/light.hpp"
#include "pr_cycles/util_baking.hpp"
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
#include <pragma/entities/components/c_light_map_component.hpp>
#include <sharedutils/util_file.h>
#include <texturemanager/texture.h>
#include <cmaterialmanager.h>
#include <pr_dds.hpp>
#include <buffers/prosper_dynamic_resizable_buffer.hpp>

// ccl happens to have the same include guard name as sharedutils, so we have to undef it here
#undef __UTIL_STRING_H__
#include <sharedutils/util_string.h>

using namespace pragma::modules;

#pragma optimize("",off)

extern DLLCENGINE CEngine *c_engine;
extern DLLCLIENT ClientState *client;
extern DLLCLIENT CGame *c_game;

cycles::PScene cycles::Scene::Create(RenderMode renderMode,const std::function<void(const uint8_t*,int,int,int)> &outputHandler,std::optional<uint32_t> sampleCount,bool hdrOutput,bool denoise)
{
	if(hdrOutput && renderMode != RenderMode::RenderImage)
		hdrOutput = false; // TODO
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
	if(sampleCount.has_value())
		sessionParams.samples = *sampleCount;
	else
	{
		switch(renderMode)
		{
		case RenderMode::BakeAmbientOcclusion:
		case RenderMode::BakeNormals:
		case RenderMode::BakeDiffuseLighting:
			sessionParams.samples = 1'225u;
			break;
		default:
			sessionParams.samples = 1'024u;
			break;
		}
	}
	// We need the scene-pointer in the callback-function, however the function has to be
	// defined before the scene is created, so we create a shared pointer that will be initialized after
	// the scene.
	auto ptrScene = std::make_shared<Scene*>(nullptr);
	auto ptrCclSession = std::make_shared<ccl::Session*>(nullptr);
	if(hdrOutput == false)
	{
		sessionParams.write_render_cb = [ptrScene,ptrCclSession,renderMode,outputHandler,denoise](const ccl::uchar *pixels,int w,int h,int channels) -> bool {
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

					// For some reason the image is flipped horizontally when rendering an image,
					// so we'll just flip it the right way here
					auto yCorrected = (renderMode == RenderMode::RenderImage) ? (w -y -1) : y;
					// We will also always have to flip the image vertically, since the data seems to be bottom->top and we need it top->bottom
					auto outOffset = (h -x -1) *w +yCorrected;
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

#ifdef ENABLE_TEST_AMBIENT_OCCLUSION
	if(renderMode != RenderMode::RenderImage)
	{
		sessionParams.background = true;
		sessionParams.progressive_refine = false;
		sessionParams.progressive = false;
		sessionParams.experimental = false;
		sessionParams.tile_size = {256,256};
		sessionParams.tile_order = ccl::TileOrder::TILE_BOTTOM_TO_TOP;
		sessionParams.start_resolution = 2147483647;
		sessionParams.pixel_size = 1;
		sessionParams.threads = 0;
		sessionParams.use_profiling = false;
		sessionParams.display_buffer_linear = true;
		sessionParams.run_denoising = false;
		sessionParams.write_denoising_passes = false;
		sessionParams.full_denoising = false;
		sessionParams.progressive_update_timeout = 1.0000000000000000;
		sessionParams.shadingsystem = ccl::SHADINGSYSTEM_SVM;
	}
#endif

	auto session = std::make_unique<ccl::Session>(sessionParams);
	*ptrCclSession = session.get();

	ccl::SceneParams sceneParams {};
	sceneParams.shadingsystem = ccl::SHADINGSYSTEM_SVM;

	auto *cclScene = new ccl::Scene{sceneParams,session->device}; // Object will be removed automatically by cycles
	cclScene->params.bvh_type = ccl::SceneParams::BVH_STATIC;

	auto *pSession = session.get();
	auto scene = PScene{new Scene{std::move(session),*cclScene,renderMode}};
	auto *pScene = scene.get();
	pSession->progress.set_update_callback([pScene]() {
		if(pScene->m_progressCallback)
			pScene->m_progressCallback(pScene->m_session->progress.get_progress());
		});
	scene->m_camera = Camera::Create(*scene);
	*ptrScene = pScene;
	return scene;
}

cycles::Scene::Scene(std::unique_ptr<ccl::Session> session,ccl::Scene &scene,RenderMode renderMode)
	: m_session{std::move(session)},m_scene{scene},m_renderMode{renderMode}
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

ccl::ImageTextureNode *cycles::Scene::AssignTexture(Shader &shader,const std::string &texIdentifier,const std::string &texFilePath,ColorSpace colorSpace) const
{
	auto *node = static_cast<ccl::ImageTextureNode*>(**shader.AddNode("image_texture",texIdentifier));
	node->filename = texFilePath;
	switch(colorSpace)
	{
	case ColorSpace::Raw:
		node->colorspace = ccl::u_colorspace_raw;
		break;
	case ColorSpace::SRGB:
		node->colorspace = ccl::u_colorspace_srgb;
		break;
	}
	return node;
}

void cycles::Scene::LinkNormalMap(Shader &shader,Material &mat,const std::string &meshName,const std::string &toNodeName,const std::string &toSocketName)
{
	// Normal map
	auto normalTexPath = prepare_texture(m_scene,mat.GetNormalMap());
	if(normalTexPath.has_value() == false)
		return;
	auto *nodeImgNormal = AssignTexture(shader,"img_normal_map",*normalTexPath,ColorSpace::Raw);
	if(nodeImgNormal == nullptr)
		return;
	auto *nodeNormalMap = static_cast<ccl::NormalMapNode*>(**shader.AddNode("normal_map","nmap"));
	nodeNormalMap->space = ccl::NodeNormalMapSpace::NODE_NORMAL_MAP_TANGENT;
	nodeNormalMap->attribute = meshName;

	constexpr auto flipYAxis = false;
	if(flipYAxis)
	{
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

		shader.Link("combine_normal","image","nmap","color");
	}
	else
		shader.Link("img_normal_map","color","nmap","color");
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
	auto shader = cycles::Shader::Create(*this,"object_shader");

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

#if 0
		// Albedo map
		auto *nodeAlbedo = AssignTexture(*shader,"albedo",*diffuseTexPath);
		if(nodeAlbedo)
			shader->Link("albedo","color",bsdfName,"base_color");
		LinkNormalMap(*shader,*mat,meshName,bsdfName,"normal");

		// Metalness map
		auto metalnessTexPath = prepare_texture(m_scene,mat->GetTextureInfo("metalness_map"));
		if(metalnessTexPath)
		{
			auto *nodeMetalness = AssignTexture(*shader,"img_metalness_map",*metalnessTexPath,ColorSpace::Raw);
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
			auto *nodeRoughness = AssignTexture(*shader,"img_roughness_map",*roughnessTexPath,ColorSpace::Raw);
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
#endif
	}
	shader->Link(bsdfName,"bsdf","output","surface");
	return shader;
}

void cycles::Scene::AddEntity(BaseEntity &ent)
{
	if(m_renderMode != RenderMode::RenderImage && m_objects.empty() == false)
	{
		Con::cwar<<"WARNING: Baking only supported for single objects, but attempted to add another one! Additional object(s) will be ignored!"<<Con::endl;
		return;
	}
#if 0
	if(m_renderMode == RenderMode::BakeDiffuseLighting && ent.IsWorld() == false)
	{
		Con::cwar<<"WARNING: Baking diffuse lighting currently only supported for world entity, but attempted to add an entity of a different type! Entity will be ignored!"<<Con::endl;
		return;
	}
#endif
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
				switch(m_renderMode)
				{
				case RenderMode::RenderImage:
				{
					Vector3 pos;
					// TODO: Do we really need the tangent?
					if(animC.valid() && animC->GetLocalVertexPosition(*subMesh,vertIdx,pos))
						mesh->AddVertex(pos,v.normal,v.tangent,v.uv); // TODO: Apply animation matrices to normal and tangent!
					else
						mesh->AddVertex(v.position,v.normal,v.tangent,v.uv);
					break;
				}
				default:
					// We're probably baking something (e.g. ao map), so we don't want to include the entity's animated pose.
					mesh->AddVertex(v.position,v.normal,v.tangent,v.uv);
					break;
				}
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
	if(m_renderMode == RenderMode::RenderImage) // We don't need the entity pose if we're baking
	{
		o->SetPos(t.GetOrigin());
		o->SetRotation(t.GetRotation());
	}
	else if(m_renderMode == RenderMode::BakeDiffuseLighting)
	{
		auto lightmapC = ent.GetComponent<pragma::CLightMapComponent>();
		if(lightmapC.valid())
		{
			// Lightmap uvs per mesh
			std::vector<std::vector<Vector2>> lightmapUvs {};
			lightmapC->ReadLightmapUvCoordinates(lightmapUvs);

			std::vector<ccl::float2> cclLightmapUvs {};
			cclLightmapUvs.resize(numTris *3);
			size_t uvOffset = 0;
			for(auto &lodMesh : lodMeshes)
			{
				for(auto &subMesh : lodMesh->GetSubMeshes())
				{
					auto refId = subMesh->GetReferenceId();
					if(refId >= lightmapUvs.size())
						continue;
					auto &meshUvs = lightmapUvs.at(refId);

					auto &tris = subMesh->GetTriangles();
					for(auto i=decltype(tris.size()){0u};i<tris.size();i+=3)
					{
						auto idx0 = tris.at(i);
						auto idx1 = tris.at(i +1);
						auto idx2 = tris.at(i +2);
						cclLightmapUvs.at(uvOffset +i) = Scene::ToCyclesUV(meshUvs.at(idx0));
						cclLightmapUvs.at(uvOffset +i +1) = Scene::ToCyclesUV(meshUvs.at(idx1));
						cclLightmapUvs.at(uvOffset +i +2) = Scene::ToCyclesUV(meshUvs.at(idx2));
					}
					uvOffset += tris.size();
				}
			}
			mesh->SetLightmapUVs(std::move(cclLightmapUvs));
		}
	}
}

void cycles::Scene::Start()
{
	ccl::BufferParams bufferParams {};
	bufferParams.width = m_scene.camera->width;
	bufferParams.height = m_scene.camera->height;
	bufferParams.full_width = m_scene.camera->width;
	bufferParams.full_height = m_scene.camera->height;

	auto imgWidth = bufferParams.width;
	auto imgHeight = bufferParams.height;

	m_session->scene = &m_scene;

	/*if(m_renderMode != RenderMode::RenderImage)
	{
		// TDOO: Remove this?
		// Update camera
		m_scene.camera->viewplane.left = -1.77777779;
		m_scene.camera->viewplane.right = 1.77777779;
		m_scene.camera->viewplane.bottom = -1.00000000;
		m_scene.camera->viewplane.top = 1.00000000;

		m_scene.camera->full_width = 1920;
		m_scene.camera->full_height = 1080;
		m_scene.camera->width = 1920;
		m_scene.camera->height = 1080;
		m_scene.camera->nearclip = 0.100000001;
		m_scene.camera->farclip = 100.000000;
		m_scene.camera->panorama_type = ccl::PANORAMA_FISHEYE_EQUISOLID;
		m_scene.camera->fisheye_fov = 3.14159274;
		m_scene.camera->fisheye_lens = 10.5000000;
		m_scene.camera->latitude_min = -1.57079637;
		m_scene.camera->latitude_max = 1.57079637;
		m_scene.camera->longitude_min = -3.14159274;
		m_scene.camera->longitude_max = 3.14159274;
		m_scene.camera->interocular_distance = 0.0649999976;
		m_scene.camera->convergence_distance = 1.94999993;
		m_scene.camera->use_spherical_stereo = false;
		m_scene.camera->use_pole_merge = false;
		m_scene.camera->pole_merge_angle_from = 1.04719758;
		m_scene.camera->pole_merge_angle_to = 1.30899692;
		m_scene.camera->aperture_ratio = 1.00000000;
		m_scene.camera->fov = 0.399596483;
		m_scene.camera->focaldistance = 0.000000000;
		m_scene.camera->aperturesize = 0.000000000;
		m_scene.camera->blades = 0;
		m_scene.camera->bladesrotation = 0.000000000;
		m_scene.camera->matrix.x.x = 0.685920656;
		m_scene.camera->matrix.x.y = -0.324013472;
		m_scene.camera->matrix.x.z = -0.651558220;
		m_scene.camera->matrix.x.w = 7.35889149;
		m_scene.camera->matrix.y.x = 0.727676332;
		m_scene.camera->matrix.y.y = 0.305420846;
		m_scene.camera->matrix.y.z = 0.614170372;
		m_scene.camera->matrix.y.w = -6.92579079;
		m_scene.camera->matrix.z.x = 0.000000000;
		m_scene.camera->matrix.z.y = 0.895395637;
		m_scene.camera->matrix.z.z = -0.445271403;
		m_scene.camera->matrix.z.w = 4.95830917;

		m_scene.camera->motion.clear();
		m_scene.camera->motion.resize(3,m_scene.camera->matrix);

		m_scene.camera->use_perspective_motion = false;
		m_scene.camera->shuttertime = 0.500000000;
		m_scene.camera->fov_pre = 0.399596483;
		m_scene.camera->fov_post = 0.399596483;
		m_scene.camera->motion_position = ccl::Camera::MOTION_POSITION_CENTER;
		m_scene.camera->rolling_shutter_type = ccl::Camera::ROLLING_SHUTTER_NONE;
		m_scene.camera->rolling_shutter_duration = 0.100000001;
		m_scene.camera->border.left = 0.000000000;
		m_scene.camera->border.right = 1.00000000;
		m_scene.camera->border.bottom = 0.000000000;
		m_scene.camera->border.top = 1.00000000;
		m_scene.camera->viewport_camera_border.left = 0.000000000;
		m_scene.camera->viewport_camera_border.right = 1.00000000;
		m_scene.camera->viewport_camera_border.bottom = 0.000000000;
		m_scene.camera->viewport_camera_border.top = 1.00000000;
		m_scene.camera->offscreen_dicing_scale = 4.00000000;
		*m_scene.dicing_camera = *m_scene.camera;
	}*/

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

	if(m_renderMode == RenderMode::RenderImage)
	{
		m_session->start();
		return;
	}
	
	// Baking cannot be done with cycles directly, we will have to
	// do some additional steps first.

	m_scene.bake_manager->set_baking(true);
	m_session->load_kernels();

	switch(m_renderMode)
	{
	case RenderMode::BakeAmbientOcclusion:
	case RenderMode::BakeDiffuseLighting:
		ccl::Pass::add(ccl::PASS_LIGHT,m_scene.film->passes);
		break;
	case RenderMode::BakeNormals:
		break;
	}

	m_scene.film->tag_update(&m_scene);
	m_scene.integrator->tag_update(&m_scene);

	
	/*{
		// TODO: Remove this?
		auto *graph = new ccl::ShaderGraph{};

		ccl::EmissionNode *emission = new ccl::EmissionNode();
		emission->color = ccl::make_float3(1.0f, 1.0f, 1.0f);
		emission->strength = 1.0f;
		graph->add(emission);

		auto *out = graph->output();
		graph->connect(emission->output("Emission"), out->input("Surface"));

		auto *shader = m_scene.default_light;
		shader->set_graph(graph);
		shader->tag_update(&m_scene);
	}
	*/
	// TODO: Shader limits are arbitrarily chosen, check how Blender does it?
	m_scene.bake_manager->set_shader_limit(256,256);
	m_session->tile_manager.set_samples(m_session->params.samples);

	ccl::ShaderEvalType shaderType;
	int bake_pass_filter;
	switch(m_renderMode)
	{
	case RenderMode::BakeAmbientOcclusion:
		shaderType = ccl::ShaderEvalType::SHADER_EVAL_AO;
		bake_pass_filter = ccl::BAKE_FILTER_AO;
		break;
	case RenderMode::BakeDiffuseLighting:
		shaderType = ccl::ShaderEvalType::SHADER_EVAL_DIFFUSE;
		bake_pass_filter = ccl::BAKE_FILTER_DIFFUSE | ccl::BAKE_FILTER_INDIRECT | ccl::BAKE_FILTER_DIRECT;
		break;
	case RenderMode::BakeNormals:
		shaderType = ccl::ShaderEvalType::SHADER_EVAL_NORMAL;
		bake_pass_filter = 0;
		break;
	}
	bake_pass_filter = ccl::BakeManager::shader_type_to_pass_filter(shaderType,bake_pass_filter);

	auto numPixels = imgWidth *imgHeight;

	if(m_objects.empty())
	{
		Con::cerr<<"ERROR: Baking requires an object in the scene, but no object has been added!"<<Con::endl;
		return;
	}
	if(m_objects.size() > 1)
		Con::cwar<<"WARNING: More than 1 object has been added to the scene for baking! This is not supported, only the first object will be used."<<Con::endl;

	auto &obj = m_objects.front();
	std::vector<baking::BakePixel> pixelArray;
	pixelArray.resize(numPixels);
	auto bakeLightmaps = (m_renderMode == RenderMode::BakeDiffuseLighting);
	baking::prepare_bake_data(obj->GetMesh(),pixelArray.data(),numPixels,imgWidth,imgHeight,bakeLightmaps);

	ccl::BakeData *bake_data = NULL;
	bake_data = m_scene.bake_manager->init(0 /* object id */,0 /* triOffset */, numPixels /* numPixels */);
	baking::populate_bake_data(bake_data, 0 /* objectId */, pixelArray.data(), numPixels);

	m_session->tile_manager.set_samples(m_session->params.samples);
	m_session->reset(bufferParams, m_session->params.samples);
	m_session->update_scene();

	auto *pSession = m_session.get();
	m_session->progress.set_update_callback([this,pSession]() {
		if(m_progressCallback)
			m_progressCallback(pSession->progress.get_progress());
	});

	std::vector<float> result;
	result.resize(numPixels *4,1.f); // TODO: Alpha = 0, Color = black?
	auto r = m_scene.bake_manager->bake(m_scene.device,&m_scene.dscene,&m_scene,m_session->progress,shaderType,bake_pass_filter,bake_data,result.data());
	if(r == false)
	{
		Con::cerr<<"ERROR: Baking failed for an unknown reason!"<<Con::endl;
		return;
	}

	// Converted baked data to rgba8
	std::vector<uint8_t> pixels {};
	pixels.resize(numPixels *4);
	for(auto i=decltype(numPixels){0u};i<numPixels;++i)
	{
		auto *inData = result.data() +i *4;
		auto *outData = pixels.data() +i *4;
		for(uint8_t j=0;j<4;++j)
			outData[j] = baking::unit_float_to_uchar_clamp(inData[j]);
	}

	// Apply margin
	// TODO: Margin only required for certain bake types?
	std::vector<uint8_t> mask_buffer {};
	mask_buffer.resize(numPixels);
	constexpr auto margin = 16u;

	baking::ImBuf ibuf {};
	ibuf.x = imgWidth;
	ibuf.y = imgHeight;
	ibuf.rect = pixels;

	baking::RE_bake_mask_fill(pixelArray, numPixels, reinterpret_cast<char*>(mask_buffer.data()));
	baking::RE_bake_margin(&ibuf, mask_buffer, margin);

	m_session->params.write_render_cb(ibuf.rect.data(),imgWidth,imgHeight,4 /* channels */);
	m_session->params.write_render_cb = nullptr; // Make sure it's not called on destruction
}

cycles::Scene::RenderMode cycles::Scene::GetRenderMode() const {return m_renderMode;}
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
#ifdef ENABLE_TEST_AMBIENT_OCCLUSION
	ccl::float3 cpos {pos.x,pos.y,pos.z};
#else
	ccl::float3 cpos {-pos.x,pos.y,pos.z};
#endif
	cpos *= scale;
	return cpos;
}

ccl::float3 cycles::Scene::ToCyclesNormal(const Vector3 &n)
{
#ifdef ENABLE_TEST_AMBIENT_OCCLUSION
	return ccl::float3{n.x,n.y,n.z};
#else
	return ccl::float3{-n.x,n.y,n.z};
#endif
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
