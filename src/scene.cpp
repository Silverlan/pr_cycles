/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#include "pr_cycles/scene.hpp"
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
#include <render/image.h>
#include <pragma/c_engine.h>
#include <prosper_context.hpp>
#include <buffers/prosper_uniform_resizable_buffer.hpp>
#include <buffers/prosper_dynamic_resizable_buffer.hpp>
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
#include <util_texture_info.hpp>
#include <util_raytracing/scene.hpp>
#include <util_raytracing/shader.hpp>
#include <util_raytracing/ccl_shader.hpp>
#include <util_raytracing/mesh.hpp>
#include <util_raytracing/object.hpp>
#include <util_raytracing/camera.hpp>
#include <texturemanager/texture.h>
#include <cmaterialmanager.h>
#include <datasystem_color.h>
#include <datasystem_vector.h>
#include "pr_cycles/subdivision.hpp"

// ccl happens to have the same include guard name as sharedutils, so we have to undef it here
#undef __UTIL_STRING_H__
#include <sharedutils/util_string.h>

using namespace pragma::modules;

#pragma optimize("",off)

extern DLLCENGINE CEngine *c_engine;
extern DLLCLIENT ClientState *client;
extern DLLCLIENT CGame *c_game;

cycles::Scene::Scene(raytracing::Scene &rtScene)
	: m_rtScene{rtScene.shared_from_this()}
{}

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

void cycles::Scene::AddRoughnessMapImageTextureNode(raytracing::ShaderModuleRoughness &shader,Material &mat,float defaultRoughness) const
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

	auto rmaPath = prepare_texture(***m_rtScene,mat.GetRMAMap(),PreparedTextureInputFlags::None,nullptr,"white");
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

