/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#include "pr_cycles/progressive_refinement.hpp"
#include <pragma/c_engine.h>
#include <util_image_buffer.hpp>
#include <image/prosper_texture.hpp>
#include <prosper_command_buffer.hpp>

extern DLLCLIENT CEngine *c_engine;

using namespace pragma::modules::cycles;

DenoiseTexture::DenoiseTexture(uint32_t w,uint32_t h)
{
	m_inputImage = uimg::ImageBuffer::Create(w,h,uimg::Format::RGB_FLOAT);
	m_denoisedImage = uimg::ImageBuffer::Create(w,h,uimg::Format::RGB_FLOAT);
	m_outputImage = uimg::ImageBuffer::Create(w,h,uimg::Format::RGBA_HDR);
	m_running = true;
	m_thread = std::thread{[this]() {
		while(m_running)
			UpdatePendingTiles();
	}};
}

void DenoiseTexture::UpdatePendingTiles()
{
	m_tileMutex.lock();
		auto tiles = std::move(m_pendingTiles);
		m_pendingTiles = {};
	m_tileMutex.unlock();
	while(tiles.empty() == false)
	{
		auto &tileData = tiles.front();
		// TODO
		//tileData.image->Copy(*m_inputImage,0,0,tileData.x,tileData.y,tileData.image->GetWidth(),tileData.image->GetHeight());
		tiles.pop();
	}

	if(m_shouldDenoise)
	{
		m_shouldDenoise = false;
		m_denoisingState = DenoisingState::Denoising;
		RunDenoise();
		m_denoisingState = DenoisingState::Complete;
	}
}

void DenoiseTexture::AppendTile(unirender::TileManager::TileData &&tileData)
{
	m_tileMutex.lock();
		m_pendingTiles.push(std::move(tileData));
	m_tileMutex.unlock();
}
std::shared_ptr<uimg::ImageBuffer> DenoiseTexture::GetDenoisedImageData() const {return m_outputImage;}

void DenoiseTexture::Denoise() {m_shouldDenoise = true;}
bool DenoiseTexture::IsDenoising() const {return m_denoisingState == DenoisingState::Denoising;}
bool DenoiseTexture::IsDenoisingComplete() const {return m_denoisingState == DenoisingState::Complete;}

void DenoiseTexture::RunDenoise()
{
	auto w = m_denoisedImage->GetWidth();
	auto h = m_denoisedImage->GetHeight();
	unirender::denoise::Info denoiseInfo {};
	denoiseInfo.hdr = true;
	denoiseInfo.width = w;
	denoiseInfo.height = h;

	unirender::denoise::ImageData output {};
	output.data = static_cast<uint8_t*>(m_inputImage->GetData());
	output.format = m_inputImage->GetFormat();

	unirender::denoise::ImageInputs inputs {};
	inputs.beautyImage = output;

	m_denoiser.Denoise(denoiseInfo,inputs,output);
	m_denoisedImage->Copy(*m_outputImage,0,0,0,0,w,h);
}

DenoiseTexture::~DenoiseTexture()
{
	m_running = false;
	m_thread.join();
}

//////////

ProgressiveTexture::~ProgressiveTexture()
{
	if(m_cbThink.IsValid())
		m_cbThink.Remove();
}
std::shared_ptr<prosper::Texture> ProgressiveTexture::GetTexture() const {return m_texture;}
void ProgressiveTexture::Initialize(unirender::Renderer &renderer)
{
	m_renderer = renderer.shared_from_this();
	auto &scene = m_renderer->GetScene();
	m_tileSize = m_renderer->GetTileManager().GetTileSize();
	auto res = scene.GetResolution();

	auto &context = c_engine->GetRenderContext();
	prosper::util::ImageCreateInfo imgCreateInfo {};
	imgCreateInfo.width = res.x;
	imgCreateInfo.height = res.y;
	imgCreateInfo.format = renderer.ShouldUseProgressiveFloatFormat() ? prosper::Format::R32G32B32A32_SFloat : prosper::Format::R16G16B16A16_SFloat;
	imgCreateInfo.usage = prosper::ImageUsageFlags::SampledBit/* | prosper::ImageUsageFlags::ColorAttachmentBit*/ | prosper::ImageUsageFlags::TransferSrcBit | prosper::ImageUsageFlags::TransferDstBit;
	imgCreateInfo.memoryFeatures = prosper::MemoryFeatureFlags::HostAccessable | prosper::MemoryFeatureFlags::HostCached;
	imgCreateInfo.tiling = prosper::ImageTiling::Linear;
	imgCreateInfo.postCreateLayout = prosper::ImageLayout::TransferDstOptimal;

	// Clear alpha
	auto img = context.CreateImage(imgCreateInfo);
	auto &setupCmd = context.GetSetupCommandBuffer();
	setupCmd->RecordClearImage(*img,prosper::ImageLayout::TransferDstOptimal,std::array<float,4>{0.f,0.f,0.f,0.f});
	setupCmd->RecordImageBarrier(*img,prosper::ImageLayout::TransferDstOptimal,prosper::ImageLayout::ShaderReadOnlyOptimal);
	context.FlushSetupCommandBuffer();

	prosper::util::SamplerCreateInfo samplerCreateInfo {};
	samplerCreateInfo.addressModeU = prosper::SamplerAddressMode::ClampToEdge;
	samplerCreateInfo.addressModeV = prosper::SamplerAddressMode::ClampToEdge;
	samplerCreateInfo.addressModeW = prosper::SamplerAddressMode::ClampToEdge;
	auto tex = context.CreateTexture({},*img,prosper::util::ImageViewCreateInfo{},samplerCreateInfo);
	tex->SetDebugName("rt_tile_realtime");
	m_image = img;
	m_texture = tex;

	// m_denoiseTexture = std::make_unique<DenoiseTexture>(w,h);
	m_cbThink = c_engine->AddCallback("Think",FunctionCallback<void>::Create([this]() {
		Update();
	}));
	m_tDenoise = std::chrono::steady_clock::now();
}
void ProgressiveTexture::Update()
{
	auto &scene = m_renderer->GetScene();
	auto &tileManager = m_renderer->GetTileManager();
	// We'll wait for all tiles to have at least 1 finished sample before we write the image data, otherwise
	// the image can look confusing
	if(tileManager.AllTilesHaveRenderedSamples() == false)
		return;
	auto tiles = m_renderer->GetRenderedTileBatch();
	/*if(m_denoiseTexture->IsDenoisingComplete())
	{
		auto imgSrc = m_denoiseTexture->GetDenoisedImageData();
		m_image->WriteImageData(0,0,imgSrc->GetWidth(),imgSrc->GetHeight(),0,0,imgSrc->GetSize(),static_cast<uint8_t*>(imgSrc->GetData()));
	}*/
	for(auto &tile : tiles)
	{
		m_image->WriteImageData(tile.x,tile.y,tile.w,tile.h,0,0,tile.data.size() *sizeof(tile.data.front()),tile.data.data());
		//if(m_denoiseTexture && (tile.sample %5 == 0))
		//	m_denoiseTexture->AppendTile(std::move(tile));
	}
	/*if(m_denoiseTexture->IsDenoising() == false)
	{
		auto tDt = std::chrono::steady_clock::now() -m_tDenoise;
		if(std::chrono::duration_cast<std::chrono::seconds>(tDt).count() >= 3)
		{
			if(m_denoise == false)
			{
				m_denoise = true;
				m_denoiseTexture->Denoise();
			}
		}
	}*/
}
