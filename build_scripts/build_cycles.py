import os
import sys
from pathlib import Path
from sys import platform
import subprocess

# To update Cycles to a newer version, follow these steps:
# - Find the latest stable release on Cycles on https://github.com/blender/cycles/tags
# - Update the fork https://github.com/Silverlan/cycles to that commit
# - Copy the commit id to "cycles_commit_sha" below
# - Update preprocessor definitions for cycles in CMakeLists.txt of external_libs/cycles/CMakeLists.txt
# - Update the versions of tbb, oidn, ocio, oiio, opensubdiv libraries in setup.py to match cycles versions
# - Go to https://github.com/blender/cycles/tree/main/lib for the commit of the cycles version
#   - Grab the commit ids for linux_x64 and windows_x64 and apply them to cycles_lib_*_x64_commit_sha in setup.py
cycles_commit_sha = "f0d593ed7d0574fe113557612b78dfdaf4ee4203" # Version 4.1.1

########## cycles ##########
os.chdir(deps_dir)
cyclesRoot = deps_dir +"/cycles"
if not Path(cyclesRoot).is_dir():
	print_msg("cycles not found. Downloading...")
	git_clone("https://github.com/Silverlan/cycles.git",branch="rollback/4.1.1")

os.chdir(cyclesRoot)

if platform == "linux":
	cyclesDepsRoot = cyclesRoot +"/lib/linux_x64"
else:
	cyclesDepsRoot = cyclesRoot +"/lib/windows_x64"

lastBuildCommit = None
lastbuildshaFile = cyclesRoot +"/lastbuildsha"
if Path(lastbuildshaFile).is_file():
	lastBuildCommit = Path(lastbuildshaFile).read_text()

targetCommit = cycles_commit_sha
if lastBuildCommit != targetCommit:
	print_msg("Downloading cycles dependencies...")
	subprocess.run(["git","fetch"],check=True)
	if platform == "win32":
		subprocess.run(["git","reset","--hard",targetCommit],check=True)

		# Turn off cycles hydra render delegate
		cyclesCmakePath = cyclesRoot +"/CMakeLists.txt"
		strIdx = open(cyclesCmakePath, 'r').read().find('"Build Cycles Hydra render delegate" OFF')
		if strIdx == -1:
			replace_text_in_file(cyclesCmakePath,'"Build Cycles Hydra render delegate" ON','"Build Cycles Hydra render delegate" OFF')
		#
		
		# For some reason Cycles does not link against OpenColorIO by default, even though it's required,
		# so we have to brute force some more changes.
		cyclesMacrosPath = cyclesRoot +"/src/cmake/macros.cmake"
		strIdx = open(cyclesMacrosPath, 'r').read().find('list(APPEND ${libraries} ${OPENIMAGEIO_LIBRARIES})')
		if strIdx == -1:
			strIdx = open(cyclesMacrosPath, 'r').read().find('if(WITH_OPENCOLORIO)')
			if strIdx != -1:
				replace_text_in_file(cyclesMacrosPath,'if(WITH_OPENCOLORIO)','list(APPEND ${libraries} ${OPENIMAGEIO_LIBRARIES})\n  if(WITH_OPENCOLORIO)')
		#
		
		scriptPath = cyclesRoot +"/src/cmake/make_update.py"
		python_interpreter = sys.executable
		command = [python_interpreter, scriptPath, "--no-cycles"]
		subprocess.run(command)

if platform == "linux":
	# Building the cycles executable causes build errors. We don't need it, but unfortunately cycles doesn't provide us with a
	# way to disable it, so we'll have to make some changes to the CMake configuration file.
	#appCmakePath = cyclesRoot +"/src/app/CMakeLists.txt"
	#strIdx = open(appCmakePath, 'r').read().find('if(WITH_CYCLES_STANDALONE)')
	#if strIdx != -1:
	#	replace_text_in_file(appCmakePath,'if(WITH_CYCLES_STANDALONE)','if(false)')
	print("")
else:
	# We need to add the --allow-unsupported-compiler flag to a cycles CMake configuration file manually,
	# otherwise compilation will fail for newer versions of Visual Studio.
	kernelCmakePath = cyclesRoot +"/src/kernel/CMakeLists.txt"
	strIdx = open(kernelCmakePath, 'r').read().find('--allow-unsupported-compiler')
	if strIdx == -1:
		replace_text_in_file(kernelCmakePath,'${CUDA_NVCC_FLAGS}','${CUDA_NVCC_FLAGS} --allow-unsupported-compiler -D _ALLOW_COMPILER_AND_STL_VERSION_MISMATCH')