raytracing::PShader cycles::Scene::CreateShader(Material &mat,const std::string &meshName,const ShaderInfo &shaderInfo) const
{
	// Make sure all textures have finished loading
	static_cast<CMaterialManager&>(client->GetMaterialManager()).GetTextureManager().WaitForTextures();

	TextureInfo *diffuseMap = nullptr;
	if(ustring::compare(mat.GetShaderIdentifier(),"skybox",false))
		diffuseMap = mat.GetTextureInfo("skybox");
	else
		diffuseMap = mat.GetDiffuseMap();
	PreparedTextureOutputFlags flags;
	auto diffuseTexPath = prepare_texture(***m_rtScene,diffuseMap,PreparedTextureInputFlags::CanBeEnvMap,&flags);
	if(diffuseTexPath.has_value() == false)
		return nullptr;

	std::optional<std::string> albedo2TexPath = {};
	if(ustring::compare(mat.GetShaderIdentifier(),"pbr_blend",false))
	{
		auto *albedo2Map = mat.GetTextureInfo(Material::ALBEDO_MAP2_IDENTIFIER);
		if(albedo2Map)
			albedo2TexPath = prepare_texture(***m_rtScene,albedo2Map,PreparedTextureInputFlags::None);
	}

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

	auto fApplyColorFactor = [&mat](raytracing::ShaderAlbedoSet &albedoSet) {
		auto &colorFactor = mat.GetDataBlock()->GetValue("color_factor");
		if(colorFactor == nullptr || typeid(*colorFactor) != typeid(ds::Vector4))
			return;
		auto &color = static_cast<ds::Vector4*>(colorFactor.get())->GetValue();
		albedoSet.SetColorFactor(color);
	};

	raytracing::PShader resShader = nullptr;
	auto renderMode = m_rtScene->GetRenderMode();
	if(renderMode == raytracing::Scene::RenderMode::SceneAlbedo)
	{
		auto shader = raytracing::Shader::Create<raytracing::ShaderAlbedo>(*m_rtScene,meshName +"_shader");
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
	else if(renderMode == raytracing::Scene::RenderMode::SceneNormals)
	{
		auto shader = raytracing::Shader::Create<raytracing::ShaderNormal>(*m_rtScene,meshName +"_shader");
		shader->SetMeshName(meshName);
		shader->GetAlbedoSet().SetAlbedoMap(*diffuseTexPath);
		if(albedo2TexPath.has_value())
		{
			shader->GetAlbedoSet2().SetAlbedoMap(*albedo2TexPath);
			shader->SetUseVertexAlphasForBlending(true);
		}
		shader->SetAlphaMode(mat.GetAlphaMode(),mat.GetAlphaCutoff());
		auto normalTexPath = prepare_texture(***m_rtScene,mat.GetNormalMap(),PreparedTextureInputFlags::None);
		if(normalTexPath.has_value())
			shader->SetNormalMap(*normalTexPath);
		resShader = shader;
	}
	else if(renderMode == raytracing::Scene::RenderMode::SceneDepth)
	{
		auto shader = raytracing::Shader::Create<raytracing::ShaderDepth>(*m_rtScene,meshName +"_shader");
		shader->SetMeshName(meshName);
		static_cast<raytracing::ShaderDepth&>(*shader).SetFarZ(m_rtScene->GetCamera().GetFarZ());
		shader->GetAlbedoSet().SetAlbedoMap(*diffuseTexPath);
		if(albedo2TexPath.has_value())
		{
			shader->GetAlbedoSet2().SetAlbedoMap(*albedo2TexPath);
			shader->SetUseVertexAlphasForBlending(true);
		}
		shader->SetAlphaMode(mat.GetAlphaMode(),mat.GetAlphaCutoff());
		resShader = shader;
	}
	else
	{
		std::string cyclesShader = "pbr";
		auto &dataBlock = mat.GetDataBlock();
		auto cyclesBlock = dataBlock->GetBlock("cycles");
		std::shared_ptr<ds::Block> cyclesShaderPropBlock = nullptr;
		if(cyclesBlock)
		{
			cyclesShader = cyclesBlock->GetString("shader","pbr");
			cyclesShaderPropBlock = cyclesBlock->GetBlock("shader_properties");
		}
		std::optional<float> ior {};
		float f;
		if(dataBlock->GetFloat("ior",&f))
			ior = f;

		if(ustring::compare(cyclesShader,"toon",false))
		{
			auto shader = raytracing::Shader::Create<raytracing::ShaderToon>(*m_rtScene,meshName +"_shader");
			fApplyColorFactor(shader->GetAlbedoSet());
			shader->SetMeshName(meshName);
			shader->GetAlbedoSet().SetAlbedoMap(*diffuseTexPath);
			if(albedo2TexPath.has_value())
			{
				shader->GetAlbedoSet2().SetAlbedoMap(*albedo2TexPath);
				shader->SetUseVertexAlphasForBlending(true);
			}
			
			auto normalTexPath = prepare_texture(***m_rtScene,mat.GetNormalMap(),PreparedTextureInputFlags::None);
			if(normalTexPath.has_value())
				shader->SetNormalMap(*normalTexPath);

			if(cyclesShaderPropBlock)
			{
				auto *shaderToon = static_cast<raytracing::ShaderToon*>(shader.get());
				float value;
				if(cyclesShaderPropBlock->GetFloat("diffuse_size",&value))
					shaderToon->SetDiffuseSize(value);
				if(cyclesShaderPropBlock->GetFloat("diffuse_smooth",&value))
					shaderToon->SetDiffuseSmooth(value);
				if(cyclesShaderPropBlock->GetFloat("specular_size",&value))
					shaderToon->SetSpecularSize(value);
				if(cyclesShaderPropBlock->GetFloat("specular_smooth",&value))
					shaderToon->SetSpecularSmooth(value);

				auto &dvSpecFactor = cyclesShaderPropBlock->GetValue("specular_color");
				if(dvSpecFactor != nullptr && typeid(*dvSpecFactor) == typeid(ds::Color))
				{
					auto &color = static_cast<ds::Color&>(*dvSpecFactor).GetValue();
					shader->SetSpecularColor(color.ToVector3());
				}

				auto &dvShadeColor = cyclesShaderPropBlock->GetValue("shade_color");
				if(dvShadeColor != nullptr && typeid(*dvShadeColor) == typeid(ds::Color))
				{
					auto &color = static_cast<ds::Color&>(*dvShadeColor).GetValue();
					shader->SetShadeColor(color.ToVector3());
				}
			}

			resShader = shader;
		}
		else if(ustring::compare(cyclesShader,"glass",false))
		{
			auto shader = raytracing::Shader::Create<raytracing::ShaderGlass>(*m_rtScene,meshName +"_shader");
			fApplyColorFactor(shader->GetAlbedoSet());
			shader->SetMeshName(meshName);
			shader->GetAlbedoSet().SetAlbedoMap(*diffuseTexPath);
			if(albedo2TexPath.has_value())
			{
				shader->GetAlbedoSet2().SetAlbedoMap(*albedo2TexPath);
				shader->SetUseVertexAlphasForBlending(true);
			}
			auto normalTexPath = prepare_texture(***m_rtScene,mat.GetNormalMap(),PreparedTextureInputFlags::None);
			if(normalTexPath.has_value())
				shader->SetNormalMap(*normalTexPath);

			// Roughness map
			AddRoughnessMapImageTextureNode(*shader,mat,0.5f);

			if(ior.has_value())
				shader->SetIOR(*ior);

			resShader = shader;
		}
		else
		{
			std::shared_ptr<raytracing::ShaderPBR> shader = nullptr;
			auto isParticleSystemShader = (shaderInfo.particleSystem.has_value() && shaderInfo.particle.has_value());
			if(isParticleSystemShader)
			{
				shader = raytracing::Shader::Create<raytracing::ShaderParticle>(*m_rtScene,meshName +"_shader");
				auto *ptShader = static_cast<raytracing::ShaderParticle*>(shader.get());
				auto *pt = static_cast<const pragma::CParticleSystemComponent::ParticleData*>(*shaderInfo.particle);
				Color color {static_cast<int16_t>(pt->color.at(0)),static_cast<int16_t>(pt->color.at(1)),static_cast<int16_t>(pt->color.at(2)),static_cast<int16_t>(pt->color.at(3))};
				ptShader->SetColor(color);

				auto *pShader = static_cast<pragma::ShaderParticle*>(c_engine->GetShader("particle").get());
				if(pShader)
				{
					auto renderFlags = ShaderParticle::RenderFlags::None;
					auto ptcFlags = pShader->GetRenderFlags(**shaderInfo.particleSystem,pragma::ParticleRenderFlags::None);
					if((*shaderInfo.particleSystem)->GetEffectiveAlphaMode() == pragma::ParticleAlphaMode::AdditiveByColor)
						renderFlags |= ShaderParticle::RenderFlags::AdditiveBlendByColor;
				}
			}
			else
				shader = raytracing::Shader::Create<raytracing::ShaderPBR>(*m_rtScene,meshName +"_shader");
			fApplyColorFactor(shader->GetAlbedoSet());
			shader->SetMeshName(meshName);

			auto data = mat.GetDataBlock();
			auto dataSSS = data->GetBlock("subsurface_scattering");
			if(dataSSS)
			{
				float subsurface;
				if(dataSSS->GetFloat("factor",&subsurface))
					shader->SetSubsurface(subsurface);

				auto &dvSubsurfaceColor = dataSSS->GetValue("color_factor");
				if(dvSubsurfaceColor != nullptr && typeid(*dvSubsurfaceColor) == typeid(ds::Vector))
				{
					auto &color = static_cast<ds::Vector&>(*dvSubsurfaceColor).GetValue();
					shader->SetSubsurfaceColorFactor(color);
				}

				const std::unordered_map<std::string,raytracing::PrincipledBSDFNode::SubsurfaceMethod> bssrdfToCclType = {
					{"cubic",raytracing::PrincipledBSDFNode::SubsurfaceMethod::Cubic},
					{"gaussian",raytracing::PrincipledBSDFNode::SubsurfaceMethod::Gaussian},
					{"principled",raytracing::PrincipledBSDFNode::SubsurfaceMethod::Principled},
					{"burley",raytracing::PrincipledBSDFNode::SubsurfaceMethod::Burley},
					{"random_walk",raytracing::PrincipledBSDFNode::SubsurfaceMethod::RandomWalk},
					{"principled_random_walk",raytracing::PrincipledBSDFNode::SubsurfaceMethod::PrincipledRandomWalk}
				};

				std::string subsurfaceMethod;
				if(dataSSS->GetString("method",&subsurfaceMethod))
				{
					auto it = bssrdfToCclType.find(subsurfaceMethod);
					if(it != bssrdfToCclType.end())
						shader->SetSubsurfaceMethod(it->second);
				}

				auto &dvSubsurfaceRadius = dataSSS->GetValue("scatter_color");
				if(dvSubsurfaceRadius != nullptr && typeid(*dvSubsurfaceRadius) == typeid(ds::Color))
					shader->SetSubsurfaceRadius(static_cast<ds::Color&>(*dvSubsurfaceRadius).GetValue().ToVector3());
			}
			//

			// Note: We always need the albedo texture information for the translucency.
			// Whether metalness/roughness/etc. affect baking in any way is unclear (probably not),
			// but it also doesn't hurt to have them.

			if(ior.has_value())
				shader->SetIOR(*ior);

			// Albedo map
			shader->GetAlbedoSet().SetAlbedoMap(*diffuseTexPath);
			if(albedo2TexPath.has_value())
			{
				shader->GetAlbedoSet2().SetAlbedoMap(*albedo2TexPath);
				shader->SetUseVertexAlphasForBlending(true);
			}
			if(mat.GetAlphaMode() != AlphaMode::Opaque && data->GetBool("black_to_alpha"))
				shader->SetFlags(raytracing::Shader::Flags::AdditiveByColor,true);

			// Normal map
			auto normalTexPath = prepare_texture(***m_rtScene,mat.GetNormalMap(),PreparedTextureInputFlags::None);
			if(normalTexPath)
				shader->SetNormalMap(*normalTexPath);

			// Metalness map
			auto metalnessTexPath = prepare_texture(***m_rtScene,mat.GetRMAMap(),PreparedTextureInputFlags::None,nullptr,"white");
			if(metalnessTexPath)
				shader->SetMetalnessMap(*metalnessTexPath);

			// If no metalness map is available, just use metalness factor directly
			float metalnessFactor;
			if(mat.GetDataBlock()->GetFloat("metalness_factor",&metalnessFactor))
				shader->SetMetalnessFactor(metalnessFactor);

			// Roughness map
			AddRoughnessMapImageTextureNode(*shader,mat,0.5f);

			// Emission map
			auto globalEmissionStrength = m_rtScene->GetEmissionStrength();
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
				auto emissionTexPath = prepare_texture(***m_rtScene,emissionTex,PreparedTextureInputFlags::None);
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
			auto wrinkleStretchMap = prepare_texture(***m_rtScene,mat.GetTextureInfo(Material::WRINKLE_STRETCH_MAP_IDENTIFIER),PreparedTextureInputFlags::None);
			if(wrinkleStretchMap)
				shader->SetWrinkleStretchMap(*wrinkleStretchMap);

			auto wrinkleCompressMap = prepare_texture(***m_rtScene,mat.GetTextureInfo(Material::WRINKLE_COMPRESS_MAP_IDENTIFIER),PreparedTextureInputFlags::None);
			if(wrinkleCompressMap)
				shader->SetWrinkleCompressMap(*wrinkleCompressMap);
			resShader = shader;
		}
		if(resShader)
		{
			resShader->SetAlphaMode(mat.GetAlphaMode(),mat.GetAlphaCutoff());
			auto &dataBlock = mat.GetDataBlock();
			float alphaFactor;
			if(dataBlock->GetFloat("alpha_factor",&alphaFactor))
				resShader->SetAlphaFactor(alphaFactor);
		}
	}
	if(resShader && shaderInfo.entity.has_value() && shaderInfo.subMesh.has_value())
	{
		auto normalMapSpace = raytracing::NormalMapNode::Space::Tangent;
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
						auto uvHandler = std::make_shared<raytracing::UVHandlerEye>(irisProjU,irisProjV,dilationFactor,maxDilationFactor,irisUvRadius);
						resShader->SetUVHandler(raytracing::Shader::TextureType::Albedo,uvHandler);
						resShader->SetUVHandler(raytracing::Shader::TextureType::Emission,uvHandler);
					}
				}
			}
		}

		auto *normalModule = dynamic_cast<raytracing::ShaderModuleNormal*>(resShader.get());
		if(normalModule)
			normalModule->SetNormalMapSpace(normalMapSpace);
	}
	return resShader;
}
raytracing::PShader cycles::Scene::CreateShader(const std::string &meshName,Model &mdl,ModelSubMesh &subMesh,BaseEntity *optEnt,uint32_t skinId) const
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
	return CreateShader(*mat,meshName,shaderInfo);
}

