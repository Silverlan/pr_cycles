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

cycles::PScene cycles::Scene::Create(RenderMode renderMode,const std::function<void(const uint8_t*,int,int,int)> &outputHandler,std::optional<uint32_t> sampleCount,bool hdrOutput,bool denoise)
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
	if(sampleCount.has_value())
		sessionParams.samples = *sampleCount;
	else
	{
		switch(renderMode)
		{
		case RenderMode::BakeAmbientOcclusion:
		case RenderMode::BakeNormals:
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

#ifdef ENABLE_TEST_AMBIENT_OCCLUSION
	sessionParams.background = true;
	sessionParams.progressive_refine = false;
	sessionParams.progressive = false;
	sessionParams.experimental = false;
	sessionParams.samples = 1225;
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

#ifdef ENABLE_TEST_AMBIENT_OCCLUSION
template<typename T>
T read_value(std::ifstream &s)
{
	T r;
	s.read(reinterpret_cast<char*>(&r),sizeof(T));
	return r;
}
#endif

cycles::PShader cycles::Scene::CreateShader(const std::string &meshName,Model &mdl,ModelSubMesh &subMesh)
{
	// Make sure all textures have finished loading
	static_cast<CMaterialManager&>(client->GetMaterialManager()).GetTextureManager().WaitForTextures();

	auto texIdx = subMesh.GetTexture();
	auto *mat = mdl.GetMaterial(texIdx);
#ifdef ENABLE_TEST_AMBIENT_OCCLUSION
	mat = client->LoadMaterial("models/player/soldier/soldier_d.wmi");
#endif
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

		// Albedo map
		auto *nodeAlbedo = AssignTexture(*shader,"albedo",*diffuseTexPath);
		if(nodeAlbedo)
			shader->Link("albedo","color",bsdfName,"base_color");
#ifndef ENABLE_TEST_AMBIENT_OCCLUSION
		LinkNormalMap(*shader,*mat,meshName,bsdfName,"normal");
#endif

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
	shader->Link(bsdfName,"bsdf","output","surface");

	/*{
	auto nodeAo = shader->AddNode("ambient_occlusion","ao");
	auto *pNodeAo = static_cast<ccl::AmbientOcclusionNode*>(**nodeAo);
	shader->Link("albedo","color","ao","color");
	shader->Link("nmap","normal","ao","normal");

	auto nodeEmission = shader->AddNode("emission","ao_emission");
	auto *pNodeEmission = static_cast<ccl::EmissionNode*>(**nodeEmission);
	shader->Link("ao","ao","ao_emission","color");

	shader->Link("ao_emission","emission","output","surface");
	}
	*/

	return shader;
}

void cycles::Scene::AddEntity(BaseEntity &ent)
{
#ifndef ENABLE_TEST_AMBIENT_OCCLUSION_2
	if(ent.IsPlayer() == false)
		return;
#endif
	auto mdl = ent.GetModel();
#ifdef ENABLE_TEST_AMBIENT_OCCLUSION
	//mdl = c_game->LoadModel("cube.wmd");
#endif
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
#ifdef ENABLE_TEST_AMBIENT_OCCLUSION
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
#endif
#ifndef ENABLE_TEST_AMBIENT_OCCLUSION
	//cycles::PMesh mesh;
	{
		std::ifstream f("E:/projects/pragma/build_winx64/output/ccl_mesh.bin", std::ios::binary);
		auto numVerts = read_value<uint32_t>(f);

		auto &subMesh = *lodMeshes.front()->GetSubMeshes().front();

		std::vector<ccl::float3> verts {};
		verts.resize(numVerts);
		f.read(reinterpret_cast<char*>(verts.data()),verts.size() *sizeof(verts.front()));

		auto numTris = read_value<uint32_t>(f);
		std::vector<int> tris {};
		tris.resize(numTris *3);
		f.read(reinterpret_cast<char*>(tris.data()),tris.size() *sizeof(tris.front()));

		std::vector<ccl::float4> normals {};
		normals.resize(numVerts);
		f.read(reinterpret_cast<char*>(normals.data()),normals.size() *sizeof(normals.front()));

		{
			for(auto &v : subMesh.GetVertices())
			{
				auto cclPos = Scene::ToCyclesPosition(v.position);
				auto matchFound = false;
				for(auto &vOther : verts)
				{
					auto f0 = umath::abs(vOther.x /cclPos.x);
					auto f1 = umath::abs(vOther.y /cclPos.y);
					auto f2 = umath::abs(vOther.z /cclPos.z);
					if(umath::abs(f1 -f0) < 0.001f && umath::abs(f2 -f0) < 0.001f)
					{
						matchFound = true;
						std::cout<<"FOUND MATHC!"<<std::endl;
					}
				}
				if(matchFound == false)
					std::cout<<"NO MATCH FOUND!"<<std::endl;

			}
		}

		std::vector<ccl::float4> generated {};
		generated.resize(numVerts);
		f.read(reinterpret_cast<char*>(generated.data()),generated.size() *sizeof(generated.front()));

		std::vector<ccl::float2> uvs {};
		uvs.resize(numTris *3);
		f.read(reinterpret_cast<char*>(uvs.data()),uvs.size() *sizeof(uvs.front()));

		std::vector<ccl::float4> uvTangents {};
		uvTangents.resize(numTris *3);
		f.read(reinterpret_cast<char*>(uvTangents.data()),uvTangents.size() *sizeof(uvTangents.front()));

		std::vector<float> uvTangentSigns {};
		uvTangentSigns.resize(numTris *3);
		f.read(reinterpret_cast<char*>(uvTangentSigns.data()),uvTangentSigns.size() *sizeof(uvTangentSigns.front()));
		f.close();
		/*
		mesh = Mesh::Create(*this,name,numVerts,numTris);
		auto shader = CreateShader(name,*mdl,*lodMeshes.front()->GetSubMeshes().front());
		auto shaderIdx = mesh->AddShader(*shader);
		for(auto &v : verts)
		(*mesh)->add_vertex(v);
		for(auto i=0;i<tris.size();i+=3)
		(*mesh)->add_triangle(tris[i],tris[i +1],tris[i +2],shaderIdx,true);


		auto *attrNormals = (*mesh)->attributes.find(ccl::ATTR_STD_VERTEX_NORMAL);
		auto *pnormals = attrNormals ? attrNormals->data_float4() : nullptr;

		auto *attrGenerated = (*mesh)->attributes.find(ccl::ATTR_STD_GENERATED);
		auto *pgenerated = attrGenerated ? attrGenerated->data_float4() : nullptr;

		auto *attrUv = (*mesh)->attributes.find(ccl::ATTR_STD_UV);
		auto *puvs = attrUv ? attrUv->data_float2() : nullptr;

		auto *attrUvTangent = (*mesh)->attributes.find(ccl::ATTR_STD_UV_TANGENT);
		auto *puvTangents = attrUvTangent ? attrUvTangent->data_float4() : nullptr;

		auto *attrUvTangentSign = (*mesh)->attributes.find(ccl::ATTR_STD_UV_TANGENT_SIGN);
		auto *puvTangentSign = attrUvTangentSign ? attrUvTangentSign->data_float() : nullptr;

		std::cout<<"Size of normals: "<<attrNormals->data_sizeof()<<std::endl;
		//std::cout<<"Size of generated: "<<attrGenerated->data_sizeof()<<std::endl;
		std::cout<<"Size of uvs: "<<attrUv->data_sizeof()<<std::endl;
		std::cout<<"Size of tangents: "<<attrUvTangent->data_sizeof()<<std::endl;
		std::cout<<"Size of tangent signs: "<<attrUvTangentSign->data_sizeof()<<std::endl;

		memcpy(pnormals,normals.data(),normals.size() *sizeof(normals.front()));
		//memcpy(pgenerated,generated.data(),generated.size() *sizeof(generated.front()));
		memcpy(puvs,uvs.data(),uvs.size() *sizeof(uvs.front()));
		//memcpy(puvTangents,uvTangents.data(),uvTangents.size() *sizeof(uvTangents.front()));
		//memcpy(puvTangentSign,uvTangentSigns.data(),uvTangentSigns.size() *sizeof(uvTangentSigns.front()));
		*/
	}
#endif

	// Create the object using the mesh
	physics::Transform t;
	ent.GetPose(t);
	auto o = Object::Create(*this,*mesh);
#ifndef ENABLE_TEST_AMBIENT_OCCLUSION
	o->SetPos(t.GetOrigin());
	o->SetRotation(t.GetRotation());
#endif
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

constexpr uint32_t OUTPUT_IMAGE_WIDTH = 1'024;
constexpr uint32_t OUTPUT_IMAGE_HEIGHT = 1'024;
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
			const float *uv = reinterpret_cast<const float*>(&uvs[i *3 +j]);

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

unsigned char unit_float_to_uchar_clamp(float val)
{
	return (unsigned char)((
		(val <= 0.0f) ? 0 : ((val > (1.0f - 0.5f / 255.0f)) ? 255 : ((255.0f * val) + 0.5f))));
}

typedef struct ImBuf {
	int x, y;
	std::vector<uint8_t> rect; // rgba
	std::vector<float> rect_float;
} ImBuf;

static int filter_make_index(const int x, const int y, const int w, const int h)
{
	if (x < 0 || x >= w || y < 0 || y >= h) {
		return -1; /* return bad index */
	}
	else {
		return y * w + x;
	}
}

static int check_pixel_assigned(
	const void *buffer, const char *mask, const int index, const int depth, const bool is_float)
{
	int res = 0;

	if (index >= 0) {
		const int alpha_index = depth * index + (depth - 1);

		if (mask != NULL) {
			res = mask[index] != 0 ? 1 : 0;
		}
		else if ((is_float && ((const float *)buffer)[alpha_index] != 0.0f) ||
			(!is_float && ((const unsigned char *)buffer)[alpha_index] != 0)) {
			res = 1;
		}
	}

	return res;
}

#define FILTER_MASK_MARGIN 1
#define FILTER_MASK_USED 2
static void IMB_filter_extend(struct ImBuf *ibuf, std::vector<uint8_t> &vmask, int filter)
{
	auto *mask = reinterpret_cast<char*>(vmask.data());

	const int width = ibuf->x;
	const int height = ibuf->y;
	const int depth = 4; /* always 4 channels */
	const int chsize = ibuf->rect_float.data() ? sizeof(float) : sizeof(unsigned char);
	const size_t bsize = ((size_t)width) * height * depth * chsize;
	const bool is_float = (ibuf->rect_float.data() != NULL);

	std::vector<uint8_t> vdstbuf;
	if(ibuf->rect_float.data())
	{
		vdstbuf.resize(ibuf->rect_float.size() *sizeof(ibuf->rect_float.front()));
		memcpy(vdstbuf.data(),ibuf->rect_float.data(),vdstbuf.size() *sizeof(vdstbuf.front()));
	}
	else
	{
		vdstbuf.resize(ibuf->rect.size() *sizeof(ibuf->rect.front()));
		memcpy(vdstbuf.data(),ibuf->rect.data(),vdstbuf.size() *sizeof(vdstbuf.front()));
	}

	void *dstbuf = vdstbuf.data();
	auto vdstmask = vmask;
	char *dstmask = reinterpret_cast<char*>(vdstmask.data());
	void *srcbuf = ibuf->rect_float.data() ? (void *)ibuf->rect_float.data() : (void *)ibuf->rect.data();
	char *srcmask = mask;
	int cannot_early_out = 1, r, n, k, i, j, c;
	float weight[25];

	/* build a weights buffer */
	n = 1;

#if 0
	k = 0;
	for (i = -n; i <= n; i++) {
		for (j = -n; j <= n; j++) {
			weight[k++] = sqrt((float)i * i + j * j);
		}
	}
#endif

	weight[0] = 1;
	weight[1] = 2;
	weight[2] = 1;
	weight[3] = 2;
	weight[4] = 0;
	weight[5] = 2;
	weight[6] = 1;
	weight[7] = 2;
	weight[8] = 1;

	/* run passes */
	for (r = 0; cannot_early_out == 1 && r < filter; r++) {
		int x, y;
		cannot_early_out = 0;

		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				const int index = filter_make_index(x, y, width, height);

				/* only update unassigned pixels */
				if (!check_pixel_assigned(srcbuf, srcmask, index, depth, is_float)) {
					float tmp[4];
					float wsum = 0;
					float acc[4] = {0, 0, 0, 0};
					k = 0;

					if (check_pixel_assigned(
						srcbuf, srcmask, filter_make_index(x - 1, y, width, height), depth, is_float) ||
						check_pixel_assigned(
							srcbuf, srcmask, filter_make_index(x + 1, y, width, height), depth, is_float) ||
						check_pixel_assigned(
							srcbuf, srcmask, filter_make_index(x, y - 1, width, height), depth, is_float) ||
						check_pixel_assigned(
							srcbuf, srcmask, filter_make_index(x, y + 1, width, height), depth, is_float)) {
						for (i = -n; i <= n; i++) {
							for (j = -n; j <= n; j++) {
								if (i != 0 || j != 0) {
									const int tmpindex = filter_make_index(x + i, y + j, width, height);

									if (check_pixel_assigned(srcbuf, srcmask, tmpindex, depth, is_float)) {
										if (is_float) {
											for (c = 0; c < depth; c++) {
												tmp[c] = ((const float *)srcbuf)[depth * tmpindex + c];
											}
										}
										else {
											for (c = 0; c < depth; c++) {
												tmp[c] = (float)((const unsigned char *)srcbuf)[depth * tmpindex + c];
											}
										}

										wsum += weight[k];

										for (c = 0; c < depth; c++) {
											acc[c] += weight[k] * tmp[c];
										}
									}
								}
								k++;
							}
						}

						if (wsum != 0) {
							for (c = 0; c < depth; c++) {
								acc[c] /= wsum;
							}

							if (is_float) {
								for (c = 0; c < depth; c++) {
									((float *)dstbuf)[depth * index + c] = acc[c];
								}
							}
							else {
								for (c = 0; c < depth; c++) {
									((unsigned char *)dstbuf)[depth * index + c] =
										acc[c] > 255 ? 255 : (acc[c] < 0 ? 0 : ((unsigned char)(acc[c] + 0.5f)));
								}
							}

							if (dstmask != NULL) {
								dstmask[index] = FILTER_MASK_MARGIN; /* assigned */
							}
							cannot_early_out = 1;
						}
					}
				}
			}
		}

		/* keep the original buffer up to date. */
		memcpy(srcbuf, dstbuf, bsize);
		if (dstmask != NULL) {
			memcpy(srcmask, dstmask, ((size_t)width) * height);
		}
	}
}

static void RE_bake_margin(ImBuf *ibuf, std::vector<uint8_t> &mask, const int margin)
{
	/* margin */
	IMB_filter_extend(ibuf, mask, margin);
}

static void RE_bake_mask_fill(const std::vector<BakePixel> pixel_array, const size_t num_pixels, char *mask)
{
	size_t i;
	if (!mask) {
		return;
	}

	/* only extend to pixels outside the mask area */
	for (i = 0; i < num_pixels; i++) {
		if (pixel_array[i].primitive_id != -1) {
			mask[i] = FILTER_MASK_USED;
		}
	}
}

#ifdef ENABLE_TEST_AMBIENT_OCCLUSION
#include <pragma/util/util_tga.hpp>
#endif
void cycles::Scene::Start()
{
	ccl::BufferParams bufferParams {};
	bufferParams.width = m_scene.camera->width;
	bufferParams.height = m_scene.camera->height;
	bufferParams.full_width = m_scene.camera->width;
	bufferParams.full_height = m_scene.camera->height;

	m_session->scene = &m_scene;
#ifndef ENABLE_TEST_AMBIENT_OCCLUSION
	m_session->reset(bufferParams,m_session->params.samples);

	m_camera->Finalize();
#endif
	// Note: Lights and objects have to be initialized before shaders, because they may
	// create additional shaders.
	for(auto &light : m_lights)
		light->Finalize();
	for(auto &o : m_objects)
		o->Finalize();
	for(auto &shader : m_shaders)
		shader->Finalize();
#ifndef ENABLE_TEST_AMBIENT_OCCLUSION
	m_session->start();
#else
	{
		m_scene.bake_manager->set_baking(true);
		m_session->load_kernels();

		switch(m_renderMode)
		{
		case RenderMode::BakeAmbientOcclusion:
			ccl::Pass::add(ccl::PASS_LIGHT,m_scene.film->passes);
			break;
		case RenderMode::BakeNormals:
			break;
		}

		// ccl::Pass::add(ccl::PASS_UV,m_scene.film->passes);
		//ccl::Pass::add(ccl::PASS_NORMAL,m_scene.film->passes);

		m_scene.film->tag_update(&m_scene);
		m_scene.integrator->tag_update(&m_scene);

		{
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
		}

		{
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
		m_scene.bake_manager->set_shader_limit(256,256);
		m_session->tile_manager.set_samples(m_session->params.samples);

		bufferParams.width = OUTPUT_IMAGE_WIDTH;
		bufferParams.height = OUTPUT_IMAGE_HEIGHT;
		bufferParams.full_width = OUTPUT_IMAGE_WIDTH;
		bufferParams.full_height = OUTPUT_IMAGE_HEIGHT;

#ifdef ENABLE_TEST_AMBIENT_OCCLUSION
		/*bufferParams.width = 960;
		bufferParams.height = 540;
		bufferParams.full_x = 0;
		bufferParams.full_y = 0;
		bufferParams.full_width = 960;
		bufferParams.full_height = 540;*/
#endif
		m_session->reset(bufferParams, m_session->params.samples);

		//auto shaderType = ccl::ShaderEvalType::SHADER_EVAL_DIFFUSE;
		ccl::ShaderEvalType shaderType;
		int bake_pass_filter;
		switch(m_renderMode)
		{
		case RenderMode::BakeAmbientOcclusion:
			shaderType = ccl::ShaderEvalType::SHADER_EVAL_AO;
			bake_pass_filter = ccl::BAKE_FILTER_AO;
			break;
		case RenderMode::BakeNormals:
			shaderType = ccl::ShaderEvalType::SHADER_EVAL_NORMAL;
			bake_pass_filter = 0;
			break;
		}

		//int bake_pass_filter =  ccl::BAKE_FILTER_DIFFUSE | ccl::BAKE_FILTER_DIRECT | ccl::BAKE_FILTER_INDIRECT | ccl::BAKE_FILTER_COLOR;
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

		//numPixels = 1024 *1024;
		std::vector<float> result;
		result.resize(numPixels *4,0.f);
		auto r = m_scene.bake_manager->bake(m_scene.device,&m_scene.dscene,&m_scene,m_session->progress,shaderType,bake_pass_filter,bake_data,result.data());

		/*{
		std::ifstream f("E:/projects/pragma/build_winx64/output/ccl_float_data.bin", std::ios::binary);

		f.seekg( 0, std::ios::end );
		auto size = f.tellg();
		f.seekg( 0 );

		f.read(reinterpret_cast<char*>(result.data()),size);
		f.close();
		}*/

		std::vector<uint8_t> pixels {};
		pixels.resize(numPixels *4);
		for(auto i=decltype(numPixels){0u};i<numPixels;++i)
		{
			auto *inData = result.data() +i *4;
			auto *outData = pixels.data() +i *4;
			for(uint8_t j=0;j<4;++j)
				outData[j] = unit_float_to_uchar_clamp(inData[j]);//static_cast<uint8_t>(umath::clamp(inData[j] *255.f,0.f,255.f));
		}

		std::vector<uint8_t> mask_buffer {};
		mask_buffer.resize(numPixels);
		constexpr auto margin = 16u;

		ImBuf ibuf {};
		ibuf.x = OUTPUT_IMAGE_WIDTH;
		ibuf.y = OUTPUT_IMAGE_HEIGHT;
		ibuf.rect = pixels;


		RE_bake_mask_fill(pixelArray, numPixels, reinterpret_cast<char*>(mask_buffer.data()));
		RE_bake_margin(&ibuf, mask_buffer, margin);

		std::vector<uint8_t> rgbPixels;
		rgbPixels.resize(numPixels *3);
		for(auto i=decltype(numPixels){0u};i<numPixels;++i)
		{
			auto srcOffset = i *4;
			auto dstOffset = i *3;
			rgbPixels.at(dstOffset) = ibuf.rect.at(srcOffset);
			rgbPixels.at(dstOffset +1) = ibuf.rect.at(srcOffset +1);
			rgbPixels.at(dstOffset +2) = ibuf.rect.at(srcOffset +2);
		}
		util::tga::write_tga("test_ao.tga",OUTPUT_IMAGE_WIDTH,OUTPUT_IMAGE_HEIGHT,rgbPixels);

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
#endif
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
