import os
import sys
from pathlib import Path
from sys import platform
import subprocess

########## cycles ##########
os.chdir(deps_dir)
cyclesRoot = deps_dir +"/cycles"
if not Path(cyclesRoot).is_dir():
	print_msg("cycles not found. Downloading...")
	git_clone("git://git.blender.org/cycles.git")

os.chdir(cyclesRoot)

lastBuildCommit = None
lastbuildshaFile = cyclesRoot +"/lastbuildsha"
if Path(lastbuildshaFile).is_file():
	lastBuildCommit = Path(lastbuildshaFile).read_text()

targetCommit = "b1882be6b1f2e27725ee672d87c5b6f8d6113eb1"
if lastBuildCommit != targetCommit:
    print_msg("Downloading cycles dependencies...")
    if platform == "win32":
    	subprocess.run([cyclesRoot +"/make.bat","update"],check=True,shell=True)
    else:
    	subprocess.run(["make","update"],check=True)

    # The update commit above will unfortunately update Cycles to the last commit. This behavior
    # can't be disabled, so we have to reset the commit back to the one we want here.
    # This can cause issues if the Cycles update-script updates the dependencies to newer versions
    # that aren't compatible with the commit we're using, but it can't be helped.
    subprocess.run(["git","reset","--hard",targetCommit],check=True)

if platform == "linux":
	# Building the cycles executable causes build errors. We don't need it, but unfortunately cycles doesn't provide us with a
	# way to disable it, so we'll have to make some changes to the CMake configuration file.
	appCmakePath = cyclesRoot +"/src/app/CMakeLists.txt"
	strIdx = open(appCmakePath, 'r').read().find('if(WITH_CYCLES_STANDALONE)')
	if strIdx != -1:
		replace_text_in_file(appCmakePath,'if(WITH_CYCLES_STANDALONE)','if(false)')
else:
	# We need to add the --allow-unsupported-compiler flag to a cycles CMake configuration file manually,
	# otherwise compilation will fail for newer versions of Visual Studio.
	kernelCmakePath = cyclesRoot +"/src/kernel/CMakeLists.txt"
	strIdx = open(kernelCmakePath, 'r').read().find('--allow-unsupported-compiler')
	if strIdx == -1:
		replace_text_in_file(kernelCmakePath,'${CUDA_NVCC_FLAGS}','${CUDA_NVCC_FLAGS} --allow-unsupported-compiler')

print_msg("Build cycles")
mkdir("build",cd=True)

# Building cycles rebuilds shaders every time, which can take a very long time.
# Since there's usually no reason to rebuild the shaders, we'll only build if the head commit has
# changed since the last build.
curCommitId = subprocess.check_output(["git","rev-parse","HEAD"]).decode(sys.stdout.encoding)
if lastBuildCommit != curCommitId:
	if platform == "linux":
		zlib = zlib_root +"/build/libz.a"
	else:
		zlib = zlib_lib
	cmake_configure("..",generator,["-DWITH_CYCLES_CUDA_BINARIES=ON","-DWITH_CYCLES_DEVICE_OPTIX=ON","-DWITH_CYCLES_DEVICE_CUDA=ON","-DZLIB_INCLUDE_DIR=" +zlib_root,"-DZLIB_LIBRARY=" +zlib])
	cmake_build(build_config)

	with open(lastbuildshaFile, 'w') as filetowrite:
	    filetowrite.write(curCommitId)
else:
	print_msg("Head commit has not changed, skipping build...")

if platform == "linux":
	cyclesDepsRoot = deps_dir +"/lib/linux_centos7_x86_64"
else:
	cyclesDepsRoot = deps_dir +"/lib/win64_vc15"

