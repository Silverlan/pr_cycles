import os
import sys
from pathlib import Path
from sys import platform
import subprocess

# TMP
#deps_dir = "E:/projects/pragma/deps"
#zlib_root = "E:/projects/pragma/deps/zlib"
#zlib_lib = "E:/projects/pragma/deps/zlib/build/RelWithDebInfo/zlibstatic.lib"
#generator = "Visual Studio 17 2022"
#build_config = "RelWithDebInfo"
#import multiprocessing
#def print_msg(msg):
#	print(msg)
#def git_clone(url,directory=None,branch=None):
#	args = ["git", "clone", url, "--recurse-submodules"]
#	if branch:
#		args.extend(["-b", branch])
#	if directory:
#		args.append(directory)
#	subprocess.run(args, check=True)
#def replace_text_in_file(filepath,srcStr,dstStr):
#	filedata = None
#	with open(filepath, 'r') as file :
#		filedata = file.read()
#
#	if filedata:
#		# Replace the target string
#		filedata = filedata.replace(srcStr, dstStr)
#
#		# Write the file out again
#		with open(filepath, 'w') as file:
#			file.write(filedata)
#def mkdir(dirName,cd=False):
#	if not Path(dirName).is_dir():
#		os.makedirs(dirName)
#	if cd:
#		os.chdir(dirName)
#def cmake_configure(scriptPath,generator,additionalArgs=[]):
#	args = ["cmake",scriptPath,"-G",generator]
#	args += additionalArgs
#	print("Running CMake configure command:", ' '.join(f'"{arg}"' for arg in args))
#	subprocess.run(args,check=True)
#def cmake_build(buildConfig,targets=None):
#	args = ["cmake","--build",".","--config",buildConfig]
#	if targets:
#		args.append("--target")
#		args += targets
#	args.append("--parallel")
#	args.append(str(multiprocessing.cpu_count()))
#	print("Running CMake build command:", ' '.join(f'"{arg}"' for arg in args))
#	subprocess.run(args,check=True)














# To update Cycles to a newer version, follow these steps:
# - Find the latest stable release on Cycles on https://github.com/blender/cycles/tags
# - Update the fork https://github.com/Silverlan/cycles to that commit
# - Copy the commit id to "cycles_commit_sha" below
# - Update preprocessor definitions for cycles in CMakeLists.txt of external_libs/cycles/CMakeLists.txt
# - Update the versions of tbb, oidn, ocio, oiio, opensubdiv libraries in setup.py to match cycles versions
cycles_commit_sha = "ee4a5f249e43e02aef439877e747f14fa5c8c3c9" # Version 4.1.1

########## cycles ##########
os.chdir(deps_dir)
cyclesRoot = deps_dir +"/cycles"
if not Path(cyclesRoot).is_dir():
	print_msg("cycles not found. Downloading...")
	git_clone("https://github.com/Silverlan/cycles.git")

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
	else:
		# Reset our changes from previous versions
		subprocess.run(["git","reset","--hard"],check=True)
		
		# On Linux we want to use our own build dependencies, so we have to run
		# the make update script manually with the --no-libraries argument to disable the default
		# behavior of downloading the prebuilt binaries.
		scriptPath = cyclesRoot +"/src/cmake/make_update.py"
		python_interpreter = sys.executable
		command = [python_interpreter, scriptPath, "--no-libraries"]
		subprocess.run(command)

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

print_msg("Download dependencies")
subprocess.run(["make","update"],check=True,shell=True)

print_msg("Build cycles")
mkdir("build",cd=True)

if platform == "linux":
	oiio_root_dir = cyclesDepsRoot +"/oiio"
	oidn_root_dir = cyclesDepsRoot +"/oidn"
else:
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
	args = ["-DWITH_CYCLES_CUDA_BINARIES=ON","-DWITH_CYCLES_DEVICE_OPTIX=ON","-DWITH_CYCLES_DEVICE_CUDA=ON","-DZLIB_INCLUDE_DIR=" +zlib_root,"-DZLIB_LIBRARY=" +zlib]
	
	# OSL is disabled because we don't need it and it causes build errors on the GitHub runner.
	args.append("-DWITH_CYCLES_OSL=OFF")

	# Hydra delegate is disabled because we don't need it and it causes build errors on the (Windows) GitHub runner.
	args.append("-DWITH_CYCLES_HYDRA_RENDER_DELEGATE=OFF")
	args.append("-DWITH_CYCLES_USD=OFF")
	
	args.append("-DOPENIMAGEIO_ROOT_DIR:PATH=" +oiio_root_dir)
	if platform == "linux":
		# Unfortunately, when building the dependencies ourselves, some of the lookup
		# locations don't match what cycles expects, so we have to tell cycles where to
		# look for those dependencies here.
		args.append("-DOPENCOLORIO_ROOT_DIR:PATH=" +cyclesDepsInstallLocation +"/ocio")
		args.append("-DOPENSUBDIV_ROOT_DIR:PATH=" +cyclesDepsInstallLocation +"/osd")
		args.append("-DOPENIMAGEDENOISE_ROOT_DIR:PATH=" +oidn_root_dir)

		# pugixml is required for the standalone executable of cycles, which we don't care about.
		# Unfortunately cycles doesn't provide an option to build without pugixml, and it's also
		# not included in the dependencies, so we just set the pugixml variables
		# to bogus values here to shut CMake up.
		args.append("-DPUGIXML_INCLUDE_DIR:PATH=/usr/include")
		args.append("-DPUGIXML_LIBRARY:PATH=/usr/lib")

		args.append("-DTIFF_INCLUDE_DIR:PATH=" +cyclesDepsInstallLocation +"/tiff/libtiff")
		args.append("-DTIFF_LIBRARY:FILEPATH=" +cyclesDepsInstallLocation +"/tiff/build/libtiff/libtiff.so")

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
cmake_args.append("-DDEPENDENCY_CYCLES_OPENVDB_LIBRARY_PATH=" +cyclesDepsRoot + "/openvdb/lib")
cmake_args.append("-DDEPENDENCY_CYCLES_DEPENDENCIES_LOCATION=" +cyclesDepsRoot + "")