void cycles::Scene::SetAOBakeTarget(Model &mdl,uint32_t matIndex)
{
	std::vector<std::shared_ptr<MeshData>> materialMeshes;
	std::vector<std::shared_ptr<MeshData>> envMeshes;
	AddModel(mdl,"ao_mesh",nullptr,0 /* skin */,nullptr,nullptr,nullptr,[this,matIndex,&materialMeshes,&envMeshes,&mdl](ModelSubMesh &mesh,const Vector3 &origin,const Quat &rot) -> bool {
		auto meshData = CalcMeshData(mdl,mesh,false,false);
		meshData->shader = CreateShader(GetUniqueName(),mdl,mesh);
		auto texIdx = mdl.GetMaterialIndex(mesh);
		if(texIdx.has_value() && *texIdx == matIndex)
		{
			materialMeshes.push_back(meshData);
			return false;
		}
		envMeshes.push_back(meshData);
		return false;
	});

	// We'll create a separate mesh from all model meshes which use the specified material.
	// This way we can map the uv coordinates to the ao output texture more easily.
	auto mesh = BuildMesh("ao_target",materialMeshes);
	auto o = raytracing::Object::Create(*m_rtScene,*mesh);
	m_rtScene->SetAOBakeTarget(*o);

	if(envMeshes.empty())
		return;

	// Note: Ambient occlusion is baked for a specific material (matIndex). The model may contain meshes that use a different material,
	// in which case those meshes are still needed to render accurate ambient occlusion values near edge cases.
	// To distinguish them from the actual ao-meshes, they're stored in a separate mesh/object here.
	// The actual ao bake target (see code above) has to be the first mesh added to the scene, otherwise the ao result may be incorrect.
	// The reason for this is currently unknown.
	auto meshEnv = BuildMesh("ao_mesh",envMeshes);
	raytracing::Object::Create(*m_rtScene,*meshEnv);
}

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
				cclLightmapUvs.at(uvOffset +i) = raytracing::Scene::ToCyclesUV(uvSet->at(idx0));
				cclLightmapUvs.at(uvOffset +i +1) = raytracing::Scene::ToCyclesUV(uvSet->at(idx1));
				cclLightmapUvs.at(uvOffset +i +2) = raytracing::Scene::ToCyclesUV(uvSet->at(idx2));
			}
		}
		uvOffset += tris.size();
	}
	mesh.SetLightmapUVs(std::move(cclLightmapUvs));
	m_rtScene->SetAOBakeTarget(*o);
}