cmake_args.append("-DDEPENDENCY_CYCLES_INCLUDE=" +deps_dir + "/cycles/src")
cmake_args.append("-DDEPENDENCY_CYCLES_ROOT=" +deps_dir + "/cycles")
cmake_args.append("-DDEPENDENCY_CYCLES_BUILD_LOCATION=" +deps_dir + "/cycles/build")
cmake_args.append("-DDEPENDENCY_CYCLES_ATOMIC_INCLUDE=" +deps_dir + "/cycles/third_party/atomic")
cmake_args.append("-DDEPENDENCY_CYCLES_OPENIMAGEIO_INCLUDE=" +cyclesDepsRoot + "/openimageio/include")
cmake_args.append("-DDEPENDENCY_CYCLES_PUGIXML_INCLUDE=" +cyclesDepsRoot + "/pugixml/include")
cmake_args.append("-DDEPENDENCY_CYCLES_OPENIMAGEDENOISE_INCLUDE=" +cyclesDepsRoot + "/openimagedenoise/include")
cmake_args.append("-DDEPENDENCY_CYCLES_OPENEXR_INCLUDE=" +cyclesDepsRoot + "/openexr/include")
cmake_args.append("-DDEPENDENCY_CYCLES_EMBREE_INCLUDE=" +cyclesDepsRoot + "/embree/include")
cmake_args.append("-DDEPENDENCY_CYCLES_OSL_INCLUDE=" +cyclesDepsRoot + "/osl/include")
cmake_args.append("-DDEPENDENCY_CYCLES_TBB_INCLUDE=" +cyclesDepsRoot + "/tbb/include")
cmake_args.append("-DDEPENDENCY_CYCLES_OPENVDB_LIBRARY_PATH=" +cyclesDepsRoot + "/openvdb/lib")
cmake_args.append("-DDEPENDENCY_CYCLES_DEPENDENCIES_LOCATION=" +cyclesDepsRoot + "")

cmake_args.append("-DDEPENDENCY_OPENEXR_INCLUDE=" +cyclesDepsRoot + "/openexr/include")
cmake_args.append("-DDEPENDENCY_OPENEXR_IMATH_INCLUDE=" +cyclesDepsRoot + "/imath/include")

if platform == "linux":
	cmake_args.append("-DDEPENDENCY_CYCLES_LIBRARY_LOCATION=" +cyclesRoot +"/build/lib")
	cmake_args.append("-DDEPENDENCY_CYCLES_TBB_LIBRARY=" +cyclesDepsRoot + "/tbb/lib/libtbb.a")
	cmake_args.append("-DDEPENDENCY_OPENEXR_UTIL_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/libOpenEXR.a")
	cmake_args.append("-DDEPENDENCY_OPENEXR_IMATH_LIBRARY=" +cyclesDepsRoot + "/imath/lib/libImath.a")
	cmake_args.append("-DDEPENDENCY_OPENEXR_ILMTHREAD_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/libIlmThread.a")
	cmake_args.append("-DDEPENDENCY_OPENEXR_IEX_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/libIex.a")

	cmake_args.append("-DDEPENDENCY_JPEG_LIBRARY=" +cyclesDepsRoot + "/jpeg/lib/libjpeg.a")
	cmake_args.append("-DDEPENDENCY_TIFF_LIBRARY=" +cyclesDepsRoot + "/tiff/lib/libtiff.a")
else:
	cmake_args.append("-DDEPENDENCY_CYCLES_LIBRARY_LOCATION=" +cyclesRoot +"/build/lib/" +build_config +"")
	cmake_args.append("-DDEPENDENCY_CYCLES_TBB_LIBRARY=" +cyclesDepsRoot + "/tbb/lib/tbb.lib")
	cmake_args.append("-DDEPENDENCY_OPENEXR_UTIL_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/OpenEXRUtil_s.lib")
	cmake_args.append("-DDEPENDENCY_OPENEXR_IMATH_LIBRARY=" +cyclesDepsRoot + "/imath/lib/Imath_s.lib")
	cmake_args.append("-DDEPENDENCY_OPENEXR_ILMTHREAD_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/IlmThread_s.lib")
	cmake_args.append("-DDEPENDENCY_OPENEXR_IEX_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/Iex_s.lib")

	cmake_args.append("-DDEPENDENCY_JPEG_LIBRARY=" +cyclesDepsRoot + "/jpeg/lib/libjpeg.lib")
	cmake_args.append("-DDEPENDENCY_TIFF_LIBRARY=" +cyclesDepsRoot + "/tiff/lib/libtiff.lib")

additional_build_targets.append("UniRender_cycles")

########## checks ##########

cyclesCmakeCacheFile = cyclesRoot +"/build/CMakeCache.txt"

strIdx = open(cyclesCmakeCacheFile, 'r').read().find('WITH_CYCLES_DEVICE_CUDA:BOOL=OFF')
if strIdx != -1:
	print_warning("CUDA is disabled for Cycles! Is CUDA installed on the system?")

strIdx = open(cyclesCmakeCacheFile, 'r').read().find('WITH_CYCLES_DEVICE_OPTIX:BOOL=OFF')
if strIdx != -1:
	print_warning("OptiX is disabled for Cycles! Is OptiX installed on the system?")