print_msg("Download dependencies")
os.chdir(cyclesRoot)
if platform == "linux":
	subprocess.run(["make","update"],check=True)
else:
	subprocess.run(["make.bat","update"],check=True)

print_msg("Build cycles")

mkdir("build",cd=True)
oiio_root_dir = cyclesDepsRoot + "/openimageio"
oidn_root_dir = cyclesDepsRoot + "/openimagedenoise"

# Building cycles rebuilds shaders every time, which can take a very long time.
# Since there's usually no reason to rebuild the shaders, we'll only build if the head commit has
# changed since the last build.
curCommitId = subprocess.check_output(["git","rev-parse","HEAD"]).decode(sys.stdout.encoding)
if lastBuildCommit != curCommitId:
	if platform == "linux":
		zlib = zlib_root +"/build/libz.a"
	else:
		zlib = zlib_lib
	args = ["-DWITH_CYCLES_CUDA_BINARIES=ON","-DWITH_CYCLES_DEVICE_OPTIX=ON","-DWITH_CYCLES_DEVICE_CUDA=ON"]
	
	# OSL is disabled because we don't need it and it causes build errors on the GitHub runner.
	args.append("-DWITH_CYCLES_OSL=OFF")

	# Hydra delegate is disabled because we don't need it and it causes build errors on the (Windows) GitHub runner.
	args.append("-DWITH_CYCLES_HYDRA_RENDER_DELEGATE=OFF")
	args.append("-DWITH_CYCLES_USD=OFF")
	
	args.append("-DOPENIMAGEIO_ROOT_DIR:PATH=" +oiio_root_dir)
	#if platform == "linux":
		# Unfortunately, when building the dependencies ourselves, some of the lookup
		# locations don't match what cycles expects, so we have to tell cycles where to
		# look for those dependencies here.
	#	args.append("-DOPENCOLORIO_ROOT_DIR:PATH=" +cyclesDepsInstallLocation +"/ocio")
	#	args.append("-DOPENSUBDIV_ROOT_DIR:PATH=" +cyclesDepsInstallLocation +"/osd")
	#	args.append("-DOPENIMAGEDENOISE_ROOT_DIR:PATH=" +oidn_root_dir)

		# pugixml is required for the standalone executable of cycles, which we don't care about.
		# Unfortunately cycles doesn't provide an option to build without pugixml, and it's also
		# not included in the dependencies, so we just set the pugixml variables
		# to bogus values here to shut CMake up.
	#	args.append("-DPUGIXML_INCLUDE_DIR:PATH=/usr/include")
	#	args.append("-DPUGIXML_LIBRARY:PATH=/usr/lib")

	#	args.append("-DTIFF_INCLUDE_DIR:PATH=" +cyclesDepsInstallLocation +"/tiff/libtiff")
	#	args.append("-DTIFF_LIBRARY:FILEPATH=" +cyclesDepsInstallLocation +"/tiff/build/libtiff/libtiff.so")

	if platform == "linux":
		cmake_configure("..","Unix Makefiles",args)
		cmake_build(build_config,targets=["install"])
	else:
		cmake_configure("..",generator,args)
		cmake_build(build_config)

	with open(lastbuildshaFile, 'w') as filetowrite:
		filetowrite.write(curCommitId)
else:
	print_msg("Head commit has not changed, skipping build...")