std::shared_ptr<cycles::Scene::MeshData> cycles::Scene::CalcMeshData(Model &mdl,ModelSubMesh &mdlMesh,bool includeAlphas,bool includeWrinkles,pragma::CModelComponent *optMdlC,pragma::CAnimatedComponent *optAnimC)
{
	auto meshData = std::make_shared<MeshData>();
	auto &meshVerts = mdlMesh.GetVertices();
	auto &meshAlphas = mdlMesh.GetAlphas();

	std::vector<Vertex> transformedVerts {};
	transformedVerts.reserve(meshVerts.size());
	
	std::optional<std::vector<float>> alphas {};
	if(includeAlphas)
	{
		alphas = std::vector<float>{};
		alphas->reserve(meshVerts.size());
	}

	std::optional<std::vector<float>> wrinkles {};
	if(includeWrinkles)
	{
		wrinkles = std::vector<float>{};
		wrinkles->reserve(meshVerts.size());
	}

	for(auto vertIdx=decltype(meshVerts.size()){0u};vertIdx<meshVerts.size();++vertIdx)
	{
		auto &v = meshVerts.at(vertIdx);
		if(m_rtScene->IsRenderSceneMode(m_rtScene->GetRenderMode()))
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

				transformedVerts.push_back({});
				auto &vTransformed = transformedVerts.back();

				auto &pos = vTransformed.position;
				pos = {vpos.x,vpos.y,vpos.z};
				pos /= vpos.w;

				auto &n = vTransformed.normal;
				n = {vn.x,vn.y,vn.z};
				n += normalOffset;
				uvec::normalize(&n);

				auto &t = vTransformed.tangent;
				t = {vt.x,vt.y,vt.z};
				t += normalOffset;
				uvec::normalize(&t);

				vTransformed.uv = v.uv;
			}
			else
				transformedVerts.push_back(v);
			if(includeWrinkles)
				wrinkles->push_back(wrinkle);
		}
		else
		{
			// We're probably baking something (e.g. ao map), so we don't want to include the entity's animated pose.
			transformedVerts.push_back(v);
		}
		
		if(includeAlphas)
		{
			auto alpha = (vertIdx < meshAlphas.size()) ? meshAlphas.at(vertIdx).x : 0.f;
			meshData->alphas->push_back(alpha);
		}
	}

	auto &meshTris = mdlMesh.GetTriangles();
	std::vector<int32_t> indices;
	indices.reserve(meshTris.size());
	for(auto idx : meshTris)
		indices.push_back(idx);

	// Subdivision
	auto applySubdivision = true;

	static auto subdivisionEnabled = false;//true;
	if(subdivisionEnabled == false)
		applySubdivision = false;

	if(applySubdivision)
	{
		std::vector<std::shared_ptr<BaseChannelData>> customAttributes {};
		customAttributes.reserve(2);

		std::vector<float> perFaceAlphaData {};
		if(alphas.has_value())
		{
			auto alphaData = std::make_shared<ChannelData<OsdFloatAttr>>([&perFaceAlphaData](BaseChannelData &cd,FaceVertexIndex faceVertexIndex,Vertex &v,int idx) {
				perFaceAlphaData.at(faceVertexIndex) = static_cast<OsdFloatAttr*>(cd.GetElementPtr(idx))->value;
			},[&perFaceAlphaData](uint32_t numFaces) {
				perFaceAlphaData.resize(numFaces *3);
			});
			alphaData->ReserveBuffer(meshData->vertices.size());

			for(auto alpha : *alphas)
				alphaData->buffer.push_back(alpha);
			customAttributes.push_back(alphaData);
		}
		
		std::vector<float> perFaceWrinkleData {};
		if(wrinkles.has_value())
		{
			auto wrinkleData = std::make_shared<ChannelData<OsdFloatAttr>>([&perFaceWrinkleData](BaseChannelData &cd,FaceVertexIndex faceVertexIndex,Vertex &v,int idx) {
				perFaceWrinkleData.at(faceVertexIndex) = static_cast<OsdFloatAttr*>(cd.GetElementPtr(idx))->value;
			},[&perFaceWrinkleData](uint32_t numFaces) {
				perFaceWrinkleData.resize(numFaces *3);
			});
			wrinkleData->ReserveBuffer(meshData->vertices.size());

			for(auto wrinkle : *wrinkles)
				wrinkleData->buffer.push_back(wrinkle);
			customAttributes.push_back(wrinkleData);
		}
		subdivide_mesh(transformedVerts,indices,meshData->vertices,meshData->triangles,2 /* subDivLevel */,customAttributes);

		if(alphas.has_value())
		{
			meshData->alphas = std::vector<float>{};
			meshData->alphas->resize(meshData->vertices.size());
			for(auto i=decltype(meshData->triangles.size()){0u};i<meshData->triangles.size();++i)
			{
				auto idx = meshData->triangles.at(i);
				meshData->alphas->at(idx) = perFaceAlphaData.at(i);
			}
		}
		if(wrinkles.has_value())
		{
			meshData->wrinkles = std::vector<float>{};
			meshData->wrinkles->resize(meshData->vertices.size());
			for(auto i=decltype(meshData->triangles.size()){0u};i<meshData->triangles.size();++i)
			{
				auto idx = meshData->triangles.at(i);
				meshData->wrinkles->at(idx) = perFaceWrinkleData.at(i);
			}
		}
	}
	else
	{
		meshData->vertices = std::move(transformedVerts);
		meshData->triangles = std::move(indices);
		if(alphas.has_value())
			meshData->alphas = std::move(*alphas);
		if(wrinkles.has_value())
			meshData->wrinkles = std::move(*wrinkles);
	}
	return meshData;
}

