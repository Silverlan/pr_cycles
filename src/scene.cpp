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
#include <render/svm.h>
#include <optional>
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

using namespace pragma::modules;

#pragma optimize("",off)
cycles::PScene cycles::Scene::Create()
{
	std::optional<ccl::DeviceInfo> device = {};
	for(auto &devInfo : ccl::Device::available_devices(ccl::DeviceTypeMask::DEVICE_MASK_CUDA | ccl::DeviceTypeMask::DEVICE_MASK_OPENCL | ccl::DeviceTypeMask::DEVICE_MASK_CPU))
	{
		switch(devInfo.type)
		{
		case ccl::DeviceType::DEVICE_CUDA:
		case ccl::DeviceType::DEVICE_OPENCL:
			// TODO: GPU devices currently don't seem to work, they just create a new instance of the program?
			// device = devInfo; // GPU device; We'll just use this one
			// goto endLoop;
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
	sessionParams.progressive = true;
	sessionParams.background = true;
	sessionParams.start_resolution = 64;
	// sessionParams.samples = 128;
	// We need the scene-pointer in the callback-function, however the function has to be
	// defined before the scene is created, so we create a shared pointer that will be initialized after
	// the scene.
	auto ptrScene = std::make_shared<Scene*>(nullptr);
	sessionParams.write_render_cb = [ptrScene](const ccl::uchar *pixels,int w,int h,int channels) -> bool {
		return (*ptrScene)->WriteRender(pixels,w,h,channels);
	};

	auto session = std::make_unique<ccl::Session>(sessionParams);

	ccl::SceneParams sceneParams {};
	sceneParams.shadingsystem = ccl::SHADINGSYSTEM_SVM;

	auto *cclScene = new ccl::Scene{sceneParams,session->device}; // Object will be removed automatically by cycles
	cclScene->params.bvh_type = ccl::SceneParams::BVH_STATIC;

	auto *pSession = session.get();
	auto scene = PScene{new Scene{std::move(session),*cclScene}};
	auto *pScene = scene.get();
	pSession->progress.set_update_callback([pScene]() {
		pScene->SessionPrintStatus();
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
	m_objects.clear();
	m_shaders.clear();
	m_camera = nullptr;
	m_session = nullptr;
}

cycles::Camera &cycles::Scene::GetCamera() {return *m_camera;}

static std::optional<std::string> get_texture_path(TextureInfo *texInfo)
{
	if(texInfo == nullptr)
		return {};
	auto fileName = "materials\\" +texInfo->name;
	std::string absPath;
	if(FileManager::FindAbsolutePath(fileName,absPath) == false)
	{
		fileName = "materials\\error.dds";
		if(FileManager::FindAbsolutePath(fileName,absPath) == false)
			return {};
	}
	return absPath;
}

cycles::PShader cycles::Scene::CreateShader(const std::string &meshName,Model &mdl,ModelSubMesh &subMesh)
{
	auto texIdx = subMesh.GetTexture();
	auto *mat = mdl.GetMaterial(texIdx);
	auto *diffuseMap = mat ? mat->GetDiffuseMap() : nullptr;
	auto diffuseTexPath = get_texture_path(diffuseMap);
	if(diffuseTexPath.has_value() == false)
		return nullptr;
	auto shader = cycles::Shader::Create(*this,"floor");

	// Albedo map
	auto nodeAlbedo = shader->AddNode("image_texture","albedo");
	static_cast<ccl::ImageTextureNode*>(**nodeAlbedo)->filename = *diffuseTexPath;

	const std::string bsdfName = "bsdf_disney";
	auto nodeBsdf = shader->AddNode("principled_bsdf",bsdfName);
	auto *pNodeBsdf = static_cast<ccl::PrincipledBsdfNode*>(**nodeBsdf);

	// Normal map
	auto normalTexPath = get_texture_path(mat->GetNormalMap());
	if(normalTexPath)
	{
#if 0
		auto *nodeNormal = static_cast<ccl::ImageTextureNode*>(**shader->AddNode("image_texture","normal"));
		nodeNormal->filename = *normalTexPath;
		nodeNormal->colorspace = "__builtin_raw";

		// Separate rgb components of input image
		auto *nodeSeparate = static_cast<ccl::SeparateRGBNode*>(**shader->AddNode("separate_rgb","separate_normal"));
		shader->Link("normal","color","separate_normal","color");

		// Invert y-axis of normal
		auto *nodeInvert = static_cast<ccl::MathNode*>(**shader->AddNode("math","invert_normal_y"));
		nodeInvert->type = ccl::NodeMathType::NODE_MATH_SUBTRACT;
		nodeInvert->value1 = 1.f;
		shader->Link("separate_normal","g","invert_normal_y","value2");

		auto *nodeInvertZ = static_cast<ccl::MathNode*>(**shader->AddNode("math","invert_normal_z"));
		nodeInvertZ->type = ccl::NodeMathType::NODE_MATH_SUBTRACT;
		nodeInvertZ->value1 = 1.f;
		shader->Link("separate_normal","b","invert_normal_z","value2");

		/*{
			auto *nodeR = static_cast<ccl::ValueNode*>(**shader->AddNode("value","normal_r"));
			nodeR->value = 0.f;

			auto *nodeG = static_cast<ccl::ValueNode*>(**shader->AddNode("value","normal_g"));
			nodeG->value = 1.f;

			auto *nodeB = static_cast<ccl::ValueNode*>(**shader->AddNode("value","normal_b"));
			nodeB->value = 0.f;
		}*/

		// Re-combine rgb components
		auto *nodeCombine = static_cast<ccl::MathNode*>(**shader->AddNode("combine_rgb","combine_normal"));
		shader->Link("separate_normal","r","combine_normal","r");
		shader->Link("separate_normal","g","combine_normal","g");
		shader->Link("separate_normal","b","combine_normal","b");

		//return ccl::float3{n.x,-n.y,-n.z};
		//shader->Link("normal_r","value","combine_normal","r");
		//shader->Link("normal_g","value","combine_normal","g");
		//shader->Link("normal_b","value","combine_normal","b");

		// Link normal image color to tangent-space normal node
		auto *nodeNormalMap = static_cast<ccl::NormalMapNode*>(**shader->AddNode("normal_map","normal_mapp"));
		nodeNormalMap->color = {0.f,1.f,0.f};
		shader->Link("normal","color","normal_mapp","color");
		//nodeNormalMap->color = {0.f,1.f,0.f};
		//shader->Link("normal","color","normal_map","color");

		// Link to BSDF normal
		//shader->Link("normal_mapp","normal",bsdfName,"normal");
		shader->Link("normal_mapp","normal",bsdfName,"normal");
#endif
		//auto *nodeUvTest = static_cast<ccl::UVMapNode*>(**shader->AddNode("uvmap","uvmaptest"));

		auto *nodeImgNormal = static_cast<ccl::ImageTextureNode*>(**shader->AddNode("image_texture","img_normal_map"));
		nodeImgNormal->filename = *normalTexPath;
		nodeImgNormal->colorspace = ccl::u_colorspace_raw;

		{
			// Separate rgb components of input image
			auto *nodeSeparate = static_cast<ccl::SeparateRGBNode*>(**shader->AddNode("separate_rgb","separate_normal"));
			shader->Link("img_normal_map","color","separate_normal","color");

			// Invert y-axis of normal
			auto *nodeInvert = static_cast<ccl::MathNode*>(**shader->AddNode("math","invert_normal_y"));
			nodeInvert->type = ccl::NodeMathType::NODE_MATH_SUBTRACT;
			nodeInvert->value1 = 1.f;
			shader->Link("separate_normal","g","invert_normal_y","value2");

			auto *nodeInvertZ = static_cast<ccl::MathNode*>(**shader->AddNode("math","invert_normal_z"));
			nodeInvertZ->type = ccl::NodeMathType::NODE_MATH_SUBTRACT;
			nodeInvertZ->value1 = 1.f;
			shader->Link("separate_normal","r","invert_normal_z","value2");

			/*{
			auto *nodeR = static_cast<ccl::ValueNode*>(**shader->AddNode("value","normal_r"));
			nodeR->value = 0.f;

			auto *nodeG = static_cast<ccl::ValueNode*>(**shader->AddNode("value","normal_g"));
			nodeG->value = 1.f;

			auto *nodeB = static_cast<ccl::ValueNode*>(**shader->AddNode("value","normal_b"));
			nodeB->value = 0.f;
			}*/

			// Re-combine rgb components
			auto *nodeCombine = static_cast<ccl::CombineRGBNode*>(**shader->AddNode("combine_rgb","combine_normal"));
			shader->Link("invert_normal_z","value","combine_normal","r");
			shader->Link("invert_normal_y","value","combine_normal","g");
			shader->Link("separate_normal","b","combine_normal","b");
		}


		//auto *nodeNormalMap = static_cast<ccl::NormalMapNode*>(**shader->AddNode("normal_map","nmap"));
		//nodeNormalMap->color = {0.f,1.f,0.f};
		//shader->Link("img_normal_map","color","nmap","color");

		//shader->Link("uvmaptest","UV","nmap","attribute");

		static auto normal = ccl::make_float3(0.5f, 0.5f, 1.0f);
		auto *nodeNormalMap = static_cast<ccl::NormalMapNode*>(**shader->AddNode("normal_map","nmap"));
		nodeNormalMap->color = normal;
		nodeNormalMap->space = ccl::NodeNormalMapSpace::NODE_NORMAL_MAP_TANGENT;
		nodeNormalMap->attribute = meshName;
		shader->Link("combine_normal","image","nmap","color");
		//shader->Link("combine_normal","image","nmap","color");
		//nodeNormalMap->attribute = "player";
		//shader->Link("nmap","normal","output","surface");

		//nodeNormalMap->attributes
		//auto *attrN = mesh->attributes.add(ccl::ATTR_STD_VERTEX_NORMAL);
		//if(attrN)
		//	attrN->resize(numVerts);

		//const AttributeDescriptor attr = find_attribute(kg, sd, node.z);
		//const AttributeDescriptor attr_sign = find_attribute(kg, sd, node.w);
		//const AttributeDescriptor attr_normal = find_attribute(kg, sd, ATTR_STD_VERTEX_NORMAL);


		shader->Link("nmap","normal",bsdfName,"normal");

		//shader->Link("nmap","normal","output","surface");
	}

	// Metalness map
	auto metalnessTexPath = get_texture_path(mat->GetTextureInfo("metalness_map"));
	if(metalnessTexPath)
	{
		auto *nodeMetalness = static_cast<ccl::ImageTextureNode*>(**shader->AddNode("image_texture","img_metalness_map"));
		nodeMetalness->filename = *metalnessTexPath;

		// We only need one channel value, so we'll just grab the red channel
		auto *nodeSeparate = static_cast<ccl::SeparateRGBNode*>(**shader->AddNode("separate_rgb","separate_metalness"));
		shader->Link("img_metalness_map","color","separate_metalness","color");

		// Use float as metallic value
		shader->Link("separate_metalness","r",bsdfName,"metallic");
	}

	// Roughness map
	auto roughnessTexPath = get_texture_path(mat->GetTextureInfo("roughness_map"));
	if(roughnessTexPath)
	{
		auto nodeRoughness = static_cast<ccl::ImageTextureNode*>(**shader->AddNode("image_texture","img_roughness_map"));
		nodeRoughness->filename = *roughnessTexPath;

		// We only need one channel value, so we'll just grab the red channel
		auto *nodeSeparate = static_cast<ccl::SeparateRGBNode*>(**shader->AddNode("separate_rgb","separate_roughness"));
		shader->Link("img_roughness_map","color","separate_roughness","color");

		// Use float as roughness value
		shader->Link("separate_roughness","r",bsdfName,"roughness");
	}

	// Emission map
	auto emissionTexPath = get_texture_path(mat->GetGlowMap());
	if(emissionTexPath)
	{
		auto nodeEmission = shader->AddNode("image_texture","emission");
		static_cast<ccl::ImageTextureNode*>(**nodeEmission)->filename = *emissionTexPath;
		shader->Link("emission","color",bsdfName,"emission");
	}

	shader->Link("albedo","color",bsdfName,"base_color");
	shader->Link(bsdfName,"bsdf","output","surface");
	return shader;
}

void cycles::Scene::AddEntity(BaseEntity &ent)
{
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

	m_session->start();

	m_session->wait();
}

ccl::Scene *cycles::Scene::operator->() {return &m_scene;}
ccl::Scene *cycles::Scene::operator*() {return &m_scene;}

void cycles::Scene::SessionPrint(const ccl::string &str)
{
	/* print with carriage return to overwrite previous */
	std::cout<<str.c_str()<<std::endl;

	/* add spaces to overwrite longer previous print */
	static int maxlen = 0;
	int len = str.size();
	maxlen = ccl::max(len, maxlen);

	for (int i = len; i < maxlen; i++)
		std::cout<<" ";

	/* flush because we don't write an end of line */
	std::flush(std::cout);
}

void cycles::Scene::SessionPrintStatus()
{
	ccl::string status, substatus;

	/* get status */
	float progress = m_session->progress.get_progress();
	m_session->progress.get_status(status, substatus);

	if (substatus != "")
		status += ": " + substatus;

	/* print status */
	status = ccl::string_printf("Progress %05.2f   %s", (double)progress * 100, status.c_str());
	SessionPrint(status);
}

bool cycles::Scene::WriteRender(const ccl::uchar *pixels, int w, int h, int channels)
{
	std::string outputPath = "E:/projects/pragma/build_winx64/output/modules/cycles/examples/monkey.jpg";
	ccl::string msg = ccl::string_printf("Writing image %s",outputPath.c_str());
	SessionPrint(msg);

	ccl::unique_ptr<ccl::ImageOutput> out = ccl::unique_ptr<ccl::ImageOutput>(ccl::ImageOutput::create(outputPath));
	if (!out) {
		return false;
	}

	ccl::ImageSpec spec(w, h, channels, ccl::TypeDesc::UINT8);
	if (!out->open(outputPath, spec)) {
		return false;
	}

	/* conversion for different top/bottom convention */
	out->write_image(
		ccl::TypeDesc::UINT8, pixels + (h - 1) * w * channels, ccl::AutoStride, -w * channels, ccl::AutoStride);

	out->close();

	return true;
}

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
