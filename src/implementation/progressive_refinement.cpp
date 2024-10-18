/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

module;

#include <pragma/c_engine.h>
#include <util_image_buffer.hpp>
#include <image/prosper_texture.hpp>
#include <prosper_command_buffer.hpp>
#include <prosper_fence.hpp>
#include <future>
#include <deque>
#include <queue>

module pragma.modules.scenekit;
import :progressive_refinement;

extern DLLCLIENT CEngine *c_engine;

using namespace pragma::modules::scenekit;

DenoiseTexture::DenoiseTexture(uint32_t w, uint32_t h)
{
	m_inputImage = uimg::ImageBuffer::Create(w, h, uimg::Format::RGB_FLOAT);
	m_denoisedImage = uimg::ImageBuffer::Create(w, h, uimg::Format::RGB_FLOAT);
	m_outputImage = uimg::ImageBuffer::Create(w, h, uimg::Format::RGBA_HDR);
	m_running = true;
	m_thread = std::thread {[this]() {
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
	while(tiles.empty() == false) {
		auto &tileData = tiles.front();
		// TODO
		//tileData.image->Copy(*m_inputImage,0,0,tileData.x,tileData.y,tileData.image->GetWidth(),tileData.image->GetHeight());
		tiles.pop();
	}

	if(m_shouldDenoise) {
		m_shouldDenoise = false;
		m_denoisingState = DenoisingState::Denoising;
		RunDenoise();
		m_denoisingState = DenoisingState::Complete;
	}
}

void DenoiseTexture::AppendTile(pragma::scenekit::TileManager::TileData &&tileData)
{
	m_tileMutex.lock();
	m_pendingTiles.push(std::move(tileData));
	m_tileMutex.unlock();
}
std::shared_ptr<uimg::ImageBuffer> DenoiseTexture::GetDenoisedImageData() const { return m_outputImage; }

void DenoiseTexture::Denoise() { m_shouldDenoise = true; }
bool DenoiseTexture::IsDenoising() const { return m_denoisingState == DenoisingState::Denoising; }
bool DenoiseTexture::IsDenoisingComplete() const { return m_denoisingState == DenoisingState::Complete; }

void DenoiseTexture::RunDenoise()
{
	auto w = m_denoisedImage->GetWidth();
	auto h = m_denoisedImage->GetHeight();
	pragma::scenekit::denoise::Info denoiseInfo {};
	denoiseInfo.hdr = true;
	denoiseInfo.width = w;
	denoiseInfo.height = h;

	pragma::scenekit::denoise::ImageData output {};
	output.data = static_cast<uint8_t *>(m_inputImage->GetData());
	output.format = m_inputImage->GetFormat();

	pragma::scenekit::denoise::ImageInputs inputs {};
	inputs.beautyImage = output;

	m_denoiser.Denoise(denoiseInfo, inputs, output);
	m_denoisedImage->Copy(*m_outputImage, 0, 0, 0, 0, w, h);
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
	c_engine->GetRenderContext().KeepResourceAliveUntilPresentationComplete(m_cmdBuffer);
}
std::shared_ptr<prosper::Texture> ProgressiveTexture::GetTexture() const { return m_texture; }
void ProgressiveTexture::Initialize(pragma::scenekit::Renderer &renderer)
{
	m_renderer = renderer.shared_from_this();
	auto &scene = m_renderer->GetScene();
	m_tileSize = m_renderer->GetTileManager().GetTileSize();
	auto res = scene.GetResolution();

	auto &context = c_engine->GetRenderContext();
	auto img = CreateImage(res.x, res.y, true);
	m_image = img;

	uint32_t queueFamilyIndex;
	m_cmdBuffer = context.AllocatePrimaryLevelCommandBuffer(prosper::QueueFamilyType::Universal, queueFamilyIndex);
	m_fence = context.CreateFence(true);
	auto result = m_cmdBuffer->StartRecording(false, false);
	assert(result);

	// Clear alpha
	m_cmdBuffer->RecordClearImage(*img, prosper::ImageLayout::TransferDstOptimal, std::array<float, 4> {0.f, 0.f, 0.f, 0.f});
	m_cmdBuffer->RecordImageBarrier(*img, prosper::ImageLayout::TransferDstOptimal, prosper::ImageLayout::ShaderReadOnlyOptimal);
	m_cmdBuffer->StopRecording();
	context.SubmitCommandBuffer(*m_cmdBuffer, prosper::QueueFamilyType::Universal, true);

	prosper::util::SamplerCreateInfo samplerCreateInfo {};
	samplerCreateInfo.addressModeU = prosper::SamplerAddressMode::ClampToEdge;
	samplerCreateInfo.addressModeV = prosper::SamplerAddressMode::ClampToEdge;
	samplerCreateInfo.addressModeW = prosper::SamplerAddressMode::ClampToEdge;
	auto tex = context.CreateTexture({}, *img, prosper::util::ImageViewCreateInfo {}, samplerCreateInfo);
	tex->SetDebugName("rt_tile_realtime");
	m_image = img;
	m_texture = tex;

	// m_denoiseTexture = std::make_unique<DenoiseTexture>(w,h);
	m_cbThink = c_engine->AddCallback("Think", FunctionCallback<void>::Create([this]() { Update(); }));
}
std::shared_ptr<prosper::IImage> ProgressiveTexture::CreateImage(uint32_t width, uint32_t height, bool onDevice) const
{
	auto &context = c_engine->GetRenderContext();
	prosper::util::ImageCreateInfo imgCreateInfo {};
	imgCreateInfo.width = width;
	imgCreateInfo.height = height;
	imgCreateInfo.format = m_renderer->ShouldUseProgressiveFloatFormat() ? prosper::Format::R32G32B32A32_SFloat : prosper::Format::R16G16B16A16_SFloat;
	imgCreateInfo.usage = prosper::ImageUsageFlags::TransferSrcBit | prosper::ImageUsageFlags::TransferDstBit;
	if(onDevice)
		imgCreateInfo.usage |= prosper::ImageUsageFlags::SampledBit;
	imgCreateInfo.memoryFeatures = onDevice ? prosper::MemoryFeatureFlags::DeviceLocal : prosper::MemoryFeatureFlags::HostAccessable | prosper::MemoryFeatureFlags::HostCached;
	imgCreateInfo.tiling = onDevice ? prosper::ImageTiling::Optimal : prosper::ImageTiling::Linear;
	imgCreateInfo.postCreateLayout = onDevice ? prosper::ImageLayout::TransferDstOptimal : prosper::ImageLayout::TransferSrcOptimal;
	return context.CreateImage(imgCreateInfo);
}
void ProgressiveTexture::Update()
{
	auto &scene = m_renderer->GetScene();
	auto &tileManager = m_renderer->GetTileManager();
	// We'll wait for all tiles to have at least 1 finished sample before we write the image data
	if(tileManager.AllTilesHaveRenderedSamples() == false)
		return;
	auto tiles = m_renderer->GetRenderedTileBatch();
	if(tiles.empty())
		return;
	auto &tile = tiles.back();
	auto it = std::find_if(m_mipImages.begin(), m_mipImages.end(), [&tile](const std::shared_ptr<prosper::IImage> &img) { return img->GetWidth() == tile.w && img->GetHeight() == tile.h; });
	if(it == m_mipImages.end()) {
		auto newImg = CreateImage(tile.w, tile.h, false);
		m_mipImages.push_back(newImg);
		it = m_mipImages.end() - 1;
	}

	auto &imgData = **it;
	imgData.WriteImageData(tile.x, tile.y, tile.w, tile.h, 0, 0, tile.data.size() * sizeof(tile.data.front()), tile.data.data());

	auto &context = c_engine->GetRenderContext();
	context.WaitForFence(*m_fence);
	auto res = m_cmdBuffer->StartRecording(false, false);
	assert(res);
	if(!res)
		return;
	m_fence->Reset();
	prosper::util::BarrierImageLayout {};
	// Ensure image data has been written by host before blitting to destination image
	m_cmdBuffer->RecordImageBarrier(imgData, prosper::PipelineStageFlags::HostBit, prosper::PipelineStageFlags::TransferBit, prosper::ImageLayout::TransferSrcOptimal, prosper::ImageLayout::TransferSrcOptimal, prosper::AccessFlags::HostWriteBit, prosper::AccessFlags::TransferReadBit);

	m_cmdBuffer->RecordImageBarrier(*m_image, prosper::ImageLayout::ShaderReadOnlyOptimal, prosper::ImageLayout::TransferDstOptimal);
	m_cmdBuffer->RecordBlitImage({}, imgData, *m_image);
	m_cmdBuffer->RecordImageBarrier(*m_image, prosper::ImageLayout::TransferDstOptimal, prosper::ImageLayout::ShaderReadOnlyOptimal);

	m_cmdBuffer->StopRecording();
	context.SubmitCommandBuffer(*m_cmdBuffer, prosper::QueueFamilyType::Universal, false, m_fence.get());
}