void cycles::Scene::AddMesh(Model &mdl,raytracing::Mesh &mesh,ModelSubMesh &mdlMesh,pragma::CModelComponent *optMdlC,pragma::CAnimatedComponent *optAnimC)
{
	auto meshData = CalcMeshData(mdl,mdlMesh,mesh.HasAlphas(),mesh.HasWrinkles(),optMdlC,optAnimC);
	if(meshData == nullptr)
		return;
	AddMeshDataToMesh(mesh,*meshData);
}

void cycles::Scene::AddMeshDataToMesh(raytracing::Mesh &mesh,const MeshData &meshData) const
{
	auto triIndexVertexOffset = mesh.GetVertexOffset();
	auto shaderIdx = mesh.AddSubMeshShader(*meshData.shader);
	for(auto &v : meshData.vertices)
		mesh.AddVertex(v.position,v.normal,v.tangent,v.uv);
	
	for(auto i=decltype(meshData.triangles.size()){0u};i<meshData.triangles.size();i+=3)
		mesh.AddTriangle(triIndexVertexOffset +meshData.triangles.at(i),triIndexVertexOffset +meshData.triangles.at(i +1),triIndexVertexOffset +meshData.triangles.at(i +2),shaderIdx);

	if(meshData.wrinkles.has_value())
	{
		for(auto wrinkle : *meshData.wrinkles)
			mesh.AddWrinkleFactor(wrinkle);
	}
	if(meshData.alphas.has_value())
	{
		for(auto alpha : *meshData.alphas)
			mesh.AddAlpha(alpha);
	}
}

