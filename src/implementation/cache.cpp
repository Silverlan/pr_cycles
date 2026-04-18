// SPDX-FileCopyrightText: (c) 2024 Silverlan <opensource@pragma-engine.com>
// SPDX-License-Identifier: MIT

module;

#ifdef _WIN32
#ifndef M_PI
// Workaround for opensubdiv compiler error when building on Windows
#define M_PI 3.1415926535897932384626433832795
#endif
#endif
#include <opensubdiv/far/topologyDescriptor.h>
#include <opensubdiv/far/primvarRefiner.h>

module pragma.modules.scenekit;

import pragma.client;
import pragma.scenekit;

import :scene;
import :subdivision;

enum class PreparedTextureInputFlags : uint8_t { None = 0u, CanBeEnvMap = 1u };
enum class PreparedTextureOutputFlags : uint8_t { None = 0u, Envmap = 1u };
namespace pragma::math::scoped_enum::bitwise {
	template<>
	struct enable_bitwise_operators<PreparedTextureInputFlags> : std::true_type {};
}
namespace pragma::math::scoped_enum::bitwise {
	template<>
	struct enable_bitwise_operators<PreparedTextureOutputFlags> : std::true_type {};
}

static std::optional<std::string> get_abs_error_texture_path()
{
	std::string errTexPath = "materials\\error.dds";
	std::string absPath;
	if(pragma::fs::find_absolute_path(errTexPath, absPath) == false)
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
	auto tex = texInfo ? std::static_pointer_cast<pragma::material::Texture>(texInfo->texture) : nullptr;
	std::string texName {};
	// Make sure texture has been fully loaded!
	if(tex == nullptr || tex->IsLoaded() == false) {
		tex = nullptr;
		if(defaultTexture.has_value()) {
			auto &texManager = static_cast<pragma::material::CMaterialManager &>(pragma::get_client_state()->GetMaterialManager()).GetTextureManager();
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
	static_cast<CMaterialManager&>(pragma::get_client_state()->GetMaterialManager()).GetTextureManager().Load(*pragma::get_cengine(),texInfo->name,loadInfo);
	if(tex->IsLoaded() == false)
	return get_abs_error_texture_path();
	}
	*/
	ufile::remove_extension_from_filename(texName); // DDS-writer will add the extension for us

	auto vkTex = tex->GetVkTexture();
	auto *img = &vkTex->GetImage();
	auto isCubemap = img->IsCubemap();
	if(isCubemap) {
		if(pragma::math::is_flag_set(inFlags, PreparedTextureInputFlags::CanBeEnvMap) == false)
			return {};
		// Image is a cubemap, which Cycles doesn't support! We'll have to convert it to a equirectangular image and use that instead.
		auto &shader = static_cast<pragma::ShaderCubemapToEquirectangular &>(*pragma::get_cengine()->GetShader("cubemap_to_equirectangular"));
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
	if(pragma::fs::find_absolute_path(texPath, absPath)) {
		outSuccess = true;
		return absPath;
	}

	// Texture does not have the right format to begin with or does not exist on the local hard drive.
	// We will have to create the texture file in the right format (if the texture object is valid).
	if(tex == nullptr)
		return get_abs_error_texture_path(); // Texture is not valid! Return error texture.

	// Output path for the DDS-file we're about to create
	auto ddsPath = "addons/converted/materials/" + texName;
	pragma::image::TextureInfo imgWriteInfo {};
	imgWriteInfo.containerFormat = pragma::image::TextureInfo::ContainerFormat::DDS; // Cycles doesn't support KTX
	if(tex->HasFlag(pragma::material::Texture::Flags::SRGB))
		imgWriteInfo.flags |= pragma::image::TextureInfo::Flags::SRGB;

	// Try to determine appropriate formats
	if(tex->HasFlag(pragma::material::Texture::Flags::NormalMap)) {
		imgWriteInfo.inputFormat = pragma::image::TextureInfo::InputFormat::R32G32B32A32_Float;
		imgWriteInfo.SetNormalMap();
	}
	else {
		auto format = img->GetFormat();
		if(prosper::util::is_16bit_format(format)) {
			imgWriteInfo.inputFormat = pragma::image::TextureInfo::InputFormat::R16G16B16A16_Float;
			imgWriteInfo.outputFormat = pragma::image::TextureInfo::OutputFormat::HDRColorMap;
		}
		else if(prosper::util::is_32bit_format(format) || prosper::util::is_64bit_format(format)) {
			imgWriteInfo.inputFormat = pragma::image::TextureInfo::InputFormat::R32G32B32A32_Float;
			imgWriteInfo.outputFormat = pragma::image::TextureInfo::OutputFormat::HDRColorMap;
		}
		else {
			imgWriteInfo.inputFormat = pragma::image::TextureInfo::InputFormat::R8G8B8A8_UInt;
			// TODO: Check the alpha channel values to determine whether we actually need a full alpha channel?
			imgWriteInfo.outputFormat = pragma::image::TextureInfo::OutputFormat::ColorMapSmoothAlpha;
		}
		switch(format) {
		case prosper::Format::BC1_RGBA_SRGB_Block:
		case prosper::Format::BC1_RGBA_UNorm_Block:
		case prosper::Format::BC1_RGB_SRGB_Block:
		case prosper::Format::BC1_RGB_UNorm_Block:
			imgWriteInfo.outputFormat = pragma::image::TextureInfo::OutputFormat::BC1;
			break;
		case prosper::Format::BC2_SRGB_Block:
		case prosper::Format::BC2_UNorm_Block:
			imgWriteInfo.outputFormat = pragma::image::TextureInfo::OutputFormat::BC2;
			break;
		case prosper::Format::BC3_SRGB_Block:
		case prosper::Format::BC3_UNorm_Block:
			imgWriteInfo.outputFormat = pragma::image::TextureInfo::OutputFormat::BC3;
			break;
		case prosper::Format::BC4_SNorm_Block:
		case prosper::Format::BC4_UNorm_Block:
			imgWriteInfo.outputFormat = pragma::image::TextureInfo::OutputFormat::BC4;
			break;
		case prosper::Format::BC5_SNorm_Block:
		case prosper::Format::BC5_UNorm_Block:
			imgWriteInfo.outputFormat = pragma::image::TextureInfo::OutputFormat::BC5;
			break;
		case prosper::Format::BC6H_SFloat_Block:
		case prosper::Format::BC6H_UFloat_Block:
			// TODO: As of 20-03-26, Cycles (/oiio) does not have support for BC6, so we'll
			// fall back to a different format
			imgWriteInfo.inputFormat = pragma::image::TextureInfo::InputFormat::R16G16B16A16_Float;
			// imgWriteInfo.outputFormat = image::TextureInfo::OutputFormat::BC6;
			imgWriteInfo.outputFormat = pragma::image::TextureInfo::OutputFormat::DXT5;
			break;
		case prosper::Format::BC7_SRGB_Block:
		case prosper::Format::BC7_UNorm_Block:
			// TODO: As of 20-03-26, Cycles (/oiio) does not have support for BC7, so we'll
			// fall back to a different format
			imgWriteInfo.inputFormat = pragma::image::TextureInfo::InputFormat::R16G16B16A16_Float;
			// imgWriteInfo.outputFormat = image::TextureInfo::OutputFormat::BC7;
			imgWriteInfo.outputFormat = pragma::image::TextureInfo::OutputFormat::DXT1;
			break;
		}
	}
	absPath = "";
	// Save the DDS image and make sure the file exists
	if(static_cast<pragma::CGame*>(pragma::get_client_game())->SaveImage(*img, ddsPath, imgWriteInfo) && pragma::fs::find_absolute_path(ddsPath + ".dds", absPath)) {
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
		Con::CWAR << "WARNING: Unable to prepare texture '";
		if(texInfo)
			Con::CWAR << texInfo->name;
		else
			Con::CWAR << "Unknown";
		Con::CWAR << "'! Using error texture instead..." << Con::endl;
	}
	else {
		if(converted)
			Con::COUT << "Converted texture '" << texInfo->name << "' to DDS!" << Con::endl;
#if 0
		ccl::ImageMetaData metaData;
		if(scene.image_manager->get_image_metadata(*result,nullptr,ccl::u_colorspace_raw,metaData) == false)
		{
			Con::CWAR<<"WARNING: Texture '"<<texInfo->name<<"' has format which is incompatible with cycles! Falling back to error texture..."<<Con::endl;
			result = get_abs_error_texture_path();
			if(scene.image_manager->get_image_metadata(*result,nullptr,ccl::u_colorspace_raw,metaData) == false)
			{
				Con::CWAR<<"WARNING: Error texture also not compatible! Falling back to untextured!"<<Con::endl;
				result = {};
			}
		}
#endif
	}

	return result;
}

pragma::modules::scenekit::Cache::Cache(pragma::scenekit::Scene::RenderMode renderMode) : m_renderMode {renderMode}
{
	m_shaderCache = pragma::scenekit::ShaderCache::Create();
	m_mdlCache = pragma::scenekit::ModelCache::Create();
	m_mdlCache->AddChunk(*m_shaderCache);
}

std::vector<std::shared_ptr<pragma::modules::scenekit::Cache::MeshData>> pragma::modules::scenekit::Cache::AddMeshList(asset::Model &mdl, const std::vector<std::shared_ptr<pragma::geometry::ModelMesh>> &meshList, const std::string &meshName, pragma::ecs::BaseEntity *optEnt, const std::optional<pragma::math::ScaledTransform> &opose,
  uint32_t skinId, pragma::CModelComponent *optMdlC, pragma::CAnimatedComponent *optAnimC, const std::function<bool(pragma::geometry::ModelMesh &, const pragma::math::ScaledTransform &)> &optMeshFilter, const std::function<bool(geometry::ModelSubMesh &, const pragma::math::ScaledTransform &)> &optSubMeshFilter,
  const std::function<void(geometry::ModelSubMesh &)> &optOnMeshAdded)
{
	auto pose = opose.has_value() ? *opose : pragma::math::ScaledTransform {};
	auto hasAlphas = false;
	auto hasWrinkles = (mdl.GetVertexAnimations().empty() == false); // TODO: Not the best way to determine if the entity uses wrinkles
	std::vector<std::shared_ptr<MeshData>> meshDatas {};
	meshDatas.reserve(meshList.size());
	for(auto &mesh : meshList) {
		if(optMeshFilter != nullptr && optMeshFilter(*mesh, pose) == false)
			continue;
		for(auto &subMesh : mesh->GetSubMeshes()) {
			if(subMesh->GetGeometryType() != geometry::ModelSubMesh::GeometryType::Triangles || subMesh->GetTriangleCount() == 0 || (optSubMeshFilter != nullptr && optSubMeshFilter(*subMesh, pose) == false))
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

std::vector<std::shared_ptr<pragma::modules::scenekit::Cache::MeshData>> pragma::modules::scenekit::Cache::AddModel(asset::Model &mdl, const std::string &meshName, pragma::ecs::BaseEntity *optEnt, const std::optional<pragma::math::ScaledTransform> &pose, uint32_t skinId, pragma::CModelComponent *optMdlC,
  pragma::CAnimatedComponent *optAnimC, const std::function<bool(pragma::geometry::ModelMesh &, const pragma::math::ScaledTransform &)> &optMeshFilter, const std::function<bool(geometry::ModelSubMesh &, const pragma::math::ScaledTransform &)> &optSubMeshFilter, const std::function<void(geometry::ModelSubMesh &)> &optOnMeshAdded)
{
	std::vector<std::shared_ptr<pragma::geometry::ModelMesh>> lodMeshes {};
	std::vector<uint32_t> bodyGroups {};
	bodyGroups.resize(mdl.GetBodyGroupCount());
	mdl.GetBodyGroupMeshes(bodyGroups, 0, lodMeshes);
	return AddMeshList(mdl, lodMeshes, meshName, optEnt, pose, skinId, optMdlC, optAnimC, optMeshFilter, optSubMeshFilter, optOnMeshAdded);
}

std::vector<std::shared_ptr<pragma::modules::scenekit::Cache::MeshData>> pragma::modules::scenekit::Cache::AddEntityMesh(pragma::ecs::BaseEntity &ent, std::vector<geometry::ModelSubMesh *> *optOutTargetMeshes, const std::function<bool(pragma::geometry::ModelMesh &, const pragma::math::ScaledTransform &)> &meshFilter,
  const std::function<bool(geometry::ModelSubMesh &, const pragma::math::ScaledTransform &)> &subMeshFilter, const std::string &nameSuffix, const std::optional<pragma::math::ScaledTransform> &pose)
{
#if 0
	if(m_renderMode == RenderMode::BakeDiffuseLighting && ent.IsWorld() == false)
	{
		Con::CWAR<<"WARNING: Baking diffuse lighting currently only supported for world entity, but attempted to add an entity of a different type! Entity will be ignored!"<<Con::endl;
		return;
	}
#endif
	auto *mdlC = static_cast<pragma::CModelComponent *>(ent.GetModelComponent());
	auto mdl = mdlC ? mdlC->GetModel() : nullptr;
	if(mdl == nullptr)
		return {};
	mdlC->UpdateLOD(0u);
	auto animC = ent.GetComponent<CAnimatedComponent>();

	pragma::scenekit::PMesh mesh = nullptr;
	auto &mdlName = mdl->GetName();
	auto shouldCache = false; //true;
	if(ent.IsWorld())
		shouldCache = false;
	auto *materialOverrideC = mdlC->GetMaterialOverrideComponent();
	if(materialOverrideC)
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
		std::string name = "ent" + nameSuffix + "_" + util::to_string(ent.GetLocalIndex());
		std::vector<geometry::ModelSubMesh *> tmpTargetMeshes {};
		auto *targetMeshes = (optOutTargetMeshes != nullptr) ? optOutTargetMeshes : &tmpTargetMeshes;
		targetMeshes->reserve(targetMeshes->size() + mdl->GetSubMeshCount());

		auto skyC = ent.GetComponent<CSkyboxComponent>();
		if(skyC.valid()) {
			// Special case
			auto &pose = ent.GetPose();
			AddModel(*mdl, name, &ent, pose, ent.GetSkin(), mdlC, animC.get(), meshFilter, [&targetMeshes, &subMeshFilter](geometry::ModelSubMesh &mesh, const pragma::math::ScaledTransform &pose) -> bool {
				if(subMeshFilter && subMeshFilter(mesh, pose) == false)
					return false;
				targetMeshes->push_back(&mesh);
				return false;
			});

			std::optional<std::string> skyboxTexture {};
			for(auto &mesh : *targetMeshes) {
				auto *mat = mdlC->GetRenderMaterial(mesh->GetSkinTextureIndex(), ent.GetSkin());
				if(mat == nullptr || (pragma::string::compare<std::string>(mat->GetShaderIdentifier(), "skybox", false) == false && pragma::string::compare<std::string>(mat->GetShaderIdentifier(), "skybox_equirect", false) == false))
					continue;
				auto *diffuseMap = mat->GetTextureInfo("skybox");
				auto tex = diffuseMap ? diffuseMap->texture : nullptr;
				auto vkTex = tex ? std::static_pointer_cast<material::Texture>(tex)->GetVkTexture() : nullptr;
				if(vkTex == nullptr || vkTex->GetImage().IsCubemap() == false)
					continue;
				PreparedTextureOutputFlags flags;
				auto diffuseTexPath = prepare_texture(diffuseMap, PreparedTextureInputFlags::CanBeEnvMap, &flags);
				if(diffuseTexPath.has_value() == false || pragma::math::is_flag_set(flags, PreparedTextureOutputFlags::Envmap) == false)
					continue;
				skyboxTexture = diffuseTexPath;
			}
			if(skyboxTexture.has_value())
				m_sky = *skyboxTexture;
			return {};
		}

		auto fFilterMesh = [&subMeshFilter](geometry::ModelSubMesh &mesh, const pragma::math::ScaledTransform &pose) -> bool { return !subMeshFilter || subMeshFilter(mesh, pose); };
		auto fOnMeshAdded = [&targetMeshes](geometry::ModelSubMesh &mesh) { targetMeshes->push_back(&mesh); };

		auto renderC = ent.GetComponent<pragma::CRenderComponent>();
		if(renderC.valid()) {
			auto &lodGroup = renderC->GetLodMeshGroup(0);
			auto &lodMeshes = renderC->GetLODMeshes();
			std::vector<std::shared_ptr<pragma::geometry::ModelMesh>> meshes;
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
pragma::scenekit::PObject pragma::modules::scenekit::Cache::AddEntity(pragma::ecs::BaseEntity &ent, std::vector<geometry::ModelSubMesh *> *optOutTargetMeshes, const std::function<bool(pragma::geometry::ModelMesh &, const pragma::math::ScaledTransform &)> &meshFilter,
  const std::function<bool(geometry::ModelSubMesh &, const pragma::math::ScaledTransform &)> &subMeshFilter, const std::string &nameSuffix)
{
	auto meshDatas = AddEntityMesh(ent, optOutTargetMeshes, meshFilter, subMeshFilter, nameSuffix);
	if(meshDatas.empty())
		return nullptr;
	std::string name = "ent" + nameSuffix + "_" + util::to_string(ent.GetLocalIndex());
	auto mesh = BuildMesh(name, meshDatas);
	if(mesh == nullptr)
		return nullptr;
	auto renderMode = m_renderMode;
	// Create the object using the mesh
	auto &t = ent.GetPose();
	auto o = pragma::scenekit::Object::Create(*mesh);
	if(pragma::scenekit::Scene::IsRenderSceneMode(renderMode) || pragma::scenekit::Scene::IsLightmapRenderMode(renderMode)) {
		o->SetPos(t.GetOrigin());
		o->SetRotation(t.GetRotation());
		o->SetScale(t.GetScale());
	}
	o->SetUuid(ent.GetUuid());
	o->SetName(pragma::util::uuid_to_string(ent.GetUuid()));
	m_mdlCache->GetChunks().front().AddObject(*o);
	return o;
}

static bool load_hair_strand_data(pragma::util::HairStrandData &strandData, const udm::LinkedPropertyWrapper &data, std::string &outErr)
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

std::shared_ptr<pragma::modules::scenekit::Cache::MeshData> pragma::modules::scenekit::Cache::CalcMeshData(asset::Model &mdl, geometry::ModelSubMesh &mdlMesh, bool includeAlphas, bool includeWrinkles, pragma::CModelComponent *optMdlC, pragma::CAnimatedComponent *optAnimC)
{
	auto meshData = std::make_shared<MeshData>();
	auto &meshVerts = mdlMesh.GetVertices();
	auto &meshAlphas = mdlMesh.GetAlphas();

	auto extData = mdlMesh.GetExtensionData();
	auto udmHair = extData["hair"]["strandData"]["assetData"];
	if(udmHair) {
		meshData->hairStrandData = std::make_unique<pragma::util::HairStrandData>();
		std::string err;
		if(!load_hair_strand_data(*meshData->hairStrandData, udmHair, err))
			meshData->hairStrandData = nullptr;
	}

	std::vector<pragma::math::Vertex> transformedVerts {};
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
		if(pragma::scenekit::Scene::IsRenderSceneMode(m_renderMode)) {
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
			auto alphaData = std::make_shared<ChannelData<OsdFloatAttr>>([&perFaceAlphaData](BaseChannelData &cd, FaceVertexIndex faceVertexIndex, pragma::math::Vertex &v, int idx) { perFaceAlphaData.at(faceVertexIndex) = static_cast<OsdFloatAttr *>(cd.GetElementPtr(idx))->value; },
			  [&perFaceAlphaData](uint32_t numFaces) { perFaceAlphaData.resize(numFaces * 3); });
			alphaData->ReserveBuffer(meshData->vertices.size());

			for(auto alpha : *alphas)
				alphaData->buffer.push_back(alpha);
			customAttributes.push_back(alphaData);
		}

		std::vector<float> perFaceWrinkleData {};
		if(wrinkles.has_value()) {
			auto wrinkleData = std::make_shared<ChannelData<OsdFloatAttr>>([&perFaceWrinkleData](BaseChannelData &cd, FaceVertexIndex faceVertexIndex, pragma::math::Vertex &v, int idx) { perFaceWrinkleData.at(faceVertexIndex) = static_cast<OsdFloatAttr *>(cd.GetElementPtr(idx))->value; },
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

pragma::material::Material *pragma::modules::scenekit::Cache::GetMaterial(pragma::ecs::BaseEntity &ent, geometry::ModelSubMesh &subMesh, uint32_t skinId) const
{
	auto mdlC = ent.GetModelComponent();
	return mdlC ? GetMaterial(static_cast<pragma::CModelComponent &>(*mdlC), subMesh, skinId) : nullptr;
}

pragma::material::Material *pragma::modules::scenekit::Cache::GetMaterial(asset::Model &mdl, geometry::ModelSubMesh &subMesh, uint32_t skinId) const
{
	auto texIdx = mdl.GetMaterialIndex(subMesh, skinId);
	return texIdx.has_value() ? mdl.GetMaterial(*texIdx) : nullptr;
}

pragma::material::Material *pragma::modules::scenekit::Cache::GetMaterial(pragma::CModelComponent &mdlC, geometry::ModelSubMesh &subMesh, uint32_t skinId) const
{
	auto mdl = mdlC.GetModel();
	if(mdl == nullptr)
		return nullptr;
	auto baseTexIdx = subMesh.GetSkinTextureIndex();
	return mdlC.GetRenderMaterial(baseTexIdx, skinId);
}

pragma::scenekit::PShader pragma::modules::scenekit::Cache::CreateShader(const std::string &meshName, asset::Model &mdl, geometry::ModelSubMesh &subMesh, pragma::ecs::BaseEntity *optEnt, uint32_t skinId) const
{
	// Make sure all textures have finished loading
	static_cast<material::CMaterialManager &>(pragma::get_client_state()->GetMaterialManager()).GetTextureManager().WaitForAllPendingCompleted();

	auto *mat = optEnt ? GetMaterial(*optEnt, subMesh, skinId) : GetMaterial(mdl, subMesh, skinId);
	if(mat == nullptr)
		return nullptr;
	ShaderInfo shaderInfo {};
	if(optEnt)
		shaderInfo.entity = optEnt;
	shaderInfo.subMesh = &subMesh;
	return CreateShader(*mat, meshName, shaderInfo);
}

void pragma::modules::scenekit::Cache::AddMesh(asset::Model &mdl, pragma::scenekit::Mesh &mesh, geometry::ModelSubMesh &mdlMesh, pragma::CModelComponent *optMdlC, pragma::CAnimatedComponent *optAnimC)
{
	auto meshData = CalcMeshData(mdl, mdlMesh, mesh.HasAlphas(), mesh.HasWrinkles(), optMdlC, optAnimC);
	if(meshData == nullptr)
		return;
	AddMeshDataToMesh(mesh, *meshData);
}

pragma::scenekit::PMesh pragma::modules::scenekit::Cache::BuildMesh(const std::string &meshName, const std::vector<std::shared_ptr<MeshData>> &meshDatas, const std::optional<pragma::math::ScaledTransform> &pose) const
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

	auto flags = pragma::scenekit::Mesh::Flags::None;
	if(hasAlphas)
		flags |= pragma::scenekit::Mesh::Flags::HasAlphas;
	if(hasWrinkles)
		flags |= pragma::scenekit::Mesh::Flags::HasWrinkles;
	auto mesh = pragma::scenekit::Mesh::Create(meshName, numVerts, numTris / 3, flags);
	m_mdlCache->GetChunks().front().AddMesh(*mesh);
	for(auto &meshData : meshDatas)
		AddMeshDataToMesh(*mesh, *meshData, pose);
	return mesh;
}

void pragma::modules::scenekit::Cache::AddMeshDataToMesh(pragma::scenekit::Mesh &mesh, const MeshData &meshData, const std::optional<pragma::math::ScaledTransform> &pose) const
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

void pragma::modules::scenekit::Cache::AddAOBakeTarget(pragma::ecs::BaseEntity *optEnt, asset::Model &mdl, uint32_t matIndex, std::shared_ptr<pragma::scenekit::Object> &oAo, std::shared_ptr<pragma::scenekit::Object> &oEnv)
{
	std::vector<std::shared_ptr<MeshData>> materialMeshes;
	std::vector<std::shared_ptr<MeshData>> envMeshes;
	auto fFilterMeshes = [this, matIndex, &materialMeshes, &envMeshes, &mdl](geometry::ModelSubMesh &mesh, const pragma::math::ScaledTransform &pose) -> bool {
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
	oAo = pragma::scenekit::Object::Create(*mesh);
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
	oEnv = pragma::scenekit::Object::Create(*meshEnv);
	m_mdlCache->GetChunks().front().AddObject(*oEnv);
}

void pragma::modules::scenekit::Cache::AddAOBakeTarget(pragma::ecs::BaseEntity &ent, uint32_t matIndex, std::shared_ptr<pragma::scenekit::Object> &oAo, std::shared_ptr<pragma::scenekit::Object> &oEnv)
{
	auto mdl = ent.GetModel();
	if(mdl == nullptr)
		return;
	AddAOBakeTarget(&ent, *mdl, matIndex, oAo, oEnv);
}

void pragma::modules::scenekit::Cache::AddAOBakeTarget(asset::Model &mdl, uint32_t matIndex, std::shared_ptr<pragma::scenekit::Object> &oAo, std::shared_ptr<pragma::scenekit::Object> &oEnv) { AddAOBakeTarget(nullptr, mdl, matIndex, oAo, oEnv); }