cmake_args.append("-DDEPENDENCY_CYCLES_INCLUDE=" +deps_dir + "/cycles/src")
cmake_args.append("-DDEPENDENCY_CYCLES_ROOT=" +deps_dir + "/cycles")
cmake_args.append("-DDEPENDENCY_CYCLES_BUILD_LOCATION=" +deps_dir + "/cycles/build")
cmake_args.append("-DDEPENDENCY_CYCLES_ATOMIC_INCLUDE=" +deps_dir + "/cycles/third_party/atomic")
cmake_args.append("-DDEPENDENCY_CYCLES_OPENIMAGEIO_INCLUDE=" +oiio_root_dir +"/include")
cmake_args.append("-DDEPENDENCY_CYCLES_PUGIXML_INCLUDE=" +cyclesDepsRoot + "/pugixml/include")
cmake_args.append("-DDEPENDENCY_CYCLES_OPENIMAGEDENOISE_INCLUDE=" +oidn_root_dir +"/include")
cmake_args.append("-DDEPENDENCY_CYCLES_OPENEXR_INCLUDE=" +cyclesDepsRoot + "/openexr/include")
cmake_args.append("-DDEPENDENCY_CYCLES_EMBREE_INCLUDE=" +cyclesDepsRoot + "/embree/include")
cmake_args.append("-DDEPENDENCY_CYCLES_OSL_INCLUDE=" +cyclesDepsRoot + "/osl/include")
cmake_args.append("-DDEPENDENCY_CYCLES_TBB_INCLUDE=" +cyclesDepsRoot + "/tbb/include")
cmake_args.append("-DDEPENDENCY_CYCLES_ZSTD_INCLUDE=" +cyclesDepsRoot + "/zstd/include")
cmake_args.append("-DDEPENDENCY_CYCLES_OPENVDB_LIBRARY_PATH=" +cyclesDepsRoot + "/openvdb/lib")
cmake_args.append("-DDEPENDENCY_CYCLES_DEPENDENCIES_LOCATION=" +cyclesDepsRoot + "")

cmake_args.append("-DDEPENDENCY_OPENEXR_INCLUDE=" +cyclesDepsRoot + "/openexr/include")
cmake_args.append("-DDEPENDENCY_OPENEXR_IMATH_INCLUDE=" +cyclesDepsRoot + "/imath/include")

if platform == "linux":
	cmake_args.append("-DDEPENDENCY_CYCLES_LIBRARY_LOCATION=" +cyclesRoot +"/build/lib")

	cmake_args.append("-DDEPENDENCY_CYCLES_TBB_LIBRARY=" +cyclesDepsRoot + "/tbb/lib/libtbb.so")
	cmake_args.append("-DDEPENDENCY_CYCLES_ZSTD_LIBRARY=" +cyclesDepsRoot + "/zstd/lib/libzstd.a")
	cmake_args.append("-DDEPENDENCY_CYCLES_EMBREE_LIBRARY=" +cyclesDepsRoot + "/embree/lib/libembree4.so")
	cmake_args.append("-DDEPENDENCY_CYCLES_OPENCOLORIO_LIBRARY=" +cyclesDepsRoot + "/opencolorio/lib/libOpenColorIO.so")
	cmake_args.append("-DDEPENDENCY_CYCLES_OPENIMAGEIO_LIBRARY=" +cyclesDepsRoot + "/openimageio/lib/libOpenImageIO.so")
	cmake_args.append("-DDEPENDENCY_CYCLES_OPENIMAGEDENOISE_LIBRARY=" +cyclesDepsRoot + "/openimagedenoise/lib/libopenimagedenoise.so")
	cmake_args.append("-DDEPENDENCY_CYCLES_IMATH_LIBRARY=" +cyclesDepsRoot + "/imath/lib/libImath.so")
	cmake_args.append("-DDEPENDENCY_JPEG_LIBRARY=" +cyclesDepsRoot + "/jpeg/lib/libjpeg.a")
	cmake_args.append("-DDEPENDENCY_TIFF_LIBRARY=" +cyclesDepsRoot + "/tiff/lib/libtiff.a")
	cmake_args.append("-DDEPENDENCY_CYCLES_LPNG_LIBRARY=" +cyclesDepsRoot + "/png/lib/libpng.a")
	cmake_args.append("-DDEPENDENCY_OPENEXR_IMATH_LIBRARY=" +cyclesDepsRoot + "/imath/lib/libImath.so")
	cmake_args.append("-DDEPENDENCY_OPENEXR_UTIL_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/libOpenEXR.so")
	cmake_args.append("-DDEPENDENCY_OPENEXR_ILMTHREAD_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/libIlmThread.so")
	cmake_args.append("-DDEPENDENCY_OPENEXR_IEX_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/libIex.so")
	cmake_args.append("-DDEPENDENCY_OPENEXR_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/libOpenEXR.so")
	cmake_args.append("-DDEPENDENCY_BOOST_THREAD_SHARED_LIBRARY=" +cyclesDepsRoot + "/boost/lib/libboost_thread.so")

	# Prebuilt binary locations
	# cmake_args.append("-DDEPENDENCY_CYCLES_TBB_LIBRARY=" +cyclesDepsRoot + "/tbb/lib/libtbb.a")
	# cmake_args.append("-DDEPENDENCY_JPEG_LIBRARY=" +cyclesDepsRoot + "/jpeg/lib/libjpeg.a")
	# cmake_args.append("-DDEPENDENCY_TIFF_LIBRARY=" +cyclesDepsRoot + "/tiff/lib/libtiff.a")
	# cmake_args.append("-DDEPENDENCY_OPENEXR_IMATH_LIBRARY=" +cyclesDepsRoot + "/imath/lib/libImath.a")
	# cmake_args.append("-DDEPENDENCY_OPENEXR_UTIL_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/libOpenEXR.a")
	# cmake_args.append("-DDEPENDENCY_OPENEXR_ILMTHREAD_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/libIlmThread.a")
	# cmake_args.append("-DDEPENDENCY_OPENEXR_IEX_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/libIex.a")