raytracing::PMesh cycles::Scene::BuildMesh(const std::string &meshName,const std::vector<std::shared_ptr<MeshData>> &meshDatas) const
{
	uint64_t numVerts = 0;
	uint64_t numTris = 0;
	auto hasAlphas = false;
	auto hasWrinkles = false;
	for(auto &meshData : meshDatas)
	{
		numVerts += meshData->vertices.size();
		numTris += meshData->triangles.size();
		hasAlphas = hasAlphas || meshData->alphas.has_value();
		hasWrinkles = hasWrinkles || meshData->wrinkles.has_value();
	}

	auto flags = raytracing::Mesh::Flags::None;
	if(hasAlphas)
		flags |= raytracing::Mesh::Flags::HasAlphas;
	if(hasWrinkles)
		flags |= raytracing::Mesh::Flags::HasWrinkles;
	auto mesh = raytracing::Mesh::Create(*m_rtScene,meshName,numVerts,numTris /3,flags);
	for(auto &meshData : meshDatas)
		AddMeshDataToMesh(*mesh,*meshData);
	return mesh;
}

raytracing::PMesh cycles::Scene::AddMeshList(
	Model &mdl,const std::vector<std::shared_ptr<ModelMesh>> &meshList,const std::string &meshName,BaseEntity *optEnt,uint32_t skinId,
	pragma::CModelComponent *optMdlC,pragma::CAnimatedComponent *optAnimC,
	const std::function<bool(ModelMesh&,const Vector3&,const Quat&)> &optMeshFilter,
	const std::function<bool(ModelSubMesh&,const Vector3&,const Quat&)> &optSubMeshFilter
)
{
	Vector3 origin {};
	auto rot = uquat::identity();
	if(optEnt)
	{
		origin = optEnt->GetPosition();
		rot = optEnt->GetRotation();
	}
	auto hasAlphas = false;
	auto hasWrinkles = (mdl.GetVertexAnimations().empty() == false); // TODO: Not the best way to determine if the entity uses wrinkles
	std::vector<std::shared_ptr<MeshData>> meshDatas {};
	meshDatas.reserve(meshList.size());
	for(auto &mesh : meshList)
	{
		if(optMeshFilter != nullptr && optMeshFilter(*mesh,origin,rot) == false)
			continue;
		for(auto &subMesh : mesh->GetSubMeshes())
		{
			if(subMesh->GetGeometryType() != ModelSubMesh::GeometryType::Triangles || subMesh->GetTriangleCount() == 0 || (optSubMeshFilter != nullptr && optSubMeshFilter(*subMesh,origin,rot) == false))
				continue;
			hasAlphas = hasAlphas || (subMesh->GetAlphaCount() > 0);

			auto meshData = CalcMeshData(mdl,*subMesh,hasAlphas,hasWrinkles,optMdlC,optAnimC);
			meshData->shader = CreateShader(GetUniqueName(),mdl,*subMesh,optEnt,skinId);
			meshDatas.push_back(meshData);
		}
	}

	if(meshDatas.empty())
		return nullptr;
	return BuildMesh(meshName,meshDatas);
}

