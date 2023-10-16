/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

#undef __SCENE_H__
#include "pr_cycles/scene.hpp"
#include "pr_cycles/subdivision.hpp"
#include <prosper_context.hpp>
#include <prosper_util.hpp>
#include <cmaterialmanager.h>
#include <cmaterial.h>
#include <util_raytracing/mesh.hpp>
#include <util_raytracing/object.hpp>
#include <util_raytracing/model_cache.hpp>
#include <pragma/c_engine.h>
#include <pragma/clientstate/clientstate.h>
#include <pragma/game/game_resources.hpp>
#include <pragma/model/model.h>
#include <pragma/model/modelmesh.h>
#include <pragma/entities/baseentity.h>
#include <pragma/entities/entity_component_system_t.hpp>
#include <pragma/entities/components/c_animated_component.hpp>
#include <pragma/entities/components/c_model_component.hpp>
#include <pragma/entities/components/c_render_component.hpp>
#include <pragma/entities/c_skybox.h>
#include <pragma/rendering/shaders/c_shader_cubemap_to_equirectangular.hpp>
#include <cmaterialmanager.h>
#include <cmaterial_manager2.hpp>
#include <sharedutils/util_file.h>
#include <sharedutils/util_hair.hpp>
#undef __UTIL_STRING_H__
#include <sharedutils/util_string.h>
#include <util_texture_info.hpp>

extern DLLCLIENT CEngine *c_engine;
extern DLLCLIENT ClientState *client;
extern DLLCLIENT CGame *c_game;

enum class PreparedTextureInputFlags : uint8_t { None = 0u, CanBeEnvMap = 1u };
REGISTER_BASIC_BITWISE_OPERATORS(PreparedTextureInputFlags)
enum class PreparedTextureOutputFlags : uint8_t { None = 0u, Envmap = 1u };
REGISTER_BASIC_BITWISE_OPERATORS(PreparedTextureOutputFlags)

static std::optional<std::string> get_abs_error_texture_path()
{
	std::string errTexPath = "materials\\error.dds";
	std::string absPath;
	if(FileManager::FindAbsolutePath(errTexPath, absPath) == false)
		return absPath;
	return {};
}

