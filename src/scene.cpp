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
#include <render/particles.h>
#include <util/util_path.h>
#ifdef ENABLE_CYCLES_LOGGING
	#define GLOG_NO_ABBREVIATED_SEVERITIES
	#include <util/util_logging.h>
	#include <glog/logging.h>
#endif
#include <optional>
#include <pragma/c_engine.h>
#include <pragma/util/util_game.hpp>
#include <prosper_context.hpp>
#include <buffers/prosper_uniform_resizable_buffer.hpp>
#include <pragma/clientstate/clientstate.h>
#include <pragma/game/game_resources.hpp>
#include <pragma/model/model.h>
#include <pragma/model/modelmesh.h>
#include <pragma/entities/baseentity.h>
#include <pragma/entities/entity_component_system_t.hpp>
#include <pragma/entities/components/c_animated_component.hpp>
#include <pragma/entities/components/c_eye_component.hpp>
#include <pragma/entities/components/c_vertex_animated_component.hpp>
#include <pragma/entities/components/c_light_map_component.hpp>
#include <pragma/entities/components/c_render_component.hpp>
#include <pragma/entities/components/c_model_component.hpp>
#include <pragma/entities/c_skybox.h>
#include <pragma/rendering/shaders/c_shader_cubemap_to_equirectangular.hpp>
#include <pragma/rendering/shaders/particles/c_shader_particle.hpp>
#include <sharedutils/util_file.h>
#include <sharedutils/util.h>
#include <util_image_buffer.hpp>
#include <util_texture_info.hpp>
#include <datasystem_color.h>
#include <datasystem_vector.h>
#include <texturemanager/texture.h>
#include <cmaterialmanager.h>
#include <buffers/prosper_dynamic_resizable_buffer.hpp>

#ifdef ENABLE_CYCLES_LOGGING
#pragma comment(lib,"shlwapi.lib")
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// ccl happens to have the same include guard name as sharedutils, so we have to undef it here
#undef __UTIL_STRING_H__
#include <sharedutils/util_string.h>

using namespace pragma::modules;

#pragma optimize("",off)

extern DLLCENGINE CEngine *c_engine;
extern DLLCLIENT ClientState *client;
extern DLLCLIENT CGame *c_game;

cycles::SceneWorker::SceneWorker(Scene &scene)
	: util::ParallelWorker<std::shared_ptr<uimg::ImageBuffer>>{},m_scene{scene.shared_from_this()}
{}
void cycles::SceneWorker::Cancel(const std::string &resultMsg)
{
	util::ParallelWorker<std::shared_ptr<uimg::ImageBuffer>>::Cancel(resultMsg);
	m_scene->OnParallelWorkerCancelled();
}
void cycles::SceneWorker::Wait()
{
	util::ParallelWorker<std::shared_ptr<uimg::ImageBuffer>>::Wait();
	m_scene->Wait();
}
std::shared_ptr<uimg::ImageBuffer> cycles::SceneWorker::GetResult() {return m_scene->m_resultImageBuffer;}

///////////////////

void cycles::Scene::ApplyPostProcessing(uimg::ImageBuffer &imgBuffer,cycles::Scene::RenderMode renderMode)
{
	// For some reason the image is flipped horizontally when rendering an image,
	// so we'll just flip it the right way here
	auto flipHorizontally = IsRenderSceneMode(renderMode);
	if(m_scene.camera->type == ccl::CameraType::CAMERA_PANORAMA)
	{
		switch(m_scene.camera->panorama_type)
		{
		case ccl::PanoramaType::PANORAMA_EQUIRECTANGULAR:
		case ccl::PanoramaType::PANORAMA_FISHEYE_EQUIDISTANT:
			flipHorizontally = false; // I have no idea why some types have to be flipped and others don't
			break;
		}
	}
	if(flipHorizontally)
		imgBuffer.FlipHorizontally();

	// We will also always have to flip the image vertically, since the data seems to be bottom->top and we need it top->bottom
	imgBuffer.FlipVertically();
	imgBuffer.ClearAlpha();
}

bool cycles::Scene::IsRenderSceneMode(RenderMode renderMode)
{
	switch(renderMode)
	{
	case RenderMode::RenderImage:
	case RenderMode::SceneAlbedo:
	case RenderMode::SceneNormals:
		return true;
	}
	return false;
}

static void init_cycles()
{
	static auto isInitialized = false;
	if(isInitialized)
		return;
	isInitialized = true;

	auto kernelPath = util::get_program_path() +"/modules/cycles";

	ccl::path_init(kernelPath,kernelPath);

	putenv(("CYCLES_KERNEL_PATH=" +kernelPath).c_str());
	putenv(("CYCLES_SHADER_PATH=" +kernelPath).c_str());
#ifdef ENABLE_CYCLES_LOGGING
	google::SetLogDestination(google::GLOG_INFO,(kernelPath +"/log/info.log").c_str());
	google::SetLogDestination(google::GLOG_WARNING,(kernelPath +"/log/warning.log").c_str());
	FLAGS_log_dir = kernelPath +"/log";

	ccl::util_logging_init(engine_info::get_name().c_str());
	ccl::util_logging_verbosity_set(2);
	ccl::util_logging_start();
	FLAGS_logtostderr = false;
	FLAGS_alsologtostderr = true; // Doesn't seem to work properly?

	/* // Test output
	google::LogAtLevel(google::GLOG_INFO,"Info test");
	google::LogAtLevel(google::GLOG_WARNING,"Warning test");
	google::FlushLogFiles(google::GLOG_INFO);
	google::FlushLogFiles(google::GLOG_WARNING);*/
#endif
}

