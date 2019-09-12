#include "pr_cycles/scene.hpp"
#include <OpenImageDenoise/oidn.hpp>
#include <util/util_half.h>
#include <iostream>

using namespace pragma::modules;

#pragma optimize("",off)
template<typename TPixelType,bool THDR=false>
	static bool denoise(const cycles::Scene::DenoiseInfo &denoise,const void *imgData,std::vector<uint8_t> &outData)
{
	auto numPixels = denoise.width *denoise.height;
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

	std::vector<std::array<float,3>> inputData {};
	inputData.resize(numPixels);
	for(auto i=decltype(numPixels){0u};i<numPixels;++i)
	{
		auto &pxData = *(static_cast<const std::array<TPixelType,4>*>(imgData) +i);
		auto &dstData = inputData.at(i);
		for(auto j=decltype(pxData.size()){0u};j<3;++j)
		{
			if constexpr (THDR)
				dstData.at(j) = ccl::half_to_float(pxData.at(j));
			else
				dstData.at(j) = pxData.at(j) /static_cast<float>(std::numeric_limits<TPixelType>::max());
		}
	}
	filter.setImage("color",inputData.data(),oidn::Format::Float3,denoise.width,denoise.height);
	// TODO: Albedo +normal?

	std::vector<std::array<float,3>> outputData {};
	outputData.resize(numPixels);
	filter.setImage("output",outputData.data(),oidn::Format::Float3,denoise.width,denoise.height);

	filter.set("hdr",denoise.hdr);

	filter.setProgressMonitorFunction([](void *userPtr,double n) -> bool {
		std::cout<<"Monitor: "<<n<<std::endl;
		return true;
	});

	filter.commit();

	filter.execute();

	outData.resize(numPixels *sizeof(TPixelType) *4);
	for(auto i=decltype(numPixels){0u};i<numPixels;++i)
	{
		auto &srcData = outputData.at(i);
		auto dstOffset = i *4;
		for(uint8_t j=0;j<4;++j)
		{
			if constexpr (THDR)
				*(reinterpret_cast<TPixelType*>(outData.data()) +dstOffset +j) = (j < 3) ? ccl::float_to_half(srcData.at(j)) : ccl::float_to_half(std::numeric_limits<float>::max());
			else
				*(reinterpret_cast<TPixelType*>(outData.data()) +dstOffset +j) = (j < 3) ? (std::min<TPixelType>(srcData.at(j) *std::numeric_limits<TPixelType>::max(),std::numeric_limits<TPixelType>::max())) : std::numeric_limits<TPixelType>::max();
		}
	}
	return true;
}

bool cycles::Scene::Denoise(const DenoiseInfo &denoiseInfo,const void *imgData,std::vector<uint8_t> &outData)
{
	if(denoiseInfo.hdr)
		return denoise<uint16_t,true>(denoiseInfo,imgData,outData);
	return denoise<uint8_t,false>(denoiseInfo,imgData,outData);
}
#pragma optimize("",on)