else:
	cmake_args.append("-DDEPENDENCY_CYCLES_LIBRARY_LOCATION=" +cyclesRoot +"/build/lib/" +build_config +"")
	cmake_args.append("-DDEPENDENCY_CYCLES_TBB_LIBRARY=" +cyclesDepsRoot + "/tbb/lib/tbb.lib")
	cmake_args.append("-DDEPENDENCY_CYCLES_ZSTD_LIBRARY=" +cyclesDepsRoot + "/zstd/lib/zstd_static.lib")
	cmake_args.append("-DDEPENDENCY_CYCLES_EMBREE_LIBRARY=" +cyclesDepsRoot + "/embree/lib/embree4.lib")
	cmake_args.append("-DDEPENDENCY_CYCLES_OPENCOLORIO_LIBRARY=" +cyclesDepsRoot + "/opencolorio/lib/OpenColorIO.lib")
	cmake_args.append("-DDEPENDENCY_CYCLES_OPENIMAGEIO_LIBRARY=" +cyclesDepsRoot + "/OpenImageIO/lib/OpenImageIO.lib")
	cmake_args.append("-DDEPENDENCY_CYCLES_OPENIMAGEDENOISE_LIBRARY=" +cyclesDepsRoot + "/openimagedenoise/lib/openimagedenoise.lib")
	cmake_args.append("-DDEPENDENCY_CYCLES_IMATH_LIBRARY=" +cyclesDepsRoot + "/imath/lib/imath.lib")
	cmake_args.append("-DDEPENDENCY_OPENEXR_UTIL_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/OpenEXRUtil_s.lib")
	cmake_args.append("-DDEPENDENCY_OPENEXR_IMATH_LIBRARY=" +cyclesDepsRoot + "/imath/lib/Imath.lib")
	cmake_args.append("-DDEPENDENCY_OPENEXR_ILMTHREAD_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/IlmThread_s.lib")
	cmake_args.append("-DDEPENDENCY_OPENEXR_IEX_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/Iex_s.lib")
	cmake_args.append("-DDEPENDENCY_BOOST_THREAD_SHARED_LIBRARY=" +cyclesDepsRoot + "/boost/lib/boost_thread-vc142-mt-x64-1_82.lib")

	cmake_args.append("-DDEPENDENCY_JPEG_LIBRARY=" +cyclesDepsRoot + "/jpeg/lib/libjpeg.lib")
	cmake_args.append("-DDEPENDENCY_TIFF_LIBRARY=" +cyclesDepsRoot + "/tiff/lib/libtiff.lib")
	cmake_args.append("-DDEPENDENCY_CYCLES_LPNG_LIBRARY=" +cyclesDepsRoot + "/png/lib/libpng.lib")

additional_build_targets.append("UniRender_cycles")

########## checks ##########

cyclesCmakeCacheFile = cyclesRoot +"/build/CMakeCache.txt"

strIdx = open(cyclesCmakeCacheFile, 'r').read().find('WITH_CYCLES_DEVICE_CUDA:BOOL=OFF')
if strIdx != -1:
	print_warning("CUDA is disabled for Cycles! Is CUDA installed on the system?")

strIdx = open(cyclesCmakeCacheFile, 'r').read().find('WITH_CYCLES_DEVICE_OPTIX:BOOL=OFF')
if strIdx != -1:
	print_warning("OptiX is disabled for Cycles! Is OptiX installed on the system?")
