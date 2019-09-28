#include "pr_cycles/scene.hpp"
#include <sharedutils/util_image_buffer.hpp>
#include <OpenImageDenoise/oidn.hpp>
#include <util/util_half.h>
#include <iostream>

using namespace pragma::modules;

#pragma optimize("",off)
bool cycles::Scene::Denoise(const DenoiseInfo &denoise,float *inOutData,const std::function<bool(float)> &fProgressCallback)
{
	auto device = oidn::newDevice();

	const char *errMsg;
	if(device.getError(errMsg) != oidn::Error::None)
		return false;
	device.setErrorFunction([](void *userPtr,oidn::Error code,const char *message) {
		std::cout<<"Error: "<<message<<std::endl;
	});
	device.set("verbose",true);
	device.commit();

	oidn::FilterRef filter = device.newFilter("RT");

	//std::vector<float> albedo {};
	//albedo.resize(denoise.width *denoise.height *3,1.f);

	//std::vector<float> normal {};
	//normal.resize(denoise.width *denoise.height *3,0.f);
	//for(auto i=0;i<denoise.width *denoise.height;i+=3)
	//	normal.at(i +1) = 1.f;

	filter.setImage("color",inOutData,oidn::Format::Float3,denoise.width,denoise.height);
	//filter.setImage("albedo",albedo.data(),oidn::Format::Float3,denoise.width,denoise.height); // TODO
	//filter.setImage("normal",normal.data(),oidn::Format::Float3,denoise.width,denoise.height);
	filter.setImage("output",inOutData,oidn::Format::Float3,denoise.width,denoise.height);

	filter.set("hdr",denoise.hdr);

	std::unique_ptr<std::function<bool(float)>> ptrProgressCallback = nullptr;
	if(fProgressCallback)
		ptrProgressCallback = std::make_unique<std::function<bool(float)>>(fProgressCallback);
	filter.setProgressMonitorFunction([](void *userPtr,double n) -> bool {
		auto *ptrProgressCallback = static_cast<std::function<bool(float)>*>(userPtr);
		if(ptrProgressCallback)
			return (*ptrProgressCallback)(n);
		return true;
	},ptrProgressCallback.get());

	filter.commit();

	filter.execute();
	return true;
}

bool cycles::Scene::Denoise(const DenoiseInfo &denoiseInfo,util::ImageBuffer &imgBuffer,const std::function<bool(float)> &fProgressCallback) const
{
	if(imgBuffer.GetFormat() == util::ImageBuffer::Format::RGB_FLOAT)
		return Denoise(denoiseInfo,imgBuffer,fProgressCallback); // Image is already in the right format, we can just denoise and be done with it

	// Image is in the wrong format, we'll need a temporary copy
	auto pImgDenoise = imgBuffer.Copy(util::ImageBuffer::Format::RGB_FLOAT);
	if(Denoise(denoiseInfo,static_cast<float*>(pImgDenoise->GetData()),fProgressCallback) == false)
		return false;

	// Copy denoised data back to result buffer
	auto itSrc = pImgDenoise->begin();
	auto itDst = imgBuffer.begin();
	auto numChannels = umath::to_integral(util::ImageBuffer::Channel::Count) -1; // -1, because we don't want to overwrite the old alpha channel values
	for(;itSrc != pImgDenoise->end();++itSrc,++itDst)
	{
		auto &pxSrc = *itSrc;
		auto &pxDst = *itDst;
		for(auto i=decltype(numChannels){0u};i<numChannels;++i)
			pxDst.CopyValue(static_cast<util::ImageBuffer::Channel>(i),pxSrc);
	}
	return true;
}
#pragma optimize("",on)