cmake_args.append("-DDEPENDENCY_OPENEXR_INCLUDE=" +cyclesDepsRoot + "/openexr/include")
cmake_args.append("-DDEPENDENCY_OPENEXR_IMATH_INCLUDE=" +cyclesDepsRoot + "/imath/include")

cmake_args.append("-DDEPENDENCY_OPENIMAGEDENOISE_INCLUDE=" +cyclesDepsRoot + "/openimagedenoise/include")

if platform == "linux":
	cmake_args.append("-DDEPENDENCY_CYCLES_LIBRARY_LOCATION=" +cyclesRoot +"/build/lib")

	cmake_args.append("-DDEPENDENCY_CYCLES_TBB_LIBRARY=" +cyclesDepsRoot + "/tbb/lib/libtbb.so")
	cmake_args.append("-DDEPENDENCY_CYCLES_EMBREE_LIBRARY=" +cyclesDepsRoot + "/embree/lib/libembree4.so")
	cmake_args.append("-DDEPENDENCY_CYCLES_OPENCOLORIO_LIBRARY=" +cyclesDepsRoot + "/opencolorio/lib/libOpenColorIO.so")
	cmake_args.append("-DDEPENDENCY_CYCLES_OPENIMAGEIO_LIBRARY=" +cyclesDepsRoot + "/OpenImageIO/lib/libOpenImageIO.so")
	cmake_args.append("-DDEPENDENCY_CYCLES_OPENIMAGEDENOISE_LIBRARY=" +cyclesDepsRoot + "/openimagedenoise/lib/libopenimagedenoise.so")
	cmake_args.append("-DDEPENDENCY_CYCLES_IMATH_LIBRARY=" +cyclesDepsRoot + "/imath/lib/libimath.so")
	cmake_args.append("-DDEPENDENCY_JPEG_LIBRARY=" +cyclesDepsRoot + "/jpeg/build/libjpeg.a")
	cmake_args.append("-DDEPENDENCY_TIFF_LIBRARY=" +cyclesDepsRoot + "/tiff/build/libtiff/libtiff.so")
	cmake_args.append("-DDEPENDENCY_CYCLES_LPNG_LIBRARY=" +cyclesDepsRoot + "/png/lib/libpng.a")
	cmake_args.append("-DDEPENDENCY_OPENEXR_IMATH_LIBRARY=" +cyclesDepsRoot + "/Imath/build/src/Imath/libImath-3_1.so")
	cmake_args.append("-DDEPENDENCY_OPENEXR_UTIL_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/libOpenEXR.so")
	cmake_args.append("-DDEPENDENCY_OPENEXR_ILMTHREAD_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/libIlmThread.so")
	cmake_args.append("-DDEPENDENCY_OPENEXR_IEX_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/libIex.so")
	cmake_args.append("-DDEPENDENCY_OPENIMAGEDENOISE_LIBRARY=" +cyclesDepsRoot + "/openimagedenoise/lib/libopenimagedenoise.so")
	cmake_args.append("-DDEPENDENCY_OPENIMAGEDENOISE_CORE_LIBRARY=" +cyclesDepsRoot + "/openimagedenoise/lib/libOpenImageDenoise_core.so")

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
	cmake_args.append("-DDEPENDENCY_CYCLES_EMBREE_LIBRARY=" +cyclesDepsRoot + "/embree/lib/embree4.lib")
	cmake_args.append("-DDEPENDENCY_CYCLES_OPENCOLORIO_LIBRARY=" +cyclesDepsRoot + "/opencolorio/lib/OpenColorIO.lib")
	cmake_args.append("-DDEPENDENCY_CYCLES_OPENIMAGEIO_LIBRARY=" +cyclesDepsRoot + "/OpenImageIO/lib/OpenImageIO.lib")
	cmake_args.append("-DDEPENDENCY_CYCLES_OPENIMAGEDENOISE_LIBRARY=" +cyclesDepsRoot + "/openimagedenoise/lib/openimagedenoise.lib")
	cmake_args.append("-DDEPENDENCY_CYCLES_IMATH_LIBRARY=" +cyclesDepsRoot + "/imath/lib/imath.lib")
	cmake_args.append("-DDEPENDENCY_OPENEXR_UTIL_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/OpenEXRUtil_s.lib")
	cmake_args.append("-DDEPENDENCY_OPENEXR_IMATH_LIBRARY=" +cyclesDepsRoot + "/imath/lib/Imath.lib")
	cmake_args.append("-DDEPENDENCY_OPENEXR_ILMTHREAD_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/IlmThread_s.lib")
	cmake_args.append("-DDEPENDENCY_OPENEXR_IEX_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/Iex_s.lib")
	cmake_args.append("-DDEPENDENCY_OPENIMAGEDENOISE_LIBRARY=" +cyclesDepsRoot + "/openimagedenoise/lib/openimagedenoise.lib")
	cmake_args.append("-DDEPENDENCY_OPENIMAGEDENOISE_CORE_LIBRARY=" +cyclesDepsRoot + "/openimagedenoise/lib/openimagedenoise_core.lib")

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
