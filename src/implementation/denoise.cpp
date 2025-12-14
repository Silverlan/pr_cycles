// SPDX-FileCopyrightText: (c) 2024 Silverlan <opensource@pragma-engine.com>
// SPDX-License-Identifier: MIT

module pragma.modules.scenekit;

import pragma.scenekit;
import :scene;

class DenoiseWorker : public pragma::util::ParallelWorker<std::shared_ptr<pragma::image::ImageBuffer>> {
  public:
	DenoiseWorker(pragma::image::ImageBuffer &imgBuffer);
	using pragma::util::ParallelWorker<std::shared_ptr<pragma::image::ImageBuffer>>::Cancel;
	virtual std::shared_ptr<pragma::image::ImageBuffer> GetResult() override;
  private:
	std::shared_ptr<pragma::image::ImageBuffer> m_imgBuffer = nullptr;
	template<typename TJob, typename... TARGS>
	friend pragma::util::ParallelJob<typename TJob::RESULT_TYPE> pragma::util::create_parallel_job(TARGS &&...args);
};

DenoiseWorker::DenoiseWorker(pragma::image::ImageBuffer &imgBuffer) : m_imgBuffer {imgBuffer.shared_from_this()}
{
	AddThread([this]() {
		pragma::scenekit::denoise::Info denoiseInfo {};
		auto success = pragma::scenekit::denoise::denoise(denoiseInfo, *m_imgBuffer, nullptr, nullptr, [this](float progress) -> bool {
			UpdateProgress(progress);
			return !IsCancelled();
		});
		if(IsCancelled())
			return;
		SetStatus(success ? pragma::util::JobStatus::Successful : pragma::util::JobStatus::Failed);
	});
}
std::shared_ptr<pragma::image::ImageBuffer> DenoiseWorker::GetResult() { return m_imgBuffer; }

pragma::util::ParallelJob<std::shared_ptr<pragma::image::ImageBuffer>> pragma::modules::scenekit::denoise(image::ImageBuffer &imgBuffer) { return pragma::util::create_parallel_job<DenoiseWorker>(imgBuffer); }