raytracing::PMesh cycles::Scene::AddModel(
	Model &mdl,const std::string &meshName,BaseEntity *optEnt,uint32_t skinId,
	pragma::CModelComponent *optMdlC,pragma::CAnimatedComponent *optAnimC,
	const std::function<bool(ModelMesh&,const Vector3&,const Quat&)> &optMeshFilter,const std::function<bool(ModelSubMesh&,const Vector3&,const Quat&)> &optSubMeshFilter
)
{
	std::vector<std::shared_ptr<ModelMesh>> lodMeshes {};
	std::vector<uint32_t> bodyGroups {};
	bodyGroups.resize(mdl.GetBodyGroupCount());
	mdl.GetBodyGroupMeshes(bodyGroups,0,lodMeshes);
	return AddMeshList(mdl,lodMeshes,meshName,optEnt,skinId,optMdlC,optAnimC,optMeshFilter,optSubMeshFilter);
}

raytracing::PObject cycles::Scene::AddEntity(
	BaseEntity &ent,std::vector<ModelSubMesh*> *optOutTargetMeshes,
	const std::function<bool(ModelMesh&,const Vector3&,const Quat&)> &meshFilter,const std::function<bool(ModelSubMesh&,const Vector3&,const Quat&)> &subMeshFilter,
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

	raytracing::PMesh mesh = nullptr;
	auto &mdlName = mdl->GetName();
	auto shouldCache = false;//true;
	if(ent.IsWorld())
		shouldCache = false;
	if(mdlC->GetMaterialOverrides().empty() == false)
		shouldCache = false; // Don't use cache if the entity uses material overrides
	if(animC.valid() && animC->GetAnimation() != -1)
		shouldCache = false; // Don't use cache if the entity is animated

	auto skin = mdlC->GetSkin();
	if(shouldCache)
	{
		auto it = m_modelCache.find(mdlName);
		if(it != m_modelCache.end())
		{
			auto itInstance = std::find_if(it->second.begin(),it->second.end(),[skin](const ModelCacheInstance &instance) {
				return instance.skin == skin;
			});
			if(itInstance != it->second.end())
				mesh = itInstance->mesh;
		}
	}
	if(mesh == nullptr)
	{
		std::string name = "ent" +nameSuffix +"_" +std::to_string(ent.GetLocalIndex());
		std::vector<ModelSubMesh*> tmpTargetMeshes {};
		auto *targetMeshes = (optOutTargetMeshes != nullptr) ? optOutTargetMeshes : &tmpTargetMeshes;
		targetMeshes->reserve(mdl->GetSubMeshCount());

		auto skyC = ent.GetComponent<CSkyboxComponent>();
		if(skyC.valid())
		{
			AddModel(*mdl,name,&ent,ent.GetSkin(),mdlC,animC.get(),meshFilter,[&targetMeshes,&subMeshFilter](ModelSubMesh &mesh,const Vector3 &origin,const Quat &rot) -> bool {
				if(subMeshFilter && subMeshFilter(mesh,origin,rot) == false)
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
				auto diffuseTexPath = prepare_texture(***m_rtScene,diffuseMap,PreparedTextureInputFlags::CanBeEnvMap,&flags);
				if(diffuseTexPath.has_value() == false || umath::is_flag_set(flags,PreparedTextureOutputFlags::Envmap) == false)
					continue;
				skyboxTexture = diffuseTexPath;
			}
			if(skyboxTexture.has_value())
				m_rtScene->SetSky(*skyboxTexture);
			return nullptr;
		}

		auto fFilterMesh = [&targetMeshes,&subMeshFilter](ModelSubMesh &mesh,const Vector3 &origin,const Quat &rot) -> bool {
			if(subMeshFilter && subMeshFilter(mesh,origin,rot) == false)
				return false;
			targetMeshes->push_back(&mesh);
			return true;
		};

		auto renderC = ent.GetComponent<pragma::CRenderComponent>();
		if(renderC.valid())
			mesh = AddMeshList(*mdl,renderC->GetLODMeshes(),name,&ent,ent.GetSkin(),mdlC,animC.get(),meshFilter,fFilterMesh);
		else
			mesh = AddModel(*mdl,name,&ent,ent.GetSkin(),mdlC,animC.get(),meshFilter,fFilterMesh);
		if(mesh == nullptr)
			return nullptr;
	}

	if(mdlName.empty() == false)
	{
		if(shouldCache)
		{
			auto it = m_modelCache.find(mdlName);
			if(it == m_modelCache.end())
				it = m_modelCache.insert(std::make_pair(mdlName,std::vector<ModelCacheInstance>{})).first;
			it->second.push_back({mesh,skin});
		}
	}

	// Create the object using the mesh
	umath::ScaledTransform t;
	ent.GetPose(t);
	auto o = raytracing::Object::Create(*m_rtScene,*mesh);
	auto renderMode = m_rtScene->GetRenderMode();
	if(m_rtScene->IsRenderSceneMode(renderMode) || renderMode == raytracing::Scene::RenderMode::BakeDiffuseLighting)
	{
		o->SetPos(t.GetOrigin());
		o->SetRotation(t.GetRotation());
		o->SetScale(t.GetScale());
	}
	return o;
}
#pragma optimize("",on)
