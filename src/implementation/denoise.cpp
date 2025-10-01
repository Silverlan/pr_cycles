// SPDX-FileCopyrightText: (c) 2024 Silverlan <opensource@pragma-engine.com>
// SPDX-License-Identifier: MIT

module;

#include <util_image_buffer.hpp>
#include <sharedutils/util_parallel_job.hpp>

module pragma.modules.scenekit;

import pragma.scenekit;
import :scene;

class DenoiseWorker : public util::ParallelWorker<std::shared_ptr<uimg::ImageBuffer>> {
  public:
	DenoiseWorker(uimg::ImageBuffer &imgBuffer);
	using util::ParallelWorker<std::shared_ptr<uimg::ImageBuffer>>::Cancel;
	virtual std::shared_ptr<uimg::ImageBuffer> GetResult() override;
  private:
	std::shared_ptr<uimg::ImageBuffer> m_imgBuffer = nullptr;
	template<typename TJob, typename... TARGS>
	friend util::ParallelJob<typename TJob::RESULT_TYPE> util::create_parallel_job(TARGS &&...args);
};

DenoiseWorker::DenoiseWorker(uimg::ImageBuffer &imgBuffer) : m_imgBuffer {imgBuffer.shared_from_this()}
{
	AddThread([this]() {
		pragma::scenekit::denoise::Info denoiseInfo {};
		auto success = pragma::scenekit::denoise::denoise(denoiseInfo, *m_imgBuffer, nullptr, nullptr, [this](float progress) -> bool {
			UpdateProgress(progress);
			return !IsCancelled();
		});
		if(IsCancelled())
			return;
		SetStatus(success ? util::JobStatus::Successful : util::JobStatus::Failed);
	});
}
std::shared_ptr<uimg::ImageBuffer> DenoiseWorker::GetResult() { return m_imgBuffer; }

util::ParallelJob<std::shared_ptr<uimg::ImageBuffer>> pragma::modules::scenekit::denoise(uimg::ImageBuffer &imgBuffer) { return util::create_parallel_job<DenoiseWorker>(imgBuffer); }