static std::optional<std::string> prepare_texture(TextureInfo *texInfo, bool &outSuccess, bool &outConverted, PreparedTextureInputFlags inFlags, PreparedTextureOutputFlags *optOutFlags = nullptr, const std::optional<std::string> &defaultTexture = {})
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
	if(tex == nullptr || tex->IsLoaded() == false) {
		tex = nullptr;
		if(defaultTexture.has_value()) {
			auto &texManager = static_cast<msys::CMaterialManager &>(client->GetMaterialManager()).GetTextureManager();
			auto ptrTex = texManager.LoadAsset(*defaultTexture);
			if(ptrTex != nullptr) {
				texName = *defaultTexture;
				tex = ptrTex;
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
	if(isCubemap) {
		if(umath::is_flag_set(inFlags, PreparedTextureInputFlags::CanBeEnvMap) == false)
			return {};
		// Image is a cubemap, which Cycles doesn't support! We'll have to convert it to a equirectangular image and use that instead.
		auto &shader = static_cast<pragma::ShaderCubemapToEquirectangular &>(*c_engine->GetShader("cubemap_to_equirectangular"));
		auto equiRectMap = shader.CubemapToEquirectangularTexture(*vkTex);
		vkTex = equiRectMap;
		img = &vkTex->GetImage();
		texName += "_equirect";

		if(optOutFlags)
			*optOutFlags |= PreparedTextureOutputFlags::Envmap;
	}

	auto texPath = "materials\\" + texName;
	texPath += ".dds";
	// Check if DDS version of the texture already exists, in which case we can just use it directly!
	std::string absPath;
	if(FileManager::FindAbsolutePath(texPath, absPath)) {
		outSuccess = true;
		return absPath;
	}

	// Texture does not have the right format to begin with or does not exist on the local hard drive.
	// We will have to create the texture file in the right format (if the texture object is valid).
	if(tex == nullptr)
		return get_abs_error_texture_path(); // Texture is not valid! Return error texture.

	// Output path for the DDS-file we're about to create
	auto ddsPath = "addons/converted/materials/" + texName;
	uimg::TextureInfo imgWriteInfo {};
	imgWriteInfo.containerFormat = uimg::TextureInfo::ContainerFormat::DDS; // Cycles doesn't support KTX
	if(tex->HasFlag(Texture::Flags::SRGB))
		imgWriteInfo.flags |= uimg::TextureInfo::Flags::SRGB;

	// Try to determine appropriate formats
	if(tex->HasFlag(Texture::Flags::NormalMap)) {
		imgWriteInfo.inputFormat = uimg::TextureInfo::InputFormat::R32G32B32A32_Float;
		imgWriteInfo.SetNormalMap();
	}
	else {
		auto format = img->GetFormat();
		if(prosper::util::is_16bit_format(format)) {
			imgWriteInfo.inputFormat = uimg::TextureInfo::InputFormat::R16G16B16A16_Float;
			imgWriteInfo.outputFormat = uimg::TextureInfo::OutputFormat::HDRColorMap;
		}
		else if(prosper::util::is_32bit_format(format) || prosper::util::is_64bit_format(format)) {
			imgWriteInfo.inputFormat = uimg::TextureInfo::InputFormat::R32G32B32A32_Float;
			imgWriteInfo.outputFormat = uimg::TextureInfo::OutputFormat::HDRColorMap;
		}
		else {
			imgWriteInfo.inputFormat = uimg::TextureInfo::InputFormat::R8G8B8A8_UInt;
			// TODO: Check the alpha channel values to determine whether we actually need a full alpha channel?
			imgWriteInfo.outputFormat = uimg::TextureInfo::OutputFormat::ColorMapSmoothAlpha;
		}
		switch(format) {
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
	if(c_game->SaveImage(*img, ddsPath, imgWriteInfo) && FileManager::FindAbsolutePath(ddsPath + ".dds", absPath)) {
		outSuccess = true;
		outConverted = true;
		return absPath;
	}
	// Something went wrong, fall back to error texture!
	return get_abs_error_texture_path();
}

static std::optional<std::string> prepare_texture(TextureInfo *texInfo, PreparedTextureInputFlags inFlags, PreparedTextureOutputFlags *optOutFlags = nullptr, const std::optional<std::string> &defaultTexture = {})
{
	if(optOutFlags)
		*optOutFlags = PreparedTextureOutputFlags::None;
	if(texInfo == nullptr)
		return {};
	auto success = false;
	auto converted = false;
	auto result = prepare_texture(texInfo, success, converted, inFlags, optOutFlags, defaultTexture);
	if(success == false) {
		Con::cwar << "WARNING: Unable to prepare texture '";
		if(texInfo)
			Con::cwar << texInfo->name;
		else
			Con::cwar << "Unknown";
		Con::cwar << "'! Using error texture instead..." << Con::endl;
	}
	else {
		if(converted)
			Con::cout << "Converted texture '" << texInfo->name << "' to DDS!" << Con::endl;
#if 0
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
#endif
	}

	return result;
}

pragma::modules::cycles::Cache::Cache(unirender::Scene::RenderMode renderMode) : m_renderMode {renderMode}
{
	m_shaderCache = unirender::ShaderCache::Create();
	m_mdlCache = unirender::ModelCache::Create();
	m_mdlCache->AddChunk(*m_shaderCache);
}

std::vector<std::shared_ptr<pragma::modules::cycles::Cache::MeshData>> pragma::modules::cycles::Cache::AddMeshList(Model &mdl, const std::vector<std::shared_ptr<ModelMesh>> &meshList, const std::string &meshName, BaseEntity *optEnt, const std::optional<umath::ScaledTransform> &opose,
  uint32_t skinId, pragma::CModelComponent *optMdlC, pragma::CAnimatedComponent *optAnimC, const std::function<bool(ModelMesh &, const umath::ScaledTransform &)> &optMeshFilter, const std::function<bool(ModelSubMesh &, const umath::ScaledTransform &)> &optSubMeshFilter,
  const std::function<void(ModelSubMesh &)> &optOnMeshAdded)
{
	auto pose = opose.has_value() ? *opose : umath::ScaledTransform {};
	auto hasAlphas = false;
	auto hasWrinkles = (mdl.GetVertexAnimations().empty() == false); // TODO: Not the best way to determine if the entity uses wrinkles
	std::vector<std::shared_ptr<MeshData>> meshDatas {};
	meshDatas.reserve(meshList.size());
	for(auto &mesh : meshList) {
		if(optMeshFilter != nullptr && optMeshFilter(*mesh, pose) == false)
			continue;
		for(auto &subMesh : mesh->GetSubMeshes()) {
			if(subMesh->GetGeometryType() != ModelSubMesh::GeometryType::Triangles || subMesh->GetTriangleCount() == 0 || (optSubMeshFilter != nullptr && optSubMeshFilter(*subMesh, pose) == false))
				continue;
			hasAlphas = hasAlphas || (subMesh->GetAlphaCount() > 0);

			auto meshData = CalcMeshData(mdl, *subMesh, hasAlphas, hasWrinkles, optMdlC, optAnimC);
			if(opose.has_value()) {
				for(auto &v : meshData->vertices) {
					v.position = *opose * v.position;
					uvec::rotate(&v.normal, opose->GetRotation());
					uvec::normalize(&v.normal);
				}
			}
			meshData->shader = CreateShader(GetUniqueName(), mdl, *subMesh, optEnt, skinId);
			if(meshData->shader) {
				meshDatas.push_back(meshData);
				optOnMeshAdded(*subMesh);
			}
		}
	}
	return meshDatas;
}

std::vector<std::shared_ptr<pragma::modules::cycles::Cache::MeshData>> pragma::modules::cycles::Cache::AddModel(Model &mdl, const std::string &meshName, BaseEntity *optEnt, const std::optional<umath::ScaledTransform> &pose, uint32_t skinId, pragma::CModelComponent *optMdlC,
  pragma::CAnimatedComponent *optAnimC, const std::function<bool(ModelMesh &, const umath::ScaledTransform &)> &optMeshFilter, const std::function<bool(ModelSubMesh &, const umath::ScaledTransform &)> &optSubMeshFilter, const std::function<void(ModelSubMesh &)> &optOnMeshAdded)
{
	std::vector<std::shared_ptr<ModelMesh>> lodMeshes {};
	std::vector<uint32_t> bodyGroups {};
	bodyGroups.resize(mdl.GetBodyGroupCount());
	mdl.GetBodyGroupMeshes(bodyGroups, 0, lodMeshes);
	return AddMeshList(mdl, lodMeshes, meshName, optEnt, pose, skinId, optMdlC, optAnimC, optMeshFilter, optSubMeshFilter, optOnMeshAdded);
}

std::vector<std::shared_ptr<pragma::modules::cycles::Cache::MeshData>> pragma::modules::cycles::Cache::AddEntityMesh(BaseEntity &ent, std::vector<ModelSubMesh *> *optOutTargetMeshes, const std::function<bool(ModelMesh &, const umath::ScaledTransform &)> &meshFilter,
  const std::function<bool(ModelSubMesh &, const umath::ScaledTransform &)> &subMeshFilter, const std::string &nameSuffix, const std::optional<umath::ScaledTransform> &pose)
{
#if 0
	if(m_renderMode == RenderMode::BakeDiffuseLighting && ent.IsWorld() == false)
	{
		Con::cwar<<"WARNING: Baking diffuse lighting currently only supported for world entity, but attempted to add an entity of a different type! Entity will be ignored!"<<Con::endl;
		return;
	}
#endif
	auto *mdlC = static_cast<pragma::CModelComponent *>(ent.GetModelComponent());
	auto mdl = mdlC ? mdlC->GetModel() : nullptr;
	if(mdl == nullptr)
		return {};
	mdlC->UpdateLOD(0u);
	auto animC = ent.GetComponent<CAnimatedComponent>();

	unirender::PMesh mesh = nullptr;
	auto &mdlName = mdl->GetName();
	auto shouldCache = false; //true;
	if(ent.IsWorld())
		shouldCache = false;
	if(mdlC->GetMaterialOverrides().empty() == false)
		shouldCache = false; // Don't use cache if the entity uses material overrides
	if(animC.valid() && animC->GetAnimation() != -1)
		shouldCache = false; // Don't use cache if the entity is animated

	auto skin = mdlC->GetSkin();
	if(shouldCache) {
		auto it = m_modelCache.find(mdlName);
		if(it != m_modelCache.end()) {
			auto itInstance = std::find_if(it->second.begin(), it->second.end(), [skin](const ModelCacheInstance &instance) { return instance.skin == skin; });
			if(itInstance != it->second.end())
				mesh = itInstance->mesh;
		}
	}
	std::vector<std::shared_ptr<MeshData>> meshDatas;
	if(mesh == nullptr) {
		std::string name = "ent" + nameSuffix + "_" + std::to_string(ent.GetLocalIndex());
		std::vector<ModelSubMesh *> tmpTargetMeshes {};
		auto *targetMeshes = (optOutTargetMeshes != nullptr) ? optOutTargetMeshes : &tmpTargetMeshes;
		targetMeshes->reserve(targetMeshes->size() + mdl->GetSubMeshCount());

		auto skyC = ent.GetComponent<CSkyboxComponent>();
		if(skyC.valid()) {
			// Special case
			auto &pose = ent.GetPose();
			AddModel(*mdl, name, &ent, pose, ent.GetSkin(), mdlC, animC.get(), meshFilter, [&targetMeshes, &subMeshFilter](ModelSubMesh &mesh, const umath::ScaledTransform &pose) -> bool {
				if(subMeshFilter && subMeshFilter(mesh, pose) == false)
					return false;
				targetMeshes->push_back(&mesh);
				return false;
			});

			std::optional<std::string> skyboxTexture {};
			for(auto &mesh : *targetMeshes) {
				auto *mat = mdlC->GetRenderMaterial(mesh->GetSkinTextureIndex(), ent.GetSkin());
				if(mat == nullptr || (ustring::compare<std::string>(mat->GetShaderIdentifier(), "skybox", false) == false && ustring::compare<std::string>(mat->GetShaderIdentifier(), "skybox_equirect", false) == false))
					continue;
				auto *diffuseMap = mat->GetTextureInfo("skybox");
				auto tex = diffuseMap ? diffuseMap->texture : nullptr;
				auto vkTex = tex ? std::static_pointer_cast<Texture>(tex)->GetVkTexture() : nullptr;
				if(vkTex == nullptr || vkTex->GetImage().IsCubemap() == false)
					continue;
				PreparedTextureOutputFlags flags;
				auto diffuseTexPath = prepare_texture(diffuseMap, PreparedTextureInputFlags::CanBeEnvMap, &flags);
				if(diffuseTexPath.has_value() == false || umath::is_flag_set(flags, PreparedTextureOutputFlags::Envmap) == false)
					continue;
				skyboxTexture = diffuseTexPath;
			}
			if(skyboxTexture.has_value())
				m_sky = *skyboxTexture;
			return {};
		}

		auto fFilterMesh = [&subMeshFilter](ModelSubMesh &mesh, const umath::ScaledTransform &pose) -> bool { return !subMeshFilter || subMeshFilter(mesh, pose); };
		auto fOnMeshAdded = [&targetMeshes](ModelSubMesh &mesh) { targetMeshes->push_back(&mesh); };

		auto renderC = ent.GetComponent<pragma::CRenderComponent>();
		if(renderC.valid()) {
			auto &lodGroup = renderC->GetLodMeshGroup(0);
			auto &lodMeshes = renderC->GetLODMeshes();
			std::vector<std::shared_ptr<ModelMesh>> meshes;
			meshes.reserve(lodGroup.second);
			for(auto meshIdx = lodGroup.first; meshIdx < lodGroup.first + lodGroup.second; ++meshIdx)
				meshes.push_back(lodMeshes.at(meshIdx));
			meshDatas = AddMeshList(*mdl, lodMeshes, name, &ent, pose, ent.GetSkin(), mdlC, animC.get(), meshFilter, fFilterMesh, fOnMeshAdded);
		}
		else
			meshDatas = AddModel(*mdl, name, &ent, pose, ent.GetSkin(), mdlC, animC.get(), meshFilter, fFilterMesh, fOnMeshAdded);
		if(meshDatas.empty())
			return meshDatas;
	}

	if(mdlName.empty() == false) {
		if(shouldCache) {
			auto it = m_modelCache.find(mdlName);
			if(it == m_modelCache.end())
				it = m_modelCache.insert(std::make_pair(mdlName, std::vector<ModelCacheInstance> {})).first;
			it->second.push_back({mesh, skin});
		}
	}
	return meshDatas;
}
unirender::PObject pragma::modules::cycles::Cache::AddEntity(BaseEntity &ent, std::vector<ModelSubMesh *> *optOutTargetMeshes, const std::function<bool(ModelMesh &, const umath::ScaledTransform &)> &meshFilter,
  const std::function<bool(ModelSubMesh &, const umath::ScaledTransform &)> &subMeshFilter, const std::string &nameSuffix)
{
	auto meshDatas = AddEntityMesh(ent, optOutTargetMeshes, meshFilter, subMeshFilter, nameSuffix);
	if(meshDatas.empty())
		return nullptr;
	std::string name = "ent" + nameSuffix + "_" + std::to_string(ent.GetLocalIndex());
	auto mesh = BuildMesh(name, meshDatas);
	if(mesh == nullptr)
		return nullptr;
	auto renderMode = m_renderMode;
	// Create the object using the mesh
	auto &t = ent.GetPose();
	auto o = unirender::Object::Create(*mesh);
	if(unirender::Scene::IsRenderSceneMode(renderMode) || unirender::Scene::IsLightmapRenderMode(renderMode)) {
		o->SetPos(t.GetOrigin());
		o->SetRotation(t.GetRotation());
		o->SetScale(t.GetScale());
	}
	o->SetUuid(ent.GetUuid());
	o->SetName(util::uuid_to_string(ent.GetUuid()));
	m_mdlCache->GetChunks().front().AddObject(*o);
	return o;
}

static bool load_hair_strand_data(util::HairStrandData &strandData, const udm::LinkedPropertyWrapper &data, std::string &outErr)
{
	//if(data.GetAssetType() != "PHD" || data.GetAssetVersion() < 1)
	//	return false;
	auto udm = data; //*data;
	uint32_t numStrands = 0;
	udm["strandCount"](numStrands);
	udm["segmentCounts"](strandData.hairSegments);
	auto udmStrands = udm["strands"];
	udmStrands["points"](strandData.points);
	udmStrands["uvs"](strandData.uvs);
	udmStrands["thickness"](strandData.thicknessData);
	return true;
}

std::shared_ptr<pragma::modules::cycles::Cache::MeshData> pragma::modules::cycles::Cache::CalcMeshData(Model &mdl, ModelSubMesh &mdlMesh, bool includeAlphas, bool includeWrinkles, pragma::CModelComponent *optMdlC, pragma::CAnimatedComponent *optAnimC)
{
	auto meshData = std::make_shared<MeshData>();
	auto &meshVerts = mdlMesh.GetVertices();
	auto &meshAlphas = mdlMesh.GetAlphas();

	auto extData = mdlMesh.GetExtensionData();
	auto udmHair = extData["hair"]["strandData"]["assetData"];
	if(udmHair) {
		meshData->hairStrandData = std::make_unique<util::HairStrandData>();
		std::string err;
		if(!load_hair_strand_data(*meshData->hairStrandData, udmHair, err))
			meshData->hairStrandData = nullptr;
	}

	std::vector<umath::Vertex> transformedVerts {};
	transformedVerts.reserve(meshVerts.size());

	std::optional<std::vector<float>> alphas {};
	if(includeAlphas) {
		alphas = std::vector<float> {};
		alphas->reserve(meshVerts.size());
	}

	std::optional<std::vector<float>> wrinkles {};
	if(includeWrinkles) {
		wrinkles = std::vector<float> {};
		wrinkles->reserve(meshVerts.size());
	}

	for(auto vertIdx = decltype(meshVerts.size()) {0u}; vertIdx < meshVerts.size(); ++vertIdx) {
		auto &v = meshVerts.at(vertIdx);
		if(unirender::Scene::IsRenderSceneMode(m_renderMode)) {
			// TODO: Do we really need the tangent?
			Vector3 normalOffset {};
			float wrinkle = 0.f;
			auto transformMat = optAnimC ? optAnimC->GetVertexTransformMatrix(mdlMesh, vertIdx, &normalOffset, &wrinkle) : std::optional<Mat4> {};
			if(transformMat.has_value()) {
				// Apply vertex matrix (including animations, flexes, etc.)
				auto vpos = *transformMat * Vector4 {v.position.x, v.position.y, v.position.z, 1.f};
				auto vn = *transformMat * Vector4 {v.normal.x, v.normal.y, v.normal.z, 0.f};
				auto vt = *transformMat * Vector4 {v.tangent.x, v.tangent.y, v.tangent.z, 0.f};

				transformedVerts.push_back({});
				auto &vTransformed = transformedVerts.back();

				auto &pos = vTransformed.position;
				pos = {vpos.x, vpos.y, vpos.z};
				pos /= vpos.w;

				auto &n = vTransformed.normal;
				n = {vn.x, vn.y, vn.z};
				n += normalOffset;
				uvec::normalize(&n);

				auto &t = vTransformed.tangent;
				Vector3 nt = {vt.x, vt.y, vt.z};
				nt += normalOffset;
				uvec::normalize(&nt);
				t = {nt, t.w};

				vTransformed.uv = v.uv;
			}
			else
				transformedVerts.push_back(v);
			if(includeWrinkles)
				wrinkles->push_back(wrinkle);
		}
		else {
			// We're probably baking something (e.g. ao map), so we don't want to include the entity's animated pose.
			transformedVerts.push_back(v);
		}

		if(includeAlphas) {
			auto alpha = (vertIdx < meshAlphas.size()) ? meshAlphas.at(vertIdx).x : 0.f;
			meshData->alphas->push_back(alpha);
		}
	}

	std::vector<int32_t> indices;
	indices.reserve(mdlMesh.GetIndexCount());
	mdlMesh.VisitIndices([this, &indices](auto *indexData, uint32_t numIndices) {
		for(auto i = decltype(numIndices) {0u}; i < numIndices; ++i)
			indices.push_back(indexData[i]);
	});

	// Subdivision
	auto udmExtData = mdl.GetExtensionData();
	uint32_t subdivLevel = 0;
	udmExtData.GetFromPath("unirender/subdivision/level")(subdivLevel);
	if(subdivLevel > 0) {
		std::vector<std::shared_ptr<BaseChannelData>> customAttributes {};
		customAttributes.reserve(2);

		std::vector<float> perFaceAlphaData {};
		if(alphas.has_value()) {
			auto alphaData = std::make_shared<ChannelData<OsdFloatAttr>>([&perFaceAlphaData](BaseChannelData &cd, FaceVertexIndex faceVertexIndex, umath::Vertex &v, int idx) { perFaceAlphaData.at(faceVertexIndex) = static_cast<OsdFloatAttr *>(cd.GetElementPtr(idx))->value; },
			  [&perFaceAlphaData](uint32_t numFaces) { perFaceAlphaData.resize(numFaces * 3); });
			alphaData->ReserveBuffer(meshData->vertices.size());

			for(auto alpha : *alphas)
				alphaData->buffer.push_back(alpha);
			customAttributes.push_back(alphaData);
		}

		std::vector<float> perFaceWrinkleData {};
		if(wrinkles.has_value()) {
			auto wrinkleData = std::make_shared<ChannelData<OsdFloatAttr>>([&perFaceWrinkleData](BaseChannelData &cd, FaceVertexIndex faceVertexIndex, umath::Vertex &v, int idx) { perFaceWrinkleData.at(faceVertexIndex) = static_cast<OsdFloatAttr *>(cd.GetElementPtr(idx))->value; },
			  [&perFaceWrinkleData](uint32_t numFaces) { perFaceWrinkleData.resize(numFaces * 3); });
			wrinkleData->ReserveBuffer(meshData->vertices.size());

			for(auto wrinkle : *wrinkles)
				wrinkleData->buffer.push_back(wrinkle);
			customAttributes.push_back(wrinkleData);
		}
		subdivide_mesh(transformedVerts, indices, meshData->vertices, meshData->triangles, subdivLevel, customAttributes);

		if(alphas.has_value()) {
			meshData->alphas = std::vector<float> {};
			meshData->alphas->resize(meshData->vertices.size());
			for(auto i = decltype(meshData->triangles.size()) {0u}; i < meshData->triangles.size(); ++i) {
				auto idx = meshData->triangles.at(i);
				meshData->alphas->at(idx) = perFaceAlphaData.at(i);
			}
		}
		if(wrinkles.has_value()) {
			meshData->wrinkles = std::vector<float> {};
			meshData->wrinkles->resize(meshData->vertices.size());
			for(auto i = decltype(meshData->triangles.size()) {0u}; i < meshData->triangles.size(); ++i) {
				auto idx = meshData->triangles.at(i);
				meshData->wrinkles->at(idx) = perFaceWrinkleData.at(i);
			}
		}
	}
	else {
		meshData->vertices = std::move(transformedVerts);
		meshData->triangles = std::move(indices);
		if(alphas.has_value())
			meshData->alphas = std::move(*alphas);
		if(wrinkles.has_value())
			meshData->wrinkles = std::move(*wrinkles);
	}
	return meshData;
}

Material *pragma::modules::cycles::Cache::GetMaterial(BaseEntity &ent, ModelSubMesh &subMesh, uint32_t skinId) const
{
	auto mdlC = ent.GetModelComponent();
	return mdlC ? GetMaterial(static_cast<pragma::CModelComponent &>(*mdlC), subMesh, skinId) : nullptr;
}

Material *pragma::modules::cycles::Cache::GetMaterial(Model &mdl, ModelSubMesh &subMesh, uint32_t skinId) const
{
	auto texIdx = mdl.GetMaterialIndex(subMesh, skinId);
	return texIdx.has_value() ? mdl.GetMaterial(*texIdx) : nullptr;
}

Material *pragma::modules::cycles::Cache::GetMaterial(pragma::CModelComponent &mdlC, ModelSubMesh &subMesh, uint32_t skinId) const
{
	auto mdl = mdlC.GetModel();
	if(mdl == nullptr)
		return nullptr;
	auto baseTexIdx = subMesh.GetSkinTextureIndex();
	return mdlC.GetRenderMaterial(baseTexIdx, skinId);
}

unirender::PShader pragma::modules::cycles::Cache::CreateShader(const std::string &meshName, Model &mdl, ModelSubMesh &subMesh, BaseEntity *optEnt, uint32_t skinId) const
{
	// Make sure all textures have finished loading
	static_cast<msys::CMaterialManager &>(client->GetMaterialManager()).GetTextureManager().WaitForAllPendingCompleted();

	auto *mat = optEnt ? GetMaterial(*optEnt, subMesh, skinId) : GetMaterial(mdl, subMesh, skinId);
	if(mat == nullptr)
		return nullptr;
	ShaderInfo shaderInfo {};
	if(optEnt)
		shaderInfo.entity = optEnt;
	shaderInfo.subMesh = &subMesh;
	return CreateShader(*mat, meshName, shaderInfo);
}

void pragma::modules::cycles::Cache::AddMesh(Model &mdl, unirender::Mesh &mesh, ModelSubMesh &mdlMesh, pragma::CModelComponent *optMdlC, pragma::CAnimatedComponent *optAnimC)
{
	auto meshData = CalcMeshData(mdl, mdlMesh, mesh.HasAlphas(), mesh.HasWrinkles(), optMdlC, optAnimC);
	if(meshData == nullptr)
		return;
	AddMeshDataToMesh(mesh, *meshData);
}

unirender::PMesh pragma::modules::cycles::Cache::BuildMesh(const std::string &meshName, const std::vector<std::shared_ptr<MeshData>> &meshDatas, const std::optional<umath::ScaledTransform> &pose) const
{
	uint64_t numVerts = 0;
	uint64_t numTris = 0;
	auto hasAlphas = false;
	auto hasWrinkles = false;
	for(auto &meshData : meshDatas) {
		numVerts += meshData->vertices.size();
		numTris += meshData->triangles.size();
		hasAlphas = hasAlphas || meshData->alphas.has_value();
		hasWrinkles = hasWrinkles || meshData->wrinkles.has_value();
	}

	auto flags = unirender::Mesh::Flags::None;
	if(hasAlphas)
		flags |= unirender::Mesh::Flags::HasAlphas;
	if(hasWrinkles)
		flags |= unirender::Mesh::Flags::HasWrinkles;
	auto mesh = unirender::Mesh::Create(meshName, numVerts, numTris / 3, flags);
	m_mdlCache->GetChunks().front().AddMesh(*mesh);
	for(auto &meshData : meshDatas)
		AddMeshDataToMesh(*mesh, *meshData, pose);
	return mesh;
}

void pragma::modules::cycles::Cache::AddMeshDataToMesh(unirender::Mesh &mesh, const MeshData &meshData, const std::optional<umath::ScaledTransform> &pose) const
{
	auto triIndexVertexOffset = mesh.GetVertexOffset();
	auto shaderIdx = mesh.AddSubMeshShader(*meshData.shader);
	for(auto &v : meshData.vertices) {
		auto pos = v.position;
		if(pose.has_value())
			pos = *pose * pos;
		mesh.AddVertex(pos, v.normal, v.tangent, v.uv);
	}

	for(auto i = decltype(meshData.triangles.size()) {0u}; i < meshData.triangles.size(); i += 3)
		mesh.AddTriangle(triIndexVertexOffset + meshData.triangles.at(i), triIndexVertexOffset + meshData.triangles.at(i + 1), triIndexVertexOffset + meshData.triangles.at(i + 2), shaderIdx);

	if(meshData.wrinkles.has_value()) {
		for(auto wrinkle : *meshData.wrinkles)
			mesh.AddWrinkleFactor(wrinkle);
	}
	if(meshData.alphas.has_value()) {
		for(auto alpha : *meshData.alphas)
			mesh.AddAlpha(alpha);
	}
	if(meshData.hairStrandData)
		mesh.AddHairStrandData(*meshData.hairStrandData, shaderIdx);
}

void pragma::modules::cycles::Cache::AddAOBakeTarget(BaseEntity *optEnt, Model &mdl, uint32_t matIndex, std::shared_ptr<unirender::Object> &oAo, std::shared_ptr<unirender::Object> &oEnv)
{
	std::vector<std::shared_ptr<MeshData>> materialMeshes;
	std::vector<std::shared_ptr<MeshData>> envMeshes;
	auto fFilterMeshes = [this, matIndex, &materialMeshes, &envMeshes, &mdl](ModelSubMesh &mesh, const umath::ScaledTransform &pose) -> bool {
		auto meshData = CalcMeshData(mdl, mesh, false, false);
		meshData->shader = CreateShader(GetUniqueName(), mdl, mesh);
		auto texIdx = mdl.GetMaterialIndex(mesh);
		if(texIdx.has_value() && *texIdx == matIndex) {
			materialMeshes.push_back(meshData);
			return false;
		}
		envMeshes.push_back(meshData);
		return false;
	};
	if(optEnt)
		AddEntityMesh(*optEnt, nullptr, nullptr, fFilterMeshes);
	else
		AddModel(mdl, "ao_mesh", nullptr, {}, 0 /* skin */, nullptr, nullptr, nullptr, fFilterMeshes);

	// We'll create a separate mesh from all model meshes which use the specified material.
	// This way we can map the uv coordinates to the ao output texture more easily.
	auto mesh = BuildMesh("ao_target", materialMeshes);
	oAo = unirender::Object::Create(*mesh);
	m_mdlCache->GetChunks().front().AddObject(*oAo);

	oEnv = nullptr;
	if(envMeshes.empty())
		return;

	// Note: Ambient occlusion is baked for a specific material (matIndex). The model may contain meshes that use a different material,
	// in which case those meshes are still needed to render accurate ambient occlusion values near edge cases.
	// To distinguish them from the actual ao-meshes, they're stored in a separate mesh/object here.
	// The actual ao bake target (see code above) has to be the first mesh added to the scene, otherwise the ao result may be incorrect.
	// The reason for this is currently unknown.
	auto meshEnv = BuildMesh("ao_mesh", envMeshes);
	oEnv = unirender::Object::Create(*meshEnv);
	m_mdlCache->GetChunks().front().AddObject(*oEnv);
}

void pragma::modules::cycles::Cache::AddAOBakeTarget(BaseEntity &ent, uint32_t matIndex, std::shared_ptr<unirender::Object> &oAo, std::shared_ptr<unirender::Object> &oEnv)
{
	auto mdl = ent.GetModel();
	if(mdl == nullptr)
		return;
	AddAOBakeTarget(&ent, *mdl, matIndex, oAo, oEnv);
}

void pragma::modules::cycles::Cache::AddAOBakeTarget(Model &mdl, uint32_t matIndex, std::shared_ptr<unirender::Object> &oAo, std::shared_ptr<unirender::Object> &oEnv) { AddAOBakeTarget(nullptr, mdl, matIndex, oAo, oEnv); }
