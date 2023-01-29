/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

#include <util_raytracing/denoise.hpp>
#include <util_image_buffer.hpp>
#include <sharedutils/util_parallel_job.hpp>
#include "pr_cycles/scene.hpp"

class DenoiseWorker
	: public util::ParallelWorker<std::shared_ptr<uimg::ImageBuffer>>
{
public:
	DenoiseWorker(uimg::ImageBuffer &imgBuffer);
	using util::ParallelWorker<std::shared_ptr<uimg::ImageBuffer>>::Cancel;
	virtual std::shared_ptr<uimg::ImageBuffer> GetResult() override;
private:
	std::shared_ptr<uimg::ImageBuffer> m_imgBuffer = nullptr;
	template<typename TJob,typename... TARGS>
		friend util::ParallelJob<typename TJob::RESULT_TYPE> util::create_parallel_job(TARGS&& ...args);
};

DenoiseWorker::DenoiseWorker(uimg::ImageBuffer &imgBuffer)
	: m_imgBuffer{imgBuffer.shared_from_this()}
{
	AddThread([this]() {
		unirender::denoise::Info denoiseInfo {};
		auto success = unirender::denoise::denoise(denoiseInfo,*m_imgBuffer,nullptr,nullptr,[this](float progress) -> bool {
			UpdateProgress(progress);
			return !IsCancelled();
		});
		if(IsCancelled())
			return;
		SetStatus(success ? util::JobStatus::Successful : util::JobStatus::Failed);
	});
}
std::shared_ptr<uimg::ImageBuffer> DenoiseWorker::GetResult() {return m_imgBuffer;}

util::ParallelJob<std::shared_ptr<uimg::ImageBuffer>> pragma::modules::cycles::denoise(uimg::ImageBuffer &imgBuffer)
{
	return util::create_parallel_job<DenoiseWorker>(imgBuffer);
}