static bool is_device_type_available(ccl::DeviceType type)
{
	using namespace ccl;
	return ccl::Device::available_devices(DEVICE_MASK(type)).empty() == false;
}
std::shared_ptr<cycles::Scene> cycles::Scene::Create(RenderMode renderMode,const CreateInfo &createInfo)
{
	init_cycles();

	auto cclDeviceType = ccl::DeviceType::DEVICE_CPU;
	switch(createInfo.deviceType)
	{
	case DeviceType::GPU:
	{
		if(is_device_type_available(ccl::DeviceType::DEVICE_CUDA))
		{
			cclDeviceType = ccl::DeviceType::DEVICE_CUDA;
			break;
		}
		if(is_device_type_available(ccl::DeviceType::DEVICE_OPENCL))
		{
			// Note: In some cases Cycles has to rebuild OpenCL shaders, but by default Cycles tries to do so using Blender's python implementation.
			// Since this isn't Blender, Cycles will create several instances of Pragma and the process will get stuck.
			// To fix the issue, a change in the Cycles library is required: OpenCLDevice::OpenCLProgram::compile_separate has
			// to always return false! This will make it fall back to an internal build function that doesn't require Blender / Python.
			cclDeviceType = ccl::DeviceType::DEVICE_OPENCL;
			break;
		}
		// No break is intended!
	}
	case DeviceType::CPU:
		cclDeviceType = ccl::DeviceType::DEVICE_CPU;
		break;
	}
	static_assert(umath::to_integral(DeviceType::Count) == 2);

	std::optional<ccl::DeviceInfo> device = {};
	for(auto &devInfo : ccl::Device::available_devices(ccl::DeviceTypeMask::DEVICE_MASK_CUDA | ccl::DeviceTypeMask::DEVICE_MASK_OPENCL | ccl::DeviceTypeMask::DEVICE_MASK_CPU))
	{
		if(devInfo.type == cclDeviceType)
		{
			device = devInfo;
			break;
		}
		if(devInfo.type == ccl::DeviceType::DEVICE_CPU)
			device = devInfo; // Fallback / Default device type
	}

	if(device.has_value() == false)
		return nullptr; // No device available

	auto deviceType = createInfo.deviceType;
	if(device->type == ccl::DeviceType::DEVICE_CPU)
		deviceType = DeviceType::CPU;

	ccl::SessionParams sessionParams {};
	sessionParams.shadingsystem = ccl::SHADINGSYSTEM_SVM;
	sessionParams.device = *device;
	sessionParams.progressive = true; // TODO: This should be set to false, but doing so causes a crash during rendering
	sessionParams.background = true;
	sessionParams.progressive_refine = false;
	sessionParams.display_buffer_linear = createInfo.hdrOutput;

	switch(deviceType)
	{
	case DeviceType::GPU:
		sessionParams.tile_size = {256,256};
		break;
	default:
		sessionParams.tile_size = {16,16};
		break;
	}
	sessionParams.tile_order = ccl::TileOrder::TILE_HILBERT_SPIRAL;
	
	if(createInfo.denoise && renderMode == RenderMode::RenderImage)
	{
		sessionParams.full_denoising = true;
		sessionParams.run_denoising = true;
	}
	sessionParams.start_resolution = 64;
	if(createInfo.samples.has_value())
		sessionParams.samples = *createInfo.samples;
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

	if(IsRenderSceneMode(renderMode))
	{
		// We need to define a write callback, otherwise the session's display object will not be initialized.
		sessionParams.write_render_cb = [](const ccl::uchar *pixels,int w,int h,int channels) -> bool {return true;};
	}

#ifdef ENABLE_TEST_AMBIENT_OCCLUSION
	if(IsRenderSceneMode(renderMode) == false)
	{
		//sessionParams.background = true;
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

	ccl::SceneParams sceneParams {};
	sceneParams.shadingsystem = ccl::SHADINGSYSTEM_SVM;

	auto *cclScene = new ccl::Scene{sceneParams,session->device}; // Object will be removed automatically by cycles
	cclScene->params.bvh_type = ccl::SceneParams::BVH_STATIC;

	auto *pSession = session.get();
	auto scene = std::shared_ptr<Scene>{new Scene{std::move(session),*cclScene,renderMode,deviceType}};

	scene->m_camera = Camera::Create(*scene);
	umath::set_flag(scene->m_stateFlags,StateFlags::OutputResultWithHDRColors,createInfo.hdrOutput);
	umath::set_flag(scene->m_stateFlags,StateFlags::DenoiseResult,createInfo.denoise);
	return scene;
}

cycles::Scene::Scene(std::unique_ptr<ccl::Session> session,ccl::Scene &scene,RenderMode renderMode,DeviceType deviceType)
	: m_session{std::move(session)},m_scene{scene},m_renderMode{renderMode},m_deviceType{deviceType}
{}

cycles::Scene::~Scene()
{
	Wait();
	FinalizeAndCloseCyclesScene();
}

std::shared_ptr<uimg::ImageBuffer> cycles::Scene::FinalizeCyclesScene()
{
	// Note: We want the HDR output values from cycles which haven't been tonemapped yet, but Cycles
	// makes this impossible to do, so we'll have to use this work-around.
	class SessionWrapper
		: ccl::Session
	{
	public:
		void Finalize(bool hdr)
		{
			// This part is the same code as the respective part in Session::~Session()
			if (session_thread) {
				/* wait for session thread to end */
				progress.set_cancel("Exiting");

				gpu_need_display_buffer_update = false;
				gpu_need_display_buffer_update_cond.notify_all();

				{
					ccl::thread_scoped_lock pause_lock(pause_mutex);
					pause = false;
				}
				pause_cond.notify_all();

				wait();
			}
			//

			/* tonemap and write out image if requested */
			delete display;

			display = new ccl::DisplayBuffer(device, hdr);
			display->reset(buffers->params);
			copy_to_display_buffer(params.samples);
		}
	};
	auto &session = reinterpret_cast<SessionWrapper&>(*m_session);
	auto outputWithHDR = umath::is_flag_set(m_stateFlags,StateFlags::OutputResultWithHDRColors);
	session.Finalize(outputWithHDR);

	auto w = m_session->display->draw_width;
	auto h = m_session->display->draw_height;
	std::shared_ptr<uimg::ImageBuffer> imgBuffer = nullptr;
	if(outputWithHDR)
	{
		auto *pixels = m_session->display->rgba_half.copy_from_device(0, w, h);
		imgBuffer = uimg::ImageBuffer::Create(pixels,w,h,uimg::ImageBuffer::Format::RGBA_HDR,false);
	}
	else
	{
		auto *pixels = m_session->display->rgba_byte.copy_from_device(0, w, h);
		imgBuffer = uimg::ImageBuffer::Create(pixels,w,h,uimg::ImageBuffer::Format::RGBA_LDR,false);
	}
	return imgBuffer;
}

void cycles::Scene::FinalizeAndCloseCyclesScene()
{
	if(m_session && IsRenderSceneMode(m_renderMode))
		m_resultImageBuffer = FinalizeCyclesScene();
	CloseCyclesScene();
}

void cycles::Scene::CloseCyclesScene()
{
	m_objects.clear();
	m_shaders.clear();
	m_camera = nullptr;
	
	if(m_session == nullptr)
		return;
	m_session = nullptr;
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

enum class PreparedTextureInputFlags : uint8_t
{
	None = 0u,
	CanBeEnvMap = 1u
};
REGISTER_BASIC_BITWISE_OPERATORS(PreparedTextureInputFlags)
enum class PreparedTextureOutputFlags : uint8_t
{
	None = 0u,
	Envmap = 1u
};
REGISTER_BASIC_BITWISE_OPERATORS(PreparedTextureOutputFlags)

static std::optional<std::string> prepare_texture(
	TextureInfo *texInfo,bool &outSuccess,bool &outConverted,PreparedTextureInputFlags inFlags,PreparedTextureOutputFlags *optOutFlags=nullptr,
	const std::optional<std::string> &defaultTexture={}
)
{
	if(optOutFlags)
		*optOutFlags = PreparedTextureOutputFlags::None;

	outSuccess = false;
	outConverted = false;
	if(texInfo == nullptr)
		return {};
	auto tex = texInfo ? std::static_pointer_cast<Texture>(texInfo->texture) : nullptr;
	std::string texName {};
	// Make sure texture has been fully loaded!
	if(tex == nullptr || tex->IsLoaded() == false)
	{
		tex = nullptr;
		if(defaultTexture.has_value())
		{
			TextureManager::LoadInfo loadInfo {};
			loadInfo.flags = TextureLoadFlags::LoadInstantly;
			std::shared_ptr<void> ptrTex;
			if(static_cast<CMaterialManager&>(client->GetMaterialManager()).GetTextureManager().Load(c_engine->GetRenderContext(),*defaultTexture,loadInfo,&ptrTex))
			{
				texName = *defaultTexture;
				tex = std::static_pointer_cast<Texture>(ptrTex);
				if(tex->IsLoaded() == false)
					tex = nullptr;
			}
		}
	}
	else
		texName = texInfo->name;
	if(tex == nullptr || tex->IsError() || tex->HasValidVkTexture() == false)
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
	ufile::remove_extension_from_filename(texName); // DDS-writer will add the extension for us

	auto vkTex = tex->GetVkTexture();
	auto *img = &vkTex->GetImage();
	auto isCubemap = img->IsCubemap();
	if(isCubemap)
	{
		if(umath::is_flag_set(inFlags,PreparedTextureInputFlags::CanBeEnvMap) == false)
			return {};
		// Image is a cubemap, which Cycles doesn't support! We'll have to convert it to a equirectangular image and use that instead.
		auto &shader = static_cast<pragma::ShaderCubemapToEquirectangular&>(*c_engine->GetShader("cubemap_to_equirectangular"));
		auto equiRectMap = shader.CubemapToEquirectangularTexture(*vkTex);
		vkTex = equiRectMap;
		img = &vkTex->GetImage();
		texName += "_equirect";

		if(optOutFlags)
			*optOutFlags |= PreparedTextureOutputFlags::Envmap;
	}

	auto texPath = "materials\\" +texName;
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

	// Output path for the DDS-file we're about to create
	auto ddsPath = "addons/converted/materials/" +texName;
	uimg::TextureInfo imgWriteInfo {};
	imgWriteInfo.containerFormat = uimg::TextureInfo::ContainerFormat::DDS; // Cycles doesn't support KTX
	if(tex->HasFlag(Texture::Flags::SRGB))
		imgWriteInfo.flags |= uimg::TextureInfo::Flags::SRGB;

	// Try to determine appropriate formats
	if(tex->HasFlag(Texture::Flags::NormalMap))
	{
		imgWriteInfo.inputFormat = uimg::TextureInfo::InputFormat::R32G32B32A32_Float;
		imgWriteInfo.SetNormalMap();
	}
	else
	{
		auto format = img->GetFormat();
		if(prosper::util::is_16bit_format(format))
		{
			imgWriteInfo.inputFormat = uimg::TextureInfo::InputFormat::R16G16B16A16_Float;
			imgWriteInfo.outputFormat = uimg::TextureInfo::OutputFormat::HDRColorMap;
		}
		else if(prosper::util::is_32bit_format(format) || prosper::util::is_64bit_format(format))
		{
			imgWriteInfo.inputFormat = uimg::TextureInfo::InputFormat::R32G32B32A32_Float;
			imgWriteInfo.outputFormat = uimg::TextureInfo::OutputFormat::HDRColorMap;
		}
		else
		{
			imgWriteInfo.inputFormat = uimg::TextureInfo::InputFormat::R8G8B8A8_UInt;
			// TODO: Check the alpha channel values to determine whether we actually need a full alpha channel?
			imgWriteInfo.outputFormat = uimg::TextureInfo::OutputFormat::ColorMapSmoothAlpha;
		}
		switch(format)
		{
		case prosper::Format::BC1_RGBA_SRGB_Block:
		case prosper::Format::BC1_RGBA_UNorm_Block:
		case prosper::Format::BC1_RGB_SRGB_Block:
		case prosper::Format::BC1_RGB_UNorm_Block:
			imgWriteInfo.outputFormat = uimg::TextureInfo::OutputFormat::BC1;
			break;
		case prosper::Format::BC2_SRGB_Block:
		case prosper::Format::BC2_UNorm_Block:
			imgWriteInfo.outputFormat = uimg::TextureInfo::OutputFormat::BC2;
			break;
		case prosper::Format::BC3_SRGB_Block:
		case prosper::Format::BC3_UNorm_Block:
			imgWriteInfo.outputFormat = uimg::TextureInfo::OutputFormat::BC3;
			break;
		case prosper::Format::BC4_SNorm_Block:
		case prosper::Format::BC4_UNorm_Block:
			imgWriteInfo.outputFormat = uimg::TextureInfo::OutputFormat::BC4;
			break;
		case prosper::Format::BC5_SNorm_Block:
		case prosper::Format::BC5_UNorm_Block:
			imgWriteInfo.outputFormat = uimg::TextureInfo::OutputFormat::BC5;
			break;
		case prosper::Format::BC6H_SFloat_Block:
		case prosper::Format::BC6H_UFloat_Block:
			// TODO: As of 20-03-26, Cycles (/oiio) does not have support for BC6, so we'll
			// fall back to a different format
			imgWriteInfo.inputFormat = uimg::TextureInfo::InputFormat::R16G16B16A16_Float;
			// imgWriteInfo.outputFormat = uimg::TextureInfo::OutputFormat::BC6;
			imgWriteInfo.outputFormat = uimg::TextureInfo::OutputFormat::DXT5;
			break;
		case prosper::Format::BC7_SRGB_Block:
		case prosper::Format::BC7_UNorm_Block:
			// TODO: As of 20-03-26, Cycles (/oiio) does not have support for BC7, so we'll
			// fall back to a different format
			imgWriteInfo.inputFormat = uimg::TextureInfo::InputFormat::R16G16B16A16_Float;
			// imgWriteInfo.outputFormat = uimg::TextureInfo::OutputFormat::BC7;
			imgWriteInfo.outputFormat = uimg::TextureInfo::OutputFormat::DXT1;
			break;
		}
	}
	absPath = "";
	// Save the DDS image and make sure the file exists
	if(c_game->SaveImage(*img,ddsPath,imgWriteInfo) && FileManager::FindAbsolutePath(ddsPath +".dds",absPath))
	{
		outSuccess = true;
		outConverted = true;
		return absPath;
	}
	// Something went wrong, fall back to error texture!
	return get_abs_error_texture_path();
}

static std::optional<std::string> prepare_texture(
	ccl::Scene &scene,TextureInfo *texInfo,PreparedTextureInputFlags inFlags,PreparedTextureOutputFlags *optOutFlags=nullptr,
	const std::optional<std::string> &defaultTexture={}
)
{
	if(optOutFlags)
		*optOutFlags = PreparedTextureOutputFlags::None;
	if(texInfo == nullptr)
		return {};
	auto success = false;
	auto converted = false;
	auto result = prepare_texture(texInfo,success,converted,inFlags,optOutFlags,defaultTexture);
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

void cycles::Scene::AddRoughnessMapImageTextureNode(ShaderModuleRoughness &shader,Material &mat,float defaultRoughness) const
{
	// If no roughness map is available, just use roughness or specular factor directly
	auto roughnessFactor = defaultRoughness;
	auto hasRoughnessFactor = mat.GetDataBlock()->GetFloat("roughness_factor",&roughnessFactor);

	float specularFactor;
	if(mat.GetDataBlock()->GetFloat("specular_factor",&specularFactor))
	{
		if(hasRoughnessFactor == false)
			roughnessFactor = 1.f;
		roughnessFactor *= (1.f -specularFactor);
		hasRoughnessFactor = true;
	}
	shader.SetRoughnessFactor(roughnessFactor);

	auto rmaPath = prepare_texture(m_scene,mat.GetRMAMap(),PreparedTextureInputFlags::None,nullptr,"white");
	if(rmaPath.has_value() == false)
	{
#if 0
		// No roughness map available; Attempt to use specular map instead
		auto specularPath = prepare_texture(m_scene,mat.GetSpecularMap(),PreparedTextureInputFlags::None);
		if(specularPath.has_value())
		{
			shader.SetSpecularMap(*specularPath);
			if(hasRoughnessFactor == false)
				shader.SetRoughnessFactor(1.f);
		}
#endif
	}
	else
	{
		shader.SetRoughnessMap(*rmaPath);
		if(hasRoughnessFactor == false)
			shader.SetRoughnessFactor(1.f);
	}
}

Material *cycles::Scene::GetMaterial(BaseEntity &ent,ModelSubMesh &subMesh,uint32_t skinId) const
{
	auto mdlC = ent.GetModelComponent();
	return mdlC.valid() ? GetMaterial(static_cast<pragma::CModelComponent&>(*mdlC),subMesh,skinId) : nullptr;
}

Material *cycles::Scene::GetMaterial(Model &mdl,ModelSubMesh &subMesh,uint32_t skinId) const
{
	auto texIdx = mdl.GetMaterialIndex(subMesh,skinId);
	return texIdx.has_value() ? mdl.GetMaterial(*texIdx) : nullptr;
}

Material *cycles::Scene::GetMaterial(pragma::CModelComponent &mdlC,ModelSubMesh &subMesh,uint32_t skinId) const
{
	auto mdl = mdlC.GetModel();
	if(mdl == nullptr)
		return nullptr;
	auto texIdx = mdl->GetMaterialIndex(subMesh,skinId);
	return texIdx.has_value() ? mdlC.GetRenderMaterial(*texIdx) : nullptr;
}

cycles::PShader cycles::Scene::CreateShader(Material &mat,const std::string &meshName,const ShaderInfo &shaderInfo)
{
	// Make sure all textures have finished loading
	static_cast<CMaterialManager&>(client->GetMaterialManager()).GetTextureManager().WaitForTextures();

	TextureInfo *diffuseMap = nullptr;
	if(ustring::compare(mat.GetShaderIdentifier(),"skybox",false))
		diffuseMap = mat.GetTextureInfo("skybox");
	else
		diffuseMap = mat.GetDiffuseMap();
	PreparedTextureOutputFlags flags;
	auto diffuseTexPath = prepare_texture(m_scene,diffuseMap,PreparedTextureInputFlags::CanBeEnvMap,&flags);
	if(diffuseTexPath.has_value() == false)
		return nullptr;

	std::optional<std::string> albedo2TexPath = {};
	if(ustring::compare(mat.GetShaderIdentifier(),"pbr_blend",false))
	{
		auto *albedo2Map = mat.GetTextureInfo(Material::ALBEDO_MAP2_IDENTIFIER);
		if(albedo2Map)
			albedo2TexPath = prepare_texture(m_scene,albedo2Map,PreparedTextureInputFlags::None);
	}

	enum class ShaderType : uint8_t
	{
		Disney = 0u,
		Toon
	};
	static auto shaderType = ShaderType::Disney;

	// TODO: Only allow toon shader when baking diffuse lighting
	const std::string bsdfName = "bsdf_scene";

	if(umath::is_flag_set(flags,PreparedTextureOutputFlags::Envmap))
		return nullptr;

	/*if(true)
	{
	// TODO: Hair shader requires meshes with curves
	auto nodeBsdf = shader->AddNode("principled_hair_bsdf",bsdfName);
	auto *pNodeBsdf = static_cast<ccl::PrincipledHairBsdfNode*>(**nodeBsdf);

	// Default settings (Taken from Blender)
	pNodeBsdf->roughness = 0.3f;
	pNodeBsdf->radial_roughness = 0.3f;
	pNodeBsdf->coat = 100.f;
	pNodeBsdf->ior = 1.55f;
	pNodeBsdf->offset = 2.f;
	pNodeBsdf->random_roughness = 0.f;

	auto hasAlbedoNode = umath::is_flag_set(flags,PreparedTextureOutputFlags::Envmap) ?
	(AssignEnvironmentTexture(*shader,"albedo",*diffuseTexPath) != nullptr) :
	(AssignTexture(*shader,"albedo",*diffuseTexPath) != nullptr);
	if(hasAlbedoNode)
	shader->Link("albedo","color","output","surface");

	// Roughness map
	//LinkRoughnessMap(*shader,*mat,bsdfName);

	// Normal map
	//LinkNormalMap(*shader,*mat,meshName,bsdfName,"normal");
	return shader;
	}
	*/

	auto fApplyColorFactor = [&mat](ShaderAlbedoSet &albedoSet) {
		auto &colorFactor = mat.GetDataBlock()->GetValue("color_factor");
		if(colorFactor == nullptr || typeid(*colorFactor) != typeid(ds::Vector4))
			return;
		auto &color = static_cast<ds::Vector4*>(colorFactor.get())->GetValue();
		albedoSet.SetColorFactor(color);
	};

	cycles::PShader resShader = nullptr;
	if(m_renderMode == RenderMode::SceneAlbedo)
	{
		auto shader = cycles::Shader::Create<ShaderAlbedo>(*this,meshName +"_shader");
		shader->SetMeshName(meshName);
		shader->GetAlbedoSet().SetAlbedoMap(*diffuseTexPath);
		if(albedo2TexPath.has_value())
		{
			shader->GetAlbedoSet2().SetAlbedoMap(*albedo2TexPath);
			shader->SetUseVertexAlphasForBlending(true);
		}
		shader->SetAlphaMode(mat.GetAlphaMode(),mat.GetAlphaCutoff());
		resShader = shader;
	}
	else if(m_renderMode == RenderMode::SceneNormals)
	{
		auto shader = cycles::Shader::Create<ShaderNormal>(*this,meshName +"_shader");
		shader->SetMeshName(meshName);
		shader->GetAlbedoSet().SetAlbedoMap(*diffuseTexPath);
		if(albedo2TexPath.has_value())
		{
			shader->GetAlbedoSet2().SetAlbedoMap(*albedo2TexPath);
			shader->SetUseVertexAlphasForBlending(true);
		}
		shader->SetAlphaMode(mat.GetAlphaMode(),mat.GetAlphaCutoff());
		auto normalTexPath = prepare_texture(m_scene,mat.GetNormalMap(),PreparedTextureInputFlags::None);
		if(normalTexPath.has_value())
			shader->SetNormalMap(*normalTexPath);
		resShader = shader;
	}
	else
	{
		if(shaderType == ShaderType::Toon)
		{
			auto shader = cycles::Shader::Create<ShaderToon>(*this,meshName +"_shader");
			fApplyColorFactor(shader->GetAlbedoSet());
			shader->SetMeshName(meshName);
			shader->GetAlbedoSet().SetAlbedoMap(*diffuseTexPath);
			if(albedo2TexPath.has_value())
			{
				shader->GetAlbedoSet2().SetAlbedoMap(*albedo2TexPath);
				shader->SetUseVertexAlphasForBlending(true);
			}
			shader->SetAlphaMode(mat.GetAlphaMode(),mat.GetAlphaCutoff());
			auto normalTexPath = prepare_texture(m_scene,mat.GetNormalMap(),PreparedTextureInputFlags::None);
			if(normalTexPath.has_value())
				shader->SetNormalMap(*normalTexPath);
			resShader = shader;
		}
		else if(ustring::compare(mat.GetShaderIdentifier(),"glass",false))
		{
			auto shader = cycles::Shader::Create<ShaderGlass>(*this,meshName +"_shader");
			fApplyColorFactor(shader->GetAlbedoSet());
			shader->SetMeshName(meshName);
			shader->GetAlbedoSet().SetAlbedoMap(*diffuseTexPath);
			if(albedo2TexPath.has_value())
			{
				shader->GetAlbedoSet2().SetAlbedoMap(*albedo2TexPath);
				shader->SetUseVertexAlphasForBlending(true);
			}
			auto normalTexPath = prepare_texture(m_scene,mat.GetNormalMap(),PreparedTextureInputFlags::None);
			if(normalTexPath.has_value())
				shader->SetNormalMap(*normalTexPath);

			// Roughness map
			AddRoughnessMapImageTextureNode(*shader,mat,0.5f);

			resShader = shader;
		}
		else
		{
			std::shared_ptr<cycles::ShaderPBR> shader = nullptr;
			auto isParticleSystemShader = (shaderInfo.particleSystem.has_value() && shaderInfo.particle.has_value());
			if(isParticleSystemShader)
			{
				shader = cycles::Shader::Create<ShaderParticle>(*this,meshName +"_shader");
				auto *ptShader = static_cast<ShaderParticle*>(shader.get());
				auto *pt = static_cast<const pragma::CParticleSystemComponent::ParticleData*>(*shaderInfo.particle);
				Color color {static_cast<int16_t>(pt->color.at(0)),static_cast<int16_t>(pt->color.at(1)),static_cast<int16_t>(pt->color.at(2)),static_cast<int16_t>(pt->color.at(3))};
				ptShader->SetColor(color);

				auto *pShader = static_cast<pragma::ShaderParticle*>(c_engine->GetShader("particle").get());
				if(pShader)
					static_cast<ShaderParticle*>(shader.get())->SetRenderFlags(umath::to_integral(pShader->GetRenderFlags(**shaderInfo.particleSystem)));
			}
			else
				shader = cycles::Shader::Create<ShaderPBR>(*this,meshName +"_shader");
			fApplyColorFactor(shader->GetAlbedoSet());
			shader->SetMeshName(meshName);

			auto data = mat.GetDataBlock();
			auto dataSSS = data->GetBlock("subsurface_scattering");
			if(dataSSS)
			{
				float subsurface;
				if(dataSSS->GetFloat("factor",&subsurface))
					shader->SetSubsurface(subsurface);

				auto &dvSubsurfaceColor = dataSSS->GetValue("color");
				if(dvSubsurfaceColor != nullptr && typeid(*dvSubsurfaceColor) == typeid(ds::Color))
				{
					auto &color = static_cast<ds::Color&>(*dvSubsurfaceColor).GetValue();
					shader->SetSubsurfaceColor(color.ToVector3());
				}

				const std::unordered_map<SurfaceMaterial::PBRInfo::SubsurfaceMethod,ccl::ClosureType> bssrdfToCclType = {
					{SurfaceMaterial::PBRInfo::SubsurfaceMethod::Cubic,ccl::ClosureType::CLOSURE_BSSRDF_CUBIC_ID},
				{SurfaceMaterial::PBRInfo::SubsurfaceMethod::Gaussian,ccl::ClosureType::CLOSURE_BSSRDF_GAUSSIAN_ID},
				{SurfaceMaterial::PBRInfo::SubsurfaceMethod::Principled,ccl::ClosureType::CLOSURE_BSDF_BSSRDF_PRINCIPLED_ID},
				{SurfaceMaterial::PBRInfo::SubsurfaceMethod::Burley,ccl::ClosureType::CLOSURE_BSSRDF_BURLEY_ID},
				{SurfaceMaterial::PBRInfo::SubsurfaceMethod::RandomWalk,ccl::ClosureType::CLOSURE_BSSRDF_RANDOM_WALK_ID},
				{SurfaceMaterial::PBRInfo::SubsurfaceMethod::PrincipledRandomWalk,ccl::ClosureType::CLOSURE_BSSRDF_PRINCIPLED_RANDOM_WALK_ID}
				};

				int32_t subsurfaceMethod;
				if(dataSSS->GetInt("method",&subsurfaceMethod))
				{
					auto it = bssrdfToCclType.find(static_cast<SurfaceMaterial::PBRInfo::SubsurfaceMethod>(subsurfaceMethod));
					if(it != bssrdfToCclType.end())
						subsurfaceMethod = it->second;
				}

				auto &dvSubsurfaceRadius = dataSSS->GetValue("radius");
				if(dvSubsurfaceRadius != nullptr && typeid(*dvSubsurfaceRadius) == typeid(ds::Vector))
					shader->SetSubsurfaceRadius(static_cast<ds::Vector&>(*dvSubsurfaceRadius).GetValue());
			}
			//

			// Note: We always need the albedo texture information for the translucency.
			// Whether metalness/roughness/etc. affect baking in any way is unclear (probably not),
			// but it also doesn't hurt to have them.

			// Albedo map
			shader->GetAlbedoSet().SetAlbedoMap(*diffuseTexPath);
			if(albedo2TexPath.has_value())
			{
				shader->GetAlbedoSet2().SetAlbedoMap(*albedo2TexPath);
				shader->SetUseVertexAlphasForBlending(true);
			}
			shader->SetAlphaMode(mat.GetAlphaMode(),mat.GetAlphaCutoff());

			// Normal map
			auto normalTexPath = prepare_texture(m_scene,mat.GetNormalMap(),PreparedTextureInputFlags::None);
			if(normalTexPath)
				shader->SetNormalMap(*normalTexPath);

			// Metalness map
			auto metalnessTexPath = prepare_texture(m_scene,mat.GetRMAMap(),PreparedTextureInputFlags::None,nullptr,"white");
			if(metalnessTexPath)
				shader->SetMetalnessMap(*metalnessTexPath);

			// If no metalness map is available, just use metalness factor directly
			float metalnessFactor;
			if(mat.GetDataBlock()->GetFloat("metalness_factor",&metalnessFactor))
				shader->SetMetalnessFactor(metalnessFactor);

			// Roughness map
			AddRoughnessMapImageTextureNode(*shader,mat,0.5f);

			// Emission map
			auto globalEmissionStrength = m_emissionStrength;
			shader->SetEmissionIntensity(globalEmissionStrength);
			if(globalEmissionStrength > 0.f)
			{
				auto *emissionTex = mat.GetGlowMap();
				static auto particleLightEmissionFactor = 0.f;
				if(particleLightEmissionFactor > 0.f && isParticleSystemShader)
				{
					emissionTex = emissionTex ? emissionTex : diffuseMap;
					auto strength = particleLightEmissionFactor;
					shader->SetEmissionFactor({strength,strength,strength});
				}
				auto emissionTexPath = prepare_texture(m_scene,emissionTex,PreparedTextureInputFlags::None);
				if(emissionTexPath)
				{
					shader->SetEmissionMap(*emissionTexPath);
					if(data->GetBool("glow_alpha_only") == true)
					{
						shader->SetEmissionFromAlbedoAlpha(*shader,true);

						// Glow intensity
						auto glowBlendDiffuseScale = 1.f;
						data->GetFloat("glow_blend_diffuse_scale",&glowBlendDiffuseScale);

						auto glowScale = 1.f;
						data->GetFloat("glow_scale",&glowScale);

						auto glowIntensity = glowBlendDiffuseScale *glowScale +1.f; // +1 to match Pragma more closely
						auto strength = glowIntensity;
						shader->SetEmissionFactor({strength,strength,strength});
					}
				}

				auto valEmissionFactor = mat.GetDataBlock()->GetValue("emission_factor");
				if(valEmissionFactor && typeid(*valEmissionFactor) == typeid(ds::Vector))
				{
					auto &emissionFactor = static_cast<ds::Vector&>(*valEmissionFactor).GetValue();
					shader->SetEmissionFactor(emissionFactor);
				}
			}

			// Wrinkle maps
			auto wrinkleStretchMap = prepare_texture(m_scene,mat.GetTextureInfo(Material::WRINKLE_STRETCH_MAP_IDENTIFIER),PreparedTextureInputFlags::None);
			if(wrinkleStretchMap)
				shader->SetWrinkleStretchMap(*wrinkleStretchMap);

			auto wrinkleCompressMap = prepare_texture(m_scene,mat.GetTextureInfo(Material::WRINKLE_COMPRESS_MAP_IDENTIFIER),PreparedTextureInputFlags::None);
			if(wrinkleCompressMap)
				shader->SetWrinkleCompressMap(*wrinkleCompressMap);
			resShader = shader;
		}
	}
	if(resShader && shaderInfo.entity.has_value() && shaderInfo.subMesh.has_value())
	{
		auto normalMapSpace = NormalMapNode::Space::Tangent;
		if(ustring::compare(mat.GetShaderIdentifier(),"eye",false))
		{
			//normalMapSpace = NormalMapNode::Space::Object;

			// Eye shader; We'll have to do some additional steps to get the proper UV coordinates.
			auto eyeC = (*shaderInfo.entity)->GetComponent<CEyeComponent>();
			auto &mdl = *(*shaderInfo.entity)->GetModel();
			uint32_t eyeballIndex;
			if(eyeC.valid() && eyeC->FindEyeballIndex((*shaderInfo.subMesh)->GetSkinTextureIndex(),eyeballIndex))
			{
				auto *eyeballData = eyeC->GetEyeballData(eyeballIndex);
				auto *eyeball = mdl.GetEyeball(eyeballIndex);
				if(eyeball && eyeballData)
				{
					Vector4 irisProjU,irisProjV;
					if(eyeC->GetEyeballProjectionVectors(eyeballIndex,irisProjU,irisProjV))
					{
						auto dilationFactor = eyeballData->config.dilation;
						auto maxDilationFactor = eyeball->maxDilationFactor;
						auto irisUvRadius = eyeball->irisUvRadius;
						auto uvHandler = std::make_shared<UVHandlerEye>(irisProjU,irisProjV,dilationFactor,maxDilationFactor,irisUvRadius);
						resShader->SetUVHandler(Shader::TextureType::Albedo,uvHandler);
						resShader->SetUVHandler(Shader::TextureType::Emission,uvHandler);
					}
				}
			}
		}

		auto *normalModule = dynamic_cast<ShaderModuleNormal*>(resShader.get());
		if(normalModule)
			normalModule->SetNormalMapSpace(normalMapSpace);
	}
	return resShader;
}
cycles::PShader cycles::Scene::CreateShader(Mesh &mesh,Model &mdl,ModelSubMesh &subMesh,BaseEntity *optEnt,uint32_t skinId)
{
	// Make sure all textures have finished loading
	static_cast<CMaterialManager&>(client->GetMaterialManager()).GetTextureManager().WaitForTextures();

	auto *mat = optEnt ? GetMaterial(*optEnt,subMesh,skinId) : GetMaterial(mdl,subMesh,skinId);
	if(mat == nullptr)
		return nullptr;
	ShaderInfo shaderInfo {};
	if(optEnt)
		shaderInfo.entity = optEnt;
	shaderInfo.subMesh = &subMesh;
	return CreateShader(*mat,mesh.GetName(),shaderInfo);
}

void cycles::Scene::SetAOBakeTarget(Model &mdl,uint32_t matIndex)
{
	std::vector<ModelSubMesh*> materialMeshes;
	std::vector<ModelSubMesh*> envMeshes;
	uint32_t numVerts = 0;
	uint32_t numTris = 0;
	uint32_t numVertsEnv = 0;
	uint32_t numTrisEnv = 0;

	AddModel(mdl,"ao_mesh",nullptr,0 /* skin */,nullptr,nullptr,nullptr,[matIndex,&materialMeshes,&envMeshes,&numVerts,&numTris,&numVertsEnv,&numTrisEnv,&mdl](ModelSubMesh &mesh) -> bool {
		auto texIdx = mdl.GetMaterialIndex(mesh);
		if(texIdx.has_value() && *texIdx == matIndex)
		{
			materialMeshes.push_back(&mesh);
			numVerts += mesh.GetVertexCount();
			numTris += mesh.GetTriangleCount();
			return false;
		}
		numVertsEnv += mesh.GetVertexCount();
		numTrisEnv += mesh.GetTriangleCount();
		envMeshes.push_back(&mesh);
		return false;
	});

	// We'll create a separate mesh from all model meshes which use the specified material.
	// This way we can map the uv coordinates to the ao output texture more easily.
	auto mesh = Mesh::Create(*this,"ao_target",numVerts,numTris);
	for(auto &matMesh : materialMeshes)
		AddMesh(mdl,*mesh,*matMesh);
	auto o = Object::Create(*this,*mesh);
	m_bakeTarget = o;

	if(envMeshes.empty())
		return;

	// Note: Ambient occlusion is baked for a specific material (matIndex). The model may contain meshes that use a different material,
	// in which case those meshes are still needed to render accurate ambient occlusion values near edge cases.
	// To distinguish them from the actual ao-meshes, they're stored in a separate mesh/object here.
	// The actual ao bake target (see code above) has to be the first mesh added to the scene, otherwise the ao result may be incorrect.
	// The reason for this is currently unknown.
	auto meshEnv = Mesh::Create(*this,"ao_mesh",numVertsEnv,numTrisEnv);
	for(auto *subMesh : envMeshes)
		AddMesh(mdl,*meshEnv,*subMesh);
	Object::Create(*this,*meshEnv);
}

void cycles::Scene::AddShader(CCLShader &shader) {m_cclShaders.push_back(shader.shared_from_this());}

void cycles::Scene::SetLightmapBakeTarget(BaseEntity &ent)
{
	auto lightmapC = ent.GetComponent<pragma::CLightMapComponent>();
	m_lightmapTargetComponent = lightmapC;
	if(lightmapC.expired())
	{
		Con::cwar<<"WARNING: Invalid target for lightmap baking: Entity has no lightmap component!"<<Con::endl;
		return;
	}
	std::vector<ModelSubMesh*> targetMeshes {};
	auto o = AddEntity(ent,&targetMeshes);
	if(o == nullptr)
		return;
	auto &mesh = o->GetMesh();

	// Lightmap uvs per mesh
	auto numTris = mesh.GetTriangleCount();
	std::vector<ccl::float2> cclLightmapUvs {};
	cclLightmapUvs.resize(numTris *3);
	size_t uvOffset = 0;
	for(auto *subMesh : targetMeshes)
	{
		auto &tris = subMesh->GetTriangles();
		auto *uvSet = subMesh->GetUVSet("lightmap");
		if(uvSet)
		{
			for(auto i=decltype(tris.size()){0u};i<tris.size();i+=3)
			{
				auto idx0 = tris.at(i);
				auto idx1 = tris.at(i +1);
				auto idx2 = tris.at(i +2);
				cclLightmapUvs.at(uvOffset +i) = Scene::ToCyclesUV(uvSet->at(idx0));
				cclLightmapUvs.at(uvOffset +i +1) = Scene::ToCyclesUV(uvSet->at(idx1));
				cclLightmapUvs.at(uvOffset +i +2) = Scene::ToCyclesUV(uvSet->at(idx2));
			}
		}
		uvOffset += tris.size();
	}
	mesh.SetLightmapUVs(std::move(cclLightmapUvs));
	m_bakeTarget = o;
}

void cycles::Scene::AddMesh(Model &mdl,Mesh &mesh,ModelSubMesh &mdlMesh,pragma::CModelComponent *optMdlC,pragma::CAnimatedComponent *optAnimC,BaseEntity *optEnt,uint32_t skinId)
{
	auto shader = CreateShader(mesh,mdl,mdlMesh,optEnt,skinId);
	if(shader == nullptr)
		return;
	auto *mat = optMdlC ? GetMaterial(*optMdlC,mdlMesh,skinId) : GetMaterial(mdl,mdlMesh,skinId);
	auto shaderIdx = mesh.AddSubMeshShader(*shader);
	auto triIndexVertexOffset = mesh.GetVertexOffset();
	auto &verts = mdlMesh.GetVertices();
	auto &alphas = mdlMesh.GetAlphas();
	auto hasAlphas = mesh.HasAlphas();
	for(auto vertIdx=decltype(verts.size()){0u};vertIdx<verts.size();++vertIdx)
	{
		auto &v = verts.at(vertIdx);
		if(IsRenderSceneMode(m_renderMode))
		{
			// TODO: Do we really need the tangent?
			Vector3 normalOffset {};
			float wrinkle = 0.f;
			auto transformMat = optAnimC ? optAnimC->GetVertexTransformMatrix(mdlMesh,vertIdx,&normalOffset,&wrinkle) : std::optional<Mat4>{};
			if(transformMat.has_value())
			{
				// Apply vertex matrix (including animations, flexes, etc.)
				auto vpos = *transformMat *Vector4{v.position.x,v.position.y,v.position.z,1.f};
				auto vn = *transformMat *Vector4{v.normal.x,v.normal.y,v.normal.z,0.f};
				auto vt = *transformMat *Vector4{v.tangent.x,v.tangent.y,v.tangent.z,0.f};
				Vector3 pos {vpos.x,vpos.y,vpos.z};
				pos /= vpos.w;

				Vector3 n {vn.x,vn.y,vn.z};
				n += normalOffset;
				uvec::normalize(&n);

				Vector3 t {vt.x,vt.y,vt.z};
				t += normalOffset;
				uvec::normalize(&t);

				mesh.AddVertex(pos,n,t,v.uv);
			}
			else
				mesh.AddVertex(v.position,v.normal,v.tangent,v.uv);
			mesh.AddWrinkleFactor(wrinkle);
		}
		else
		{
			// We're probably baking something (e.g. ao map), so we don't want to include the entity's animated pose.
			mesh.AddVertex(v.position,v.normal,v.tangent,v.uv);
		}

		if(hasAlphas)
		{
			auto alpha = (vertIdx < alphas.size()) ? alphas.at(vertIdx).x : 0.f;
			mesh.AddAlpha(alpha);
		}
	}

	auto &tris = mdlMesh.GetTriangles();
	for(auto i=decltype(tris.size()){0u};i<tris.size();i+=3)
		mesh.AddTriangle(triIndexVertexOffset +tris.at(i),triIndexVertexOffset +tris.at(i +1),triIndexVertexOffset +tris.at(i +2),shaderIdx);
}

cycles::PMesh cycles::Scene::AddMeshList(
	Model &mdl,const std::vector<std::shared_ptr<ModelMesh>> &meshList,const std::string &meshName,BaseEntity *optEnt,uint32_t skinId,
	pragma::CModelComponent *optMdlC,pragma::CAnimatedComponent *optAnimC,
	const std::function<bool(ModelMesh&)> &optMeshFilter,
	const std::function<bool(ModelSubMesh&)> &optSubMeshFilter
)
{
	std::vector<ModelSubMesh*> targetMeshes {};
	targetMeshes.reserve(mdl.GetSubMeshCount());
	uint64_t numVerts = 0ull;
	uint64_t numTris = 0ull;
	auto hasAlphas = false;
	for(auto &mesh : meshList)
	{
		if(optMeshFilter != nullptr && optMeshFilter(*mesh) == false)
			continue;
		for(auto &subMesh : mesh->GetSubMeshes())
		{
			if(subMesh->GetGeometryType() != ModelSubMesh::GeometryType::Triangles || (optSubMeshFilter != nullptr && optSubMeshFilter(*subMesh) == false))
				continue;
			targetMeshes.push_back(subMesh.get());
			numVerts += subMesh->GetVertexCount();
			numTris += subMesh->GetTriangleCount();
			hasAlphas = hasAlphas || (subMesh->GetAlphaCount() > 0);
		}
	}

	if(numTris == 0)
		return nullptr;

	// Create the mesh
	// TODO: If multiple entities are using same model, CACHE the mesh(es)! (Unless they're animated)
	auto flags = Mesh::Flags::None;
	if(hasAlphas)
		flags |= Mesh::Flags::HasAlphas;
	if(mdl.GetVertexAnimations().empty() == false) // TODO: Not the best way to determine if the entity uses wrinkles
		flags |= Mesh::Flags::HasWrinkles;
	auto mesh = Mesh::Create(*this,meshName,numVerts,numTris,flags);
	for(auto *subMesh : targetMeshes)
		AddMesh(mdl,*mesh,*subMesh,optMdlC,optAnimC,optEnt,skinId);
	return mesh;
}

cycles::PMesh cycles::Scene::AddModel(
	Model &mdl,const std::string &meshName,BaseEntity *optEnt,uint32_t skinId,
	pragma::CModelComponent *optMdlC,pragma::CAnimatedComponent *optAnimC,
	const std::function<bool(ModelMesh&)> &optMeshFilter,const std::function<bool(ModelSubMesh&)> &optSubMeshFilter
)
{
	std::vector<std::shared_ptr<ModelMesh>> lodMeshes {};
	std::vector<uint32_t> bodyGroups {};
	bodyGroups.resize(mdl.GetBodyGroupCount());
	mdl.GetBodyGroupMeshes(bodyGroups,0,lodMeshes);
	return AddMeshList(mdl,lodMeshes,meshName,optEnt,skinId,optMdlC,optAnimC,optMeshFilter,optSubMeshFilter);
}

void cycles::Scene::AddSkybox(const std::string &texture)
{
	if(umath::is_flag_set(m_stateFlags,StateFlags::SkyInitialized))
		return;
	umath::set_flag(m_stateFlags,StateFlags::SkyInitialized);

	auto skyTex = (m_sky.empty() == false) ? m_sky : texture;

	// Note: m_sky can be absolute or relative path
	std::string absPath = skyTex;
	if(FileManager::ExistsSystem(absPath) == false && FileManager::FindAbsolutePath("materials/" +skyTex,absPath) == false)
		return;

	// Setup the skybox as a background shader
	auto shader = cycles::Shader::Create<ShaderGeneric>(*this,"background");
	auto cclShader = shader->GenerateCCLShader(*m_scene.default_background);
	auto nodeBg = cclShader->AddBackgroundNode();
	nodeBg.SetStrength(m_skyStrength);

	auto nodeTex = cclShader->AddEnvironmentTextureNode(absPath);
	cclShader->Link(nodeTex,nodeBg.inColor);
	cclShader->Link(nodeBg,cclShader->GetOutputNode().inSurface);

	auto nodeTexCoord = cclShader->AddTextureCoordinateNode();
	auto nodeMapping = cclShader->AddMappingNode();
	nodeMapping.SetType(MappingNode::Type::Point);
	nodeMapping.SetRotation(m_skyAngles);
	cclShader->Link(nodeTexCoord.outGenerated,nodeMapping.inVector);

	cclShader->Link(nodeMapping.outVector,nodeTex.inVector);

	// Add the light source for the background
	auto *light = new ccl::Light{}; // Object will be removed automatically by cycles
	light->tfm = ccl::transform_identity();

	m_scene.lights.push_back(light);
	light->type = ccl::LightType::LIGHT_BACKGROUND;
	light->map_resolution = 2'048;
	light->shader = m_scene.default_background;
	light->use_mis = true;
	light->max_bounces = 1'024;
	light->samples = 4;
	light->tag_update(&m_scene);
}

cycles::PObject cycles::Scene::AddEntity(
	BaseEntity &ent,std::vector<ModelSubMesh*> *optOutTargetMeshes,
	const std::function<bool(ModelMesh&)> &meshFilter,const std::function<bool(ModelSubMesh&)> &subMeshFilter,
	const std::string &nameSuffix
)
{
#if 0
	if(m_renderMode == RenderMode::BakeDiffuseLighting && ent.IsWorld() == false)
	{
		Con::cwar<<"WARNING: Baking diffuse lighting currently only supported for world entity, but attempted to add an entity of a different type! Entity will be ignored!"<<Con::endl;
		return;
	}
#endif
	auto *mdlC = static_cast<pragma::CModelComponent*>(ent.GetModelComponent().get());
	auto mdl = mdlC ? mdlC->GetModel() : nullptr;
	if(mdl == nullptr)
		return nullptr;

	auto animC = ent.GetComponent<CAnimatedComponent>();
	std::string name = "ent" +nameSuffix +"_" +std::to_string(ent.GetLocalIndex());
	std::vector<ModelSubMesh*> tmpTargetMeshes {};
	auto *targetMeshes = (optOutTargetMeshes != nullptr) ? optOutTargetMeshes : &tmpTargetMeshes;
	targetMeshes->reserve(mdl->GetSubMeshCount());

	auto skyC = ent.GetComponent<CSkyboxComponent>();
	if(skyC.valid())
	{
		AddModel(*mdl,name,&ent,ent.GetSkin(),mdlC,animC.get(),meshFilter,[&targetMeshes,&subMeshFilter](ModelSubMesh &mesh) -> bool {
			if(subMeshFilter && subMeshFilter(mesh) == false)
				return false;
			targetMeshes->push_back(&mesh);
			return false;
		});
		std::optional<std::string> skyboxTexture {};
		for(auto &mesh : *targetMeshes)
		{
			auto *mat = mdlC->GetRenderMaterial(mesh->GetSkinTextureIndex());
			if(mat == nullptr || (ustring::compare(mat->GetShaderIdentifier(),"skybox",false) == false && ustring::compare(mat->GetShaderIdentifier(),"skybox_equirect",false) == false))
				continue;
			auto *diffuseMap = mat->GetTextureInfo("skybox");
			auto tex = diffuseMap ? diffuseMap->texture : nullptr;
			auto vkTex = tex ? std::static_pointer_cast<Texture>(tex)->GetVkTexture() : nullptr;
			if(vkTex == nullptr || vkTex->GetImage().IsCubemap() == false)
				continue;
			PreparedTextureOutputFlags flags;
			auto diffuseTexPath = prepare_texture(m_scene,diffuseMap,PreparedTextureInputFlags::CanBeEnvMap,&flags);
			if(diffuseTexPath.has_value() == false || umath::is_flag_set(flags,PreparedTextureOutputFlags::Envmap) == false)
				continue;
			skyboxTexture = diffuseTexPath;
		}
		if(skyboxTexture.has_value())
			m_sky = *skyboxTexture;
		return nullptr;
	}

	auto fFilterMesh = [&targetMeshes,&subMeshFilter](ModelSubMesh &mesh) -> bool {
		if(subMeshFilter && subMeshFilter(mesh) == false)
			return false;
		targetMeshes->push_back(&mesh);
		return true;
	};

	auto renderC = ent.GetComponent<pragma::CRenderComponent>();
	PMesh mesh = nullptr;
	if(renderC.valid())
		mesh = AddMeshList(*mdl,renderC->GetLODMeshes(),name,&ent,ent.GetSkin(),mdlC,animC.get(),meshFilter,fFilterMesh);
	else
		mesh = AddModel(*mdl,name,&ent,ent.GetSkin(),mdlC,animC.get(),meshFilter,fFilterMesh);
	if(mesh == nullptr)
		return nullptr;

	// Create the object using the mesh
	physics::ScaledTransform t;
	ent.GetPose(t);
	auto o = Object::Create(*this,*mesh);
	if(IsRenderSceneMode(m_renderMode) || m_renderMode == RenderMode::BakeDiffuseLighting)
	{
		o->SetPos(t.GetOrigin());
		o->SetRotation(t.GetRotation());
		o->SetScale(t.GetScale());
	}
	return o;
}

static uint32_t calc_pixel_offset(uint32_t imgWidth,uint32_t xOffset,uint32_t yOffset)
{
	return yOffset *imgWidth +xOffset;
}

static bool row_contains_visible_pixels(const float *inOutImgData,uint32_t pxStartOffset,uint32_t w)
{
	for(auto x=decltype(w){0u};x<w;++x)
	{
		if(inOutImgData[(pxStartOffset +x) *4 +3] > 0.f)
			return true;
	}
	return false;
}

static bool col_contains_visible_pixels(const float *inOutImgData,uint32_t pxStartOffset,uint32_t h,uint32_t imgWidth)
{
	for(auto y=decltype(h){0u};y<h;++y)
	{
		if(inOutImgData[(pxStartOffset +(y *imgWidth)) *4 +3] > 0.f)
			return true;
	}
	return false;
}

static void shrink_area_to_fit(const float *inOutImgData,uint32_t imgWidth,uint32_t &xOffset,uint32_t &yOffset,uint32_t &w,uint32_t &h)
{
	while(h > 0 && row_contains_visible_pixels(inOutImgData,calc_pixel_offset(imgWidth,xOffset,yOffset),w) == false)
	{
		++yOffset;
		--h;
	}
	while(h > 0 && row_contains_visible_pixels(inOutImgData,calc_pixel_offset(imgWidth,xOffset,yOffset +h -1),w) == false)
		--h;

	while(w > 0 && col_contains_visible_pixels(inOutImgData,calc_pixel_offset(imgWidth,xOffset,yOffset),h,imgWidth) == false)
	{
		++xOffset;
		--w;
	}
	while(w > 0 && col_contains_visible_pixels(inOutImgData,calc_pixel_offset(imgWidth,xOffset +w -1,yOffset),h,imgWidth) == false)
		--w;
}

void cycles::Scene::DenoiseHDRImageArea(uimg::ImageBuffer &imgBuffer,uint32_t imgWidth,uint32_t imgHeight,uint32_t xOffset,uint32_t yOffset,uint32_t w,uint32_t h) const
{
	// In some cases the borders may not contain any image data (i.e. fully transparent) if the pixels are not actually
	// being used by any geometry. Since the denoiser does not know transparency, we have to shrink the image area to exclude the
	// transparent borders to avoid artifacts.
	auto *imgData = static_cast<float*>(imgBuffer.GetData());
	shrink_area_to_fit(imgData,imgWidth,xOffset,yOffset,w,h);

	if(w == 0 || h == 0)
		return; // Nothing for us to do

	// Sanity check
	auto pxStartOffset = calc_pixel_offset(imgWidth,xOffset,yOffset);
	for(auto y=decltype(h){0u};y<h;++y)
	{
		for(auto x=decltype(w){0u};x<w;++x)
		{
			auto srcPxIdx = pxStartOffset +y *imgWidth +x;
			auto a = imgData[srcPxIdx *4 +3];
			if(a < 1.f)
			{
				// This should be unreachable, but just in case...
				// If this case does occur, that means there are transparent pixels WITHIN the image area, which are not
				// part of a transparent border!
				Con::cerr<<"ERROR: Image area for denoising contains transparent pixel at ("<<x<<","<<y<<") with alpha of "<<a<<"! This is not allowed!"<<Con::endl;
			}
		}
	}

	// White areas
	/*for(auto y=decltype(h){0u};y<h;++y)
	{
		for(auto x=decltype(w){0u};x<w;++x)
		{
			auto srcPxIdx = pxStartOffset +y *imgWidth +x;
			auto dstPxIdx = y *w +x;
			if(inOutImgData[srcPxIdx *4 +3] == 0.f)
			{
				inOutImgData[srcPxIdx *4 +0] = 0.f;
				inOutImgData[srcPxIdx *4 +1] = 0.f;
				inOutImgData[srcPxIdx *4 +2] = 0.f;
				inOutImgData[srcPxIdx *4 +3] = 1.f;
			}
			else
			{
				inOutImgData[srcPxIdx *4 +0] = 1.f;
				inOutImgData[srcPxIdx *4 +1] = 1.f;
				inOutImgData[srcPxIdx *4 +2] = 1.f;
				inOutImgData[srcPxIdx *4 +3] = 1.f;
			}
		}
	}*/

	std::vector<float> imgAreaData {};
	imgAreaData.resize(w *h *3);
	// Extract the area from the image data
	for(auto y=decltype(h){0u};y<h;++y)
	{
		for(auto x=decltype(w){0u};x<w;++x)
		{
			auto srcPxIdx = pxStartOffset +y *imgWidth +x;
			auto dstPxIdx = y *w +x;
			for(uint8_t i=0;i<3;++i)
				imgAreaData.at(dstPxIdx *3 +i) = imgData[srcPxIdx *4 +i];
		}
	}

	// Denoise the extracted area
	DenoiseInfo denoiseInfo {};
	denoiseInfo.hdr = true;
	denoiseInfo.width = w;
	denoiseInfo.height = h;
	Denoise(denoiseInfo,imgAreaData.data());
	
	// Copy the denoised area back into the original image
	for(auto y=decltype(h){0u};y<h;++y)
	{
		for(auto x=decltype(w){0u};x<w;++x)
		{
			auto srcPxIdx = pxStartOffset +y *imgWidth +x;
			//if(inOutImgData[srcPxIdx *4 +3] == 0.f)
			//	continue; // Alpha is zero; Skip this one
			auto dstPxIdx = y *w +x;
			//for(uint8_t i=0;i<3;++i)
			//	inOutImgData[srcPxIdx *4 +i] = imgAreaData.at(dstPxIdx *3 +i);
			/*if(inOutImgData[srcPxIdx *4 +3] == 0.f)
			{
				inOutImgData[srcPxIdx *4 +0] = 0.f;
				inOutImgData[srcPxIdx *4 +1] = 0.f;
				inOutImgData[srcPxIdx *4 +2] = 0.f;
				inOutImgData[srcPxIdx *4 +3] = 1.f;
			}
			else
			{
				inOutImgData[srcPxIdx *4 +0] = 1.f;
				inOutImgData[srcPxIdx *4 +1] = 1.f;
				inOutImgData[srcPxIdx *4 +2] = 1.f;
				inOutImgData[srcPxIdx *4 +3] = 1.f;
			}*/
		}
	}
}

void cycles::Scene::SetupRenderSettings(
	ccl::Scene &scene,ccl::Session &session,ccl::BufferParams &bufferParams,cycles::Scene::RenderMode renderMode,
	uint32_t maxTransparencyBounces
) const
{
	// Default parameters taken from Blender
	auto &integrator = *scene.integrator;
	integrator.min_bounce = 0;
	integrator.max_bounce = 12;
	integrator.max_diffuse_bounce = 4;
	integrator.max_glossy_bounce = 4;
	integrator.max_transmission_bounce = 12;
	integrator.max_volume_bounce = 0;

	integrator.transparent_min_bounce = 0;
	integrator.transparent_max_bounce = maxTransparencyBounces;

	integrator.volume_max_steps = 1024;
	integrator.volume_step_size = 0.1;

	integrator.caustics_reflective = true;
	integrator.caustics_refractive = true;
	integrator.filter_glossy = 0.f;
	integrator.seed = 0;
	integrator.sampling_pattern = ccl::SamplingPattern::SAMPLING_PATTERN_SOBOL;

	integrator.sample_clamp_direct = 0.f;
	integrator.sample_clamp_indirect = 0.f;
	integrator.motion_blur = false;
	integrator.method = ccl::Integrator::Method::PATH;
	integrator.sample_all_lights_direct = true;
	integrator.sample_all_lights_indirect = true;
	integrator.light_sampling_threshold = 0.f;

	integrator.diffuse_samples = 1;
	integrator.glossy_samples = 1;
	integrator.transmission_samples = 1;
	integrator.ao_samples = 1;
	integrator.mesh_light_samples = 1;
	integrator.subsurface_samples = 1;
	integrator.volume_samples = 1;

	integrator.ao_bounces = 0;
	integrator.tag_update(&scene);

	// Film
	auto &film = *scene.film;
	film.exposure = 1.f;
	film.filter_type = ccl::FilterType::FILTER_GAUSSIAN;
	film.filter_width = 1.5f;
	if(renderMode == cycles::Scene::RenderMode::RenderImage)
	{
		film.mist_start = 5.f;
		film.mist_depth = 25.f;
		film.mist_falloff = 2.f;
	}
	film.tag_update(&scene);
	film.tag_passes_update(&scene, film.passes);

	film.cryptomatte_depth = 3;
	film.cryptomatte_passes = ccl::CRYPT_NONE;

	session.params.pixel_size = 1;
	session.params.threads = 0;
	session.params.use_profiling = false;
	session.params.shadingsystem = ccl::ShadingSystem::SHADINGSYSTEM_SVM;

	ccl::vector<ccl::Pass> passes;
	auto displayPass = ccl::PassType::PASS_DIFFUSE_COLOR;
	switch(renderMode)
	{
	case cycles::Scene::RenderMode::SceneAlbedo:
		// Note: PASS_DIFFUSE_COLOR would probably make more sense but does not seem to work
		// (just creates a black output).
		ccl::Pass::add(ccl::PassType::PASS_COMBINED,passes);
		ccl::Pass::add(ccl::PassType::PASS_DEPTH,passes);
		displayPass = ccl::PassType::PASS_COMBINED;
		break;
	case cycles::Scene::RenderMode::SceneNormals:
		ccl::Pass::add(ccl::PassType::PASS_COMBINED,passes);
		ccl::Pass::add(ccl::PassType::PASS_DEPTH,passes);
		displayPass = ccl::PassType::PASS_COMBINED;
		break;
	case cycles::Scene::RenderMode::RenderImage:
		ccl::Pass::add(ccl::PassType::PASS_COMBINED,passes);
		ccl::Pass::add(ccl::PassType::PASS_DEPTH,passes);
		displayPass = ccl::PassType::PASS_COMBINED;
		break;
	case cycles::Scene::RenderMode::BakeAmbientOcclusion:
		ccl::Pass::add(ccl::PassType::PASS_AO,passes);
		ccl::Pass::add(ccl::PassType::PASS_DEPTH,passes);
		displayPass = ccl::PassType::PASS_AO;
		break;
	case cycles::Scene::RenderMode::BakeDiffuseLighting:
		ccl::Pass::add(ccl::PassType::PASS_DIFFUSE_DIRECT,passes);
		ccl::Pass::add(ccl::PassType::PASS_DIFFUSE_INDIRECT,passes);
		ccl::Pass::add(ccl::PassType::PASS_DEPTH,passes);
		displayPass = ccl::PassType::PASS_COMBINED; // TODO: Is this correct?
		break;
	}
	bufferParams.passes = passes;

	if(m_motionBlurStrength > 0.f)
	{
		// ccl::Pass::add(ccl::PassType::PASS_MOTION,passes);
		scene.integrator->motion_blur = true;
	}

	film.pass_alpha_threshold = 0.5;
	film.tag_passes_update(&scene, passes);
	film.display_pass = displayPass;
	film.tag_update(&scene);
	scene.integrator->tag_update(&scene);

	// Camera
	/*auto &cam = *scene.camera;
	cam.shuttertime	= 0.500000000;
	cam.motion_position=	ccl::Camera::MotionPosition::MOTION_POSITION_CENTER;
	cam.shutter_table_offset=	18446744073709551615;
	cam.rolling_shutter_type=	ccl::Camera::RollingShutterType::ROLLING_SHUTTER_NONE;
	cam.rolling_shutter_duration=	0.100000001	;
	cam.focaldistance=	2.49260306	;
	cam.aperturesize=	0.00625000009	;
	cam.blades=	0;
	cam.bladesrotation=	0.000000000	;
	cam.type=	ccl::CAMERA_PERSPECTIVE ;
	cam.fov=	0.503379941	;
	cam.panorama_type=	ccl::PANORAMA_FISHEYE_EQUISOLID ;
	cam.fisheye_fov=	3.14159274	;
	cam.fisheye_lens=	10.5000000	;
	cam.latitude_min=	-1.57079637	;
	cam.latitude_max=	1.57079637	;
	cam.longitude_min=	-3.14159274	;
	cam.longitude_max=	3.14159274	;
	cam.stereo_eye=	ccl::Camera::STEREO_NONE ;
	cam.use_spherical_stereo=	false	;
	cam.interocular_distance=	0.0649999976	;
	cam.convergence_distance=	1.94999993	;
	cam.use_pole_merge	=false	;
	cam.pole_merge_angle_from=	1.04719758	;
	cam.pole_merge_angle_to=	1.30899692	;
	cam.aperture_ratio=	1.00000000	;
	cam.sensorwidth=	0.0359999985	;
	cam.sensorheight=	0.0240000002	;
	cam.nearclip=	0.100000001	;
	cam.farclip	=100.000000	;
	cam.width	=3840	;
	cam.height=	2160	;
	cam.resolution=	1	;

	cam.viewplane.left=	-1.77777779	;
	cam.viewplane.right=	1.77777779	;
	cam.viewplane.bottom=	-1.00000000	;
	cam.viewplane.top=	1.00000000	;


	cam.full_width=	3840	;
	cam.full_height=	2160	;
	cam.offscreen_dicing_scale	=4.00000000	;
	cam.border.left = 0.f;
	cam.border.right = 1.f;
	cam.border.bottom = 0.f;
	cam.border.top = 1.f;
	cam.viewport_camera_border .left = 0.f;
	cam.viewport_camera_border.right = 1.f;
	cam.viewport_camera_border.bottom = 0.f;
	cam.viewport_camera_border.top = 1.f;

	cam.matrix.x.x=	1.00000000	;
	cam.matrix.x.y=	1.63195708e-07	;
	cam.matrix.x.z=	3.42843151e-07	;
	cam.matrix.x.w=	17.5277958	;

	cam.matrix.y.x=	-3.47716451e-07	;
	cam.matrix.y.y	=0.0308625121	;
	cam.matrix.y.z=	0.999523640	;
	cam.matrix.y.w=	-2.77792454	;

	cam.matrix.z.x=	-1.52536970e-07	;
	cam.matrix.z.y=	0.999523640	;
	cam.matrix.z.z=	-0.0308625121	;
	cam.matrix.z.w=	0.846632719	;

	cam.use_perspective_motion=	false	;
	cam.fov_pre=	0.503379941	;
	cam.fov_post	=0.503379941	;

	cam.tag_update();*/
}

ccl::BufferParams cycles::Scene::GetBufferParameters() const
{
	ccl::BufferParams bufferParams {};
	bufferParams.width = m_scene.camera->width;
	bufferParams.height = m_scene.camera->height;
	bufferParams.full_width = m_scene.camera->width;
	bufferParams.full_height = m_scene.camera->height;
	SetupRenderSettings(m_scene,*m_session,bufferParams,m_renderMode,m_maxTransparencyBounces);
	return bufferParams;
}

void cycles::Scene::InitializeAlbedoPass(bool reloadShaders)
{
	auto bufferParams = GetBufferParameters();
	uint32_t sampleCount = 1;
	m_session->params.samples = sampleCount;
	m_session->reset(bufferParams,sampleCount); // We only need the normals and albedo colors for the first sample

	m_scene.lights.clear();

	if(reloadShaders == false)
		return;
	// Note: For denoising the scene has to be rendered three times:
	// 1) With lighting
	// 2) Albedo colors only
	// 3) Normals only
	// However, Cycles doesn't allow rendering to multiple outputs at once, so we
	// need three separate render passes. For the additional render passes
	// we have to replace the shaders, which is also impossible to do with Cycles.
	// Instead, we have to create an additional set of shaders for each object and
	// re-assign the shader indices of the mesh.
	for(auto &o : m_objects)
	{
		auto &mesh = o->GetMesh();
		auto &subMeshShaders = mesh.GetSubMeshShaders();
		auto numShaders = subMeshShaders.size();
		auto shaderOffset = mesh->used_shaders.size();
		mesh->used_shaders.resize(mesh->used_shaders.size() +numShaders);
		for(auto i=decltype(numShaders){0u};i<numShaders;++i)
		{
			auto &shader = subMeshShaders.at(i);
			auto *albedoModule = dynamic_cast<ShaderModuleAlbedo*>(shader.get());
			if(albedoModule == nullptr)
				continue;
			auto &albedoMap = albedoModule->GetAlbedoSet().GetAlbedoMap();
			if(albedoMap.has_value() == false)
				continue;
			auto albedoShader = Shader::Create<ShaderAlbedo>(*this,mesh.GetName() +"_albedo");
			albedoShader->SetUVHandlers(shader->GetUVHandlers());
			albedoShader->SetAlphaMode(shader->GetAlphaMode(),shader->GetAlphaCutoff());
			albedoShader->GetAlbedoSet().SetAlbedoMap(*albedoMap);

			auto *spriteSheetModule = dynamic_cast<ShaderModuleSpriteSheet*>(shader.get());
			if(spriteSheetModule && spriteSheetModule->GetSpriteSheetData().has_value())
				albedoShader->SetSpriteSheetData(*spriteSheetModule->GetSpriteSheetData());

			if(albedoModule->GetAlbedoSet2().GetAlbedoMap().has_value())
			{
				albedoShader->GetAlbedoSet2().SetAlbedoMap(*albedoModule->GetAlbedoSet2().GetAlbedoMap());
				albedoShader->SetUseVertexAlphasForBlending(albedoModule->ShouldUseVertexAlphasForBlending());
			}

			auto cclShader = albedoShader->GenerateCCLShader();
			cclShader->Finalize();

			mesh->used_shaders[i +shaderOffset] = **cclShader;
		}
		for(auto i=decltype(mesh->shader.size()){0};i<mesh->shader.size();++i)
			mesh->shader[i] += numShaders;
		mesh->tag_update(&m_scene,false);
	}
}

void cycles::Scene::InitializeNormalPass(bool reloadShaders)
{
	// Also see cycles::Scene::CreateShader
	auto bufferParams = GetBufferParameters();
	uint32_t sampleCount = 1;
	m_session->params.samples = sampleCount;
	m_session->reset(bufferParams,sampleCount); // We only need the normals and albedo colors for the first sample

	// Disable the sky (by making it black)
	auto shader = cycles::Shader::Create<ShaderGeneric>(*this,"clear_sky");
	auto cclShader = shader->GenerateCCLShader();

	auto nodeColor = cclShader->AddColorNode();
	nodeColor.SetColor({0.f,0.f,0.f});
	cclShader->Link(nodeColor.outColor,cclShader->GetOutputNode().inSurface);
	cclShader->Finalize();
	m_scene.default_background = **cclShader;
	(*cclShader)->tag_update(&m_scene);

	if(reloadShaders == false)
		return;
	// Note: For denoising the scene has to be rendered three times:
	// 1) With lighting
	// 2) Albedo colors only
	// 3) Normals only
	// However, Cycles doesn't allow rendering to multiple outputs at once, so we
	// need three separate render passes. For the additional render passes
	// we have to replace the shaders, which is also impossible to do with Cycles.
	// Instead, we have to create an additional set of shaders for each object and
	// re-assign the shader indices of the mesh.
	for(auto &o : m_objects)
	{
		auto &mesh = o->GetMesh();
		auto &subMeshShaders = mesh.GetSubMeshShaders();
		auto numShaders = subMeshShaders.size();
		auto shaderOffset = mesh->used_shaders.size();
		mesh->used_shaders.resize(mesh->used_shaders.size() +numShaders);
		for(auto i=decltype(numShaders){0u};i<numShaders;++i)
		{
			auto &shader = subMeshShaders.at(i);
			auto *normalModule = dynamic_cast<ShaderModuleNormal*>(shader.get());
			if(normalModule == nullptr)
				continue;
			auto &albedoMap = normalModule->GetAlbedoSet().GetAlbedoMap();
			auto &normalMap = normalModule->GetNormalMap();

			auto normalShader = Shader::Create<ShaderNormal>(*this,mesh.GetName() +"_normal");
			normalShader->SetUVHandlers(shader->GetUVHandlers());
			normalShader->SetAlphaMode(shader->GetAlphaMode(),shader->GetAlphaCutoff());
			if(albedoMap.has_value())
			{
				normalShader->GetAlbedoSet().SetAlbedoMap(*albedoMap);
				if(normalModule->GetAlbedoSet2().GetAlbedoMap().has_value())
				{
					normalShader->GetAlbedoSet2().SetAlbedoMap(*normalModule->GetAlbedoSet2().GetAlbedoMap());
					normalShader->SetUseVertexAlphasForBlending(normalModule->ShouldUseVertexAlphasForBlending());
				}
				auto *spriteSheetModule = dynamic_cast<ShaderModuleSpriteSheet*>(shader.get());
				if(spriteSheetModule && spriteSheetModule->GetSpriteSheetData().has_value())
					normalShader->SetSpriteSheetData(*spriteSheetModule->GetSpriteSheetData());
			}
			if(normalMap.has_value())
			{
				normalShader->SetNormalMap(*normalMap);
				normalShader->SetNormalMapSpace(normalModule->GetNormalMapSpace());
			}

			auto cclShader = normalShader->GenerateCCLShader();
			cclShader->Finalize();

			mesh->used_shaders[i +shaderOffset] = **cclShader;
		}
		for(auto i=decltype(mesh->shader.size()){0};i<mesh->shader.size();++i)
			mesh->shader[i] += numShaders;
		mesh->tag_update(&m_scene,false);
	}
}

static void update_cancel(cycles::SceneWorker &worker,ccl::Session &session)
{
	if(worker.IsCancelled())
		session.progress.set_cancel("Cancelled by application.");
}

util::ParallelJob<std::shared_ptr<uimg::ImageBuffer>> cycles::Scene::Finalize()
{
	if(m_sky.empty() == false)
		AddSkybox(m_sky);

	auto bufferParams = GetBufferParameters();

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
	for(auto &cclShader : m_cclShaders)
		cclShader->Finalize();

	enum class RenderProcessResult : uint8_t
	{
		Complete = 0,
		Continue
	};
	auto job = util::create_parallel_job<SceneWorker>(*this);
	auto &worker = static_cast<SceneWorker&>(job.GetWorker());
	auto fRenderThread = [this,&worker](float baseProgress,float progressMultiplier,const std::function<RenderProcessResult()> &fOnComplete) {
		for(;;)
		{
			worker.UpdateProgress(baseProgress +m_session->progress.get_progress() *progressMultiplier);
			update_cancel(worker,*m_session);
			if(m_session->progress.get_cancel())
			{
				Con::cwar<<"WARNING: Cycles rendering has been cancelled: "<<m_session->progress.get_cancel_message()<<Con::endl;
				worker.Cancel(m_session->progress.get_cancel_message());
				break;
			}
			if(m_session->progress.get_error())
			{
				std::string status;
				std::string subStatus;
				m_session->progress.get_status(status,subStatus);
				Con::cwar<<"WARNING: Cycles rendering has failed at status '"<<status<<"' ("<<subStatus<<") with error: "<<m_session->progress.get_error_message()<<Con::endl;
				worker.SetStatus(util::JobStatus::Failed,m_session->progress.get_error_message());
				break;
			}
			if(m_session->progress.get_progress() == 1.f)
				break;
			std::this_thread::sleep_for(std::chrono::seconds{1});
		}
		if(worker.GetStatus() == util::JobStatus::Pending && fOnComplete != nullptr && fOnComplete() == RenderProcessResult::Continue)
			return;
		if(worker.GetStatus() == util::JobStatus::Pending)
			worker.SetStatus(util::JobStatus::Successful);
	};

	if(m_renderMode == RenderMode::RenderImage)
	{
		worker.AddThread([this,&worker,fRenderThread]() {
			m_session->start();

			// Render image with lighting
			auto denoise = umath::is_flag_set(m_stateFlags,StateFlags::DenoiseResult);
			auto progressMultiplier = denoise ? 0.95f : 1.f;
			fRenderThread(0.f,progressMultiplier,[this,&worker,denoise,fRenderThread]() -> RenderProcessResult {
				m_session->wait();
				m_resultImageBuffer = FinalizeCyclesScene();
				ApplyPostProcessing(*m_resultImageBuffer,m_renderMode);
				if(denoise == false)
				{
					CloseCyclesScene();
					return RenderProcessResult::Complete;
				}

				// Render albedo colors (required for denoising)
				m_renderMode = RenderMode::SceneAlbedo;

				InitializeAlbedoPass(true);

				worker.AddThread([this,&worker,fRenderThread]() {
					m_session->start();
					fRenderThread(0.95f,0.025f,[this,&worker,fRenderThread]() -> RenderProcessResult {
						m_session->wait();
						m_albedoImageBuffer = FinalizeCyclesScene();
						ApplyPostProcessing(*m_albedoImageBuffer,m_renderMode);

						// Render albedo colors (required for denoising)
						m_renderMode = RenderMode::SceneNormals;

						InitializeNormalPass(true);

						worker.AddThread([this,&worker,fRenderThread]() {
							m_session->start();
							fRenderThread(0.975f,0.025f,[this,&worker,fRenderThread]() -> RenderProcessResult {
								m_session->wait();
								m_normalImageBuffer = FinalizeCyclesScene();
								ApplyPostProcessing(*m_normalImageBuffer,m_renderMode);

								// Denoise
								cycles::Scene::DenoiseInfo denoiseInfo {};
								denoiseInfo.hdr = m_resultImageBuffer->IsHDRFormat();
								denoiseInfo.width = m_resultImageBuffer->GetWidth();
								denoiseInfo.height = m_resultImageBuffer->GetHeight();

								static auto dbgAlbedo = false;
								static auto dbgNormals = false;
								if(dbgAlbedo)
									m_resultImageBuffer = m_albedoImageBuffer;
								else if(dbgNormals)
									m_resultImageBuffer = m_normalImageBuffer;
								else
								{
									Denoise(denoiseInfo,*m_resultImageBuffer,m_albedoImageBuffer.get(),m_normalImageBuffer.get(),[this,&worker](float progress) -> bool {
										return !worker.IsCancelled();
									});
								}
								CloseCyclesScene();
								return RenderProcessResult::Complete; // End of the line
							});
						});
						worker.Start();
						return RenderProcessResult::Continue;
					});
				});
				worker.Start();
				return RenderProcessResult::Continue;
			});
		});
		return job;
	}
	else if(m_renderMode == RenderMode::SceneAlbedo || m_renderMode == RenderMode::SceneNormals)
	{
		if(m_renderMode == RenderMode::SceneAlbedo)
			InitializeAlbedoPass(false);
		else
			InitializeNormalPass(false);
		worker.AddThread([this,&worker,fRenderThread]() {
			m_session->start();
			fRenderThread(0.f,1.f,[this,&worker,fRenderThread]() -> RenderProcessResult {
				m_session->wait();
				m_resultImageBuffer = FinalizeCyclesScene();
				ApplyPostProcessing(*m_resultImageBuffer,m_renderMode);
				CloseCyclesScene();
				return RenderProcessResult::Complete;
			});
		});
		return job;
	}
	
	// Baking cannot be done with cycles directly, we will have to
	// do some additional steps first.

	worker.AddThread([this,&worker,bufferParams]() {
		auto imgWidth = bufferParams.width;
		auto imgHeight = bufferParams.height;
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

		if(worker.IsCancelled())
			return;

		auto numPixels = imgWidth *imgHeight;
		if(m_bakeTarget.expired())
		{
			worker.SetStatus(util::JobStatus::Failed,"Invalid bake target!");
			return;
		}
		auto obj = m_bakeTarget.lock();
		std::vector<baking::BakePixel> pixelArray;
		pixelArray.resize(numPixels);
		auto bakeLightmaps = (m_renderMode == RenderMode::BakeDiffuseLighting);
		baking::prepare_bake_data(*obj,pixelArray.data(),numPixels,imgWidth,imgHeight,bakeLightmaps);

		if(worker.IsCancelled())
			return;

		auto objectId = obj->GetId();
		ccl::BakeData *bake_data = NULL;
		uint32_t triOffset = 0u;
		// Note: This has been commented because it can cause crashes in some cases. To fix the underlying issue, the mesh for
		// which ao should be baked is now moved to the front so it always has a triangle offset of 0 (See 'SetAOBakeTarget'.).
		/// It would be expected that the triangle offset is relative to the object, but that's not actually the case.
		/// Instead, it seems to be a global triangle offset, so we have to count the number of triangles for all objects
		/// before this one.
		///for(auto i=decltype(objectId){0u};i<objectId;++i)
		///	triOffset += m_objects.at(i)->GetMesh().GetTriangleCount();
		bake_data = m_scene.bake_manager->init(objectId,triOffset /* triOffset */,numPixels);
		baking::populate_bake_data(bake_data,objectId,pixelArray.data(),numPixels);

		if(worker.IsCancelled())
			return;

		worker.UpdateProgress(0.2f);

		m_session->tile_manager.set_samples(m_session->params.samples);
		m_session->reset(const_cast<ccl::BufferParams&>(bufferParams), m_session->params.samples);
		m_session->update_scene();

		auto imgBuffer = uimg::ImageBuffer::Create(imgWidth,imgHeight,uimg::ImageBuffer::Format::RGBA_FLOAT);
		auto r = m_scene.bake_manager->bake(m_scene.device,&m_scene.dscene,&m_scene,m_session->progress,shaderType,bake_pass_filter,bake_data,static_cast<float*>(imgBuffer->GetData()));
		if(r == false)
		{
			worker.SetStatus(util::JobStatus::Failed,"Cycles baking has failed for an unknown reason!");
			return;
		}

		if(worker.IsCancelled())
			return;

		worker.UpdateProgress(0.95f);

		if(worker.IsCancelled())
			return;

		worker.SetResultMessage("Baking margin...");

		baking::ImBuf ibuf {};
		ibuf.x = imgWidth;
		ibuf.y = imgHeight;
		ibuf.rect = imgBuffer;

		// Apply margin
		// TODO: Margin only required for certain bake types?
		std::vector<uint8_t> mask_buffer {};
		mask_buffer.resize(numPixels);
		constexpr auto margin = 16u;
		baking::RE_bake_mask_fill(pixelArray, numPixels, reinterpret_cast<char*>(mask_buffer.data()));
		baking::RE_bake_margin(&ibuf, mask_buffer, margin);

		if(worker.IsCancelled())
			return;

		// Note: Denoising may not work well with baked images, since we can't supply any geometry information,
		// but the result is decent enough as long as the sample count is high.
		if(umath::is_flag_set(m_stateFlags,StateFlags::DenoiseResult))
		{
			/*if(m_renderMode == RenderMode::BakeDiffuseLighting)
			{
			auto lightmapInfo = m_lightmapTargetComponent.valid() ? m_lightmapTargetComponent->GetLightmapInfo() : nullptr;
			if(lightmapInfo)
			{
			auto originalResolution = lightmapInfo->atlasSize;
			// All of the lightmaps have to be denoised individually
			for(auto &rect : lightmapInfo->lightmapAtlas)
			{
			auto x = rect.x +lightmapInfo->borderSize;
			auto y = rect.y +lightmapInfo->borderSize;
			auto w = rect.w -lightmapInfo->borderSize *2;
			auto h = rect.h -lightmapInfo->borderSize *2;
			Vector2 offset {
			x /static_cast<float>(originalResolution),
			(originalResolution -y -h) /static_cast<float>(originalResolution) // Note: y is flipped!
			};
			Vector2 size {
			w /static_cast<float>(originalResolution),
			h /static_cast<float>(originalResolution)
			};

			DenoiseHDRImageArea(
			*imgBuffer,imgWidth,imgHeight,
			umath::clamp<float>(umath::round(offset.x *static_cast<float>(imgWidth)),0.f,imgWidth) +0.001f, // Add a small offset to make sure something like 2.9999 isn't truncated to 2
			umath::clamp<float>(umath::round(offset.y *static_cast<float>(imgHeight)),0.f,imgHeight) +0.001f,
			umath::clamp<float>(umath::round(size.x *static_cast<float>(imgWidth)),0.f,imgWidth) +0.001f,
			umath::clamp<float>(umath::round(size.y *static_cast<float>(imgHeight)),0.f,imgHeight) +0.001f
			);
			}
			}
			}
			else*/
			{
				// TODO: Check if denoise flag is set
				// Denoise the result. This has to be done before applying the margin! (Otherwise noise may flow into the margin)

				worker.SetResultMessage("Baking margin...");
				DenoiseInfo denoiseInfo {};
				denoiseInfo.hdr = true;
				denoiseInfo.lightmap = bakeLightmaps;
				denoiseInfo.width = imgWidth;
				denoiseInfo.height = imgHeight;
				Denoise(denoiseInfo,*imgBuffer,nullptr,nullptr,[this,&worker](float progress) -> bool {
					worker.UpdateProgress(0.95f +progress *0.2f);
					return !worker.IsCancelled();
				});
			}
		}

		if(worker.IsCancelled())
			return;

		ApplyPostProcessing(*imgBuffer,m_renderMode);

		if(worker.IsCancelled())
			return;

		if(umath::is_flag_set(m_stateFlags,StateFlags::OutputResultWithHDRColors) == false)
		{
			// Convert baked data to rgba8
			auto imgBufLDR = imgBuffer->Copy(uimg::ImageBuffer::Format::RGBA_LDR);
			auto numChannels = umath::to_integral(uimg::ImageBuffer::Channel::Count);
			auto itSrc = imgBuffer->begin();
			for(auto &pxViewDst : *imgBufLDR)
			{
				auto &pxViewSrc = *itSrc;
				for(auto i=decltype(numChannels){0u};i<numChannels;++i)
					pxViewDst.SetValue(static_cast<uimg::ImageBuffer::Channel>(i),baking::unit_float_to_uchar_clamp(pxViewSrc.GetFloatValue(static_cast<uimg::ImageBuffer::Channel>(i))));
				++itSrc;
			}
			imgBuffer = imgBufLDR;
		}
		else
		{
			// Image data is float data, but we only need 16 bits for our purposes
			auto imgBufHDR = imgBuffer->Copy(uimg::ImageBuffer::Format::RGBA_HDR);
			auto numChannels = umath::to_integral(uimg::ImageBuffer::Channel::Count);
			auto itSrc = imgBuffer->begin();
			for(auto &pxViewDst : *imgBufHDR)
			{
				auto &pxViewSrc = *itSrc;
				for(auto i=decltype(numChannels){0u};i<numChannels;++i)
					pxViewDst.SetValue(static_cast<uimg::ImageBuffer::Channel>(i),static_cast<uint16_t>(umath::float32_to_float16_glm(pxViewSrc.GetFloatValue(static_cast<uimg::ImageBuffer::Channel>(i)))));
				++itSrc;
			}
			imgBuffer = imgBufHDR;
		}

		if(worker.IsCancelled())
			return;

		m_resultImageBuffer = imgBuffer;
		// m_session->params.write_render_cb(static_cast<ccl::uchar*>(imgBuffer->GetData()),imgWidth,imgHeight,4 /* channels */);
		m_session->params.write_render_cb = nullptr; // Make sure it's not called on destruction
		worker.SetStatus(util::JobStatus::Successful,"Baking has been completed successfully!");
		worker.UpdateProgress(1.f);
	});
	return job;
}

cycles::Scene::RenderMode cycles::Scene::GetRenderMode() const {return m_renderMode;}
float cycles::Scene::GetProgress() const
{
	return m_session->progress.get_progress();
}
void cycles::Scene::OnParallelWorkerCancelled()
{
	m_session->set_pause(true);
}
void cycles::Scene::Wait()
{
	if(m_session)
		m_session->wait();
}

const std::vector<cycles::PShader> &cycles::Scene::GetShaders() const {return const_cast<Scene*>(this)->GetShaders();}
std::vector<cycles::PShader> &cycles::Scene::GetShaders() {return m_shaders;}
const std::vector<cycles::PObject> &cycles::Scene::GetObjects() const {return const_cast<Scene*>(this)->GetObjects();}
std::vector<cycles::PObject> &cycles::Scene::GetObjects() {return m_objects;}
const std::vector<cycles::PLight> &cycles::Scene::GetLights() const {return const_cast<Scene*>(this)->GetLights();}
std::vector<cycles::PLight> &cycles::Scene::GetLights() {return m_lights;}

void cycles::Scene::SetLightIntensityFactor(float f) {m_lightIntensityFactor = f;}
float cycles::Scene::GetLightIntensityFactor() const {return m_lightIntensityFactor;}

void cycles::Scene::SetSky(const std::string &skyPath) {m_sky = skyPath;}
void cycles::Scene::SetSkyAngles(const EulerAngles &angSky) {m_skyAngles = angSky;}
void cycles::Scene::SetSkyStrength(float strength) {m_skyStrength = strength;}
void cycles::Scene::SetEmissionStrength(float strength) {m_emissionStrength = strength;}
void cycles::Scene::SetMaxTransparencyBounces(uint32_t maxBounces) {m_maxTransparencyBounces = maxBounces;}
void cycles::Scene::SetMotionBlurStrength(float strength) {m_motionBlurStrength = strength;}

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

ccl::float3 cycles::Scene::ToCyclesVector(const Vector3 &v)
{
	return ccl::float3{v.x,v.y,v.z};
}

Vector3 cycles::Scene::ToPragmaPosition(const ccl::float3 &pos)
{
	auto scale = util::units_to_metres(1.f);
	Vector3 prPos {pos.x,pos.y,pos.z};
	prPos /= scale;
	return prPos;
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

ccl::Transform cycles::Scene::ToCyclesTransform(const pragma::physics::ScaledTransform &t)
{
	Vector3 axis;
	float angle;
	uquat::to_axis_angle(t.GetRotation(),axis,angle);
	auto cclT = ccl::transform_identity();
	cclT = cclT *ccl::transform_rotate(angle,Scene::ToCyclesNormal(axis));
	cclT = ccl::transform_translate(Scene::ToCyclesPosition(t.GetOrigin())) *cclT;
	cclT = cclT *ccl::transform_scale(Scene::ToCyclesVector(t.GetScale()));
	return cclT;
}

float cycles::Scene::ToCyclesLength(float len)
{
	auto scale = util::units_to_metres(1.f);
	return len *scale;
}
#pragma optimize("",on)
