import os
import sys
from pathlib import Path
from sys import platform
import subprocess

# To update Cycles to a newer version, follow these steps:
# - Find the latest stable release on Cycles on https://github.com/blender/cycles/tags
# - Copy the commit id to "cycles_commit_sha" below
# - Browse the files for the commit of that version and go to "src/util/version.h".
# The value for "CYCLES_BLENDER_LIBRARIES_VERSION" is the version of Blender that
# corresponds to this version of Cycles.
# - Go to the Blender repository on GitHub: https://github.com/blender/blender/tags and
# find the version that corresponds to CYCLES_BLENDER_LIBRARIES_VERSION
# - Browse the files for the commit of that version and go to "build_files/build_environment/"
# - Copy the file "install_deps.sh" to "build_scripts/cycles" of this repository and overwrite the existing file.
# - Open the file in a text-editor and update the following entries:
# - Change OCIO_VERSION to "2.2.0", unless the version is already newer
# - Change USD_VERSION to "22.11", unless the version is already newer
# - Change USD_VERSION_SHORT to "22.11", unless the version is already newer
# - Find the value for OPENEXR_VERSION, note it down and save and close the file
# - Go to the Imath repository on GitHub: https://github.com/AcademySoftwareFoundation/Imath/tags and
# find the version that corresponds to OPENEXR_VERSION
# - Copy the commit id to "imath_commit_sha" below
cycles_commit_sha = "7cfe5570e835bd14c5ddc453e89fc6844e2b85ea" # Version 3.5.0
imath_commit_sha = "90c47b4" # Version 3.1.5, has to match the OpenEXR version of install_deps.sh

########## cycles dependencies ##########
if platform == "linux":
	# The prebuild binaries of the cycles build script are built using a pre-C++11 ABI, which makes them incompatible with Pragma.
	# For this reason we have to build them ourselves manually.
	sourceLocation = deps_dir +"/lib/source"
	installLocation = deps_dir +"/lib/linux_x86_64" # Cycles will be looking for "deps/lib/${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR}"
	cyclesDepsInstallLocation = installLocation
	args = [os.path.dirname(os.path.realpath(__file__)) +"/cycles/install_deps.sh",
		"--build-boost","--build-oiio","--build-tbb","--build-alembic",
		"--build-embree","--build-ocio","--build-openvdb","--build-osl",
		"--build-osd","--build-oidn","--build-usd","--with-embree","--with-oidn",
		"--build-nanovdb","--with-nanovdb",
		"--source",sourceLocation,
		"--install",installLocation]
	if no_confirm:
		args.append("--no-confirm")
	if no_sudo:
		args.append("--no-sudo")

	subprocess.run(args,check=True)

	cyclesDepsRoot = cyclesDepsInstallLocation
else:
	cyclesDepsRoot = deps_dir +"/lib/win64_vc15"

########## Imath ##########
if platform == "linux":
	# OpenEXR used to come with Imath, but that changed with version 3, now Imath is a separate repository.
	# Unfortunately cycles still expects Imath to reside with OpenEXR, so we have to download it manually
	# and copy it over.
	os.chdir(cyclesDepsInstallLocation)
	imath_root = cyclesDepsInstallLocation +"/Imath"
	if not Path(imath_root).is_dir():
		print_msg("Imath not found. Downloading...")
		git_clone("https://github.com/AcademySoftwareFoundation/Imath.git","Imath")

	os.chdir(imath_root)
	subprocess.run(["git","reset","--hard",imath_commit_sha],check=True)

	print_msg("Build Imath")
	mkdir("build",cd=True)

	cmake_configure("..",generator)
	cmake_build(build_config)

	cp_dir(imath_root +"/src/Imath",installLocation +"/openexr/include/Imath")
	cp(imath_root +"/build/config/ImathConfig.h",installLocation +"/openexr/include/Imath/")

########## libjpeg-turbo ##########
if platform == "linux":
	os.chdir(cyclesDepsInstallLocation)
	libjpeg_root = cyclesDepsInstallLocation +"/jpeg"
	if not Path(libjpeg_root).is_dir():
		print_msg("libjpeg-turbo not found. Downloading...")
		git_clone("https://github.com/libjpeg-turbo/libjpeg-turbo.git","jpeg")

	os.chdir(libjpeg_root)
	subprocess.run(["git","reset","--hard","8162edd"],check=True)

	print_msg("Build libjpeg-turbo")
	mkdir("build",cd=True)

	cmake_configure("..",generator)
	cmake_build(build_config)

########## libepoxy ##########
if platform == "linux":
	os.chdir(cyclesDepsInstallLocation)
	libepoxy_root = cyclesDepsInstallLocation +"/epoxy"
	if not Path(libepoxy_root).is_dir():
		print_msg("libepoxy not found. Downloading...")
		git_clone("https://github.com/anholt/libepoxy.git","epoxy")

	os.chdir(libepoxy_root)
	print_msg("Build libepoxy")
	subprocess.run(["mkdir -p _build && cd _build && meson && ninja && sudo ninja install"],check=True,shell=True)

########## libtiff ##########
if platform == "linux":
	os.chdir(cyclesDepsInstallLocation)
	libtiff_root = cyclesDepsInstallLocation +"/tiff"
	if not Path(libtiff_root).is_dir():
		print_msg("libjpeg-turbo not found. Downloading...")
		http_extract("https://download.osgeo.org/libtiff/tiff-4.4.0.tar.gz",format="tar.gz")
		os.rename("tiff-4.4.0","tiff")

	os.chdir(libtiff_root)

	print_msg("Build libtiff")
	mkdir("build",cd=True)

	cmake_configure("..",generator)
	cmake_build(build_config)

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

targetCommit = cycles_commit_sha
if lastBuildCommit != targetCommit:
	print_msg("Downloading cycles dependencies...")
	if platform == "win32":
		subprocess.run([cyclesRoot +"/make.bat","update"],check=True,shell=True)
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
	if platform == "linux":
		# Unfortunately, when building the dependencies ourselves, some of the lookup
		# locations don't match what cycles expects, so we have to tell cycles where to
		# look for those dependencies here.
		args.append("-DOPENIMAGEIO_ROOT_DIR:PATH=" +oiio_root_dir)
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

if platform == "linux":
	cmake_args.append("-DDEPENDENCY_CYCLES_LIBRARY_LOCATION=" +cyclesRoot +"/build/lib")

	cmake_args.append("-DDEPENDENCY_CYCLES_TBB_LIBRARY=" +cyclesDepsRoot + "/tbb/lib/libtbb.so")
	cmake_args.append("-DDEPENDENCY_JPEG_LIBRARY=" +cyclesDepsRoot + "/jpeg/build/libjpeg.a")
	cmake_args.append("-DDEPENDENCY_TIFF_LIBRARY=" +cyclesDepsRoot + "/tiff/build/libtiff/libtiff.so")
	cmake_args.append("-DDEPENDENCY_OPENEXR_IMATH_LIBRARY=" +cyclesDepsRoot + "/Imath/build/src/Imath/libImath-3_1.so")
	cmake_args.append("-DDEPENDENCY_OPENEXR_UTIL_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/libOpenEXR.so")
	cmake_args.append("-DDEPENDENCY_OPENEXR_ILMTHREAD_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/libIlmThread.so")
	cmake_args.append("-DDEPENDENCY_OPENEXR_IEX_LIBRARY=" +cyclesDepsRoot + "/openexr/lib/libIex.so")

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
