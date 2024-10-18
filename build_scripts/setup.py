import os
import tarfile
import subprocess
import argparse
from sys import platform
from pathlib import Path

os.chdir(deps_dir)

# These need to match the cycles version that is being used (see build_cycles.py for more information)
cycles_lib_windows_x64_commit_sha = "30392df"
cycles_lib_linux_x64_commit_sha = "4d2e4b4"
use_prebuilt_binaries = True

if use_prebuilt_binaries:
	# Prebuilt Binaries
	os.chdir(deps_dir)
	cycles_deps_dir = deps_dir +"/cycles_dependencies"

	if platform == "linux":
		clone_url = "https://projects.blender.org/blender/lib-linux_x64.git"
		clone_dir_name = "lib-linux_x64"
		clone_commit_sha = cycles_lib_linux_x64_commit_sha
	else:
		clone_url = "https://projects.blender.org/blender/lib-windows_x64.git"
		clone_dir_name = "lib-windows_x64"
		clone_commit_sha = cycles_lib_windows_x64_commit_sha

	if not Path(cycles_deps_dir).is_dir():
		print_msg("cycles_dependencies not found. Downloading...")
		mkdir("cycles_dependencies", cd=True)

		subprocess.run(["git", "clone", "--no-checkout", clone_url], check=True)
		os.chdir(clone_dir_name)
		subprocess.run(["git", "sparse-checkout", "set", "openimagedenoise/", "tbb/", "opencolorio/", "openimageio/", "opensubdiv/", "imath/"], check=True)
		subprocess.run(["git", "checkout", clone_commit_sha], check=True)

	cycles_deps_dir = cycles_deps_dir +"/" +clone_dir_name
	cmake_args.append("-DUNIRENDER_PREBUILT_BINARY_LOCATION=" +cycles_deps_dir)

	# OIDN
	oidn_root = cycles_deps_dir +"/openimagedenoise"
	cmake_args.append("-DDEPENDENCY_OPENIMAGEDENOISE_INCLUDE=" +oidn_root +"/include")
	if platform == "linux":
		cmake_args.append("-DDEPENDENCY_OPENIMAGEDENOISE_LIBRARY=" +oidn_root +"/lib/libOpenImageDenoise.so")
		cmake_args.append("-DDEPENDENCY_OPENIMAGEDENOISE_CORE_LIBRARY=" +oidn_root +"/lib/libOpenImageDenoise_core.so.2.3.0")
	else:
		cmake_args.append("-DDEPENDENCY_OPENIMAGEDENOISE_LIBRARY=" +oidn_root +"/lib/openimagedenoise.lib")
		cmake_args.append("-DDEPENDENCY_OPENIMAGEDENOISE_CORE_LIBRARY=" +oidn_root +"/lib/OpenImageDenoise_core.lib")

	# TBB
	tbb_root = cycles_deps_dir +"/tbb"
	if platform == "linux":
		cmake_args.append("-DDEPENDENCY_TBB_LIBRARY=" +tbb_root +"/lib/libtbb.so")
	else:
		cmake_args.append("-DDEPENDENCY_TBB_LIBRARY=" +tbb_root +"/lib/tbb.lib")

	# OpenColorIO
	ocio_root = cycles_deps_dir +"/opencolorio"
	cmake_args.append("-DDEPENDENCY_OPENCOLORIO_INCLUDE=" +ocio_root +"/include")
	if platform == "linux":
		cmake_args.append("-DDEPENDENCY_OPENCOLORIO_LIBRARY=" +ocio_root +"/lib/libOpenColorIO.so")
	else:
		cmake_args.append("-DDEPENDENCY_OPENCOLORIO_LIBRARY=" +ocio_root +"/lib/OpenColorIO.lib")

	# Required by OpenColorIO
	imath_root = cycles_deps_dir +"/imath"
	cmake_args.append("-DDEPENDENCY_IMATH_INCLUDE=" +imath_root +"/include")
	if platform == "linux":
		cmake_args.append("-DDEPENDENCY_IMATH_LIBRARY=" +imath_root +"/lib/libImath.so")
	else:
		cmake_args.append("-DDEPENDENCY_IMATH_LIBRARY=" +imath_root +"/lib/Imath.lib")

	# OpenImageIO
	oiio_root = cycles_deps_dir +"/openimageio"
	cmake_args.append("-DDEPENDENCY_OPENIMAGEIO_INCLUDE=" +oiio_root +"/include")
	if platform == "linux":
		cmake_args.append("-DDEPENDENCY_OPENIMAGEIO_LIBRARY=" +oiio_root +"/lib/libOpenImageIO.so")
	else:
		cmake_args.append("-DDEPENDENCY_OPENIMAGEIO_LIBRARY=" +oiio_root +"/lib/OpenImageIO.lib")

	# OpenSubDiv
	osd_root = cycles_deps_dir +"/opensubdiv"
	cmake_args.append("-DDEPENDENCY_OPENSUBDIV_INCLUDE=" +osd_root +"/include")
	if platform == "linux":
		cmake_args.append("-DDEPENDENCY_OPENSUBDIV_LIBRARY=" +osd_root +"/lib/libosdGPU.so")
		cmake_args.append("-DDEPENDENCY_OPENSUBDIV_CPU_LIBRARY=" +osd_root +"/lib/libosdCPU.so")
	else:
		cmake_args.append("-DDEPENDENCY_OPENSUBDIV_LIBRARY=" +osd_root +"/lib/osdGPU.lib")
		cmake_args.append("-DDEPENDENCY_OPENSUBDIV_CPU_LIBRARY=" +osd_root +"/lib/osdCPU.lib")
else:
	########## ISPC ##########
	ispc_version = "v1.21.0"
	if platform == "linux":
		ispc_root = deps_dir +"/ispc-" +ispc_version +"-linux.tar.gz"
		if not Path(ispc_root).is_dir():
			print_msg("ISPC not found. Downloading...")
			zip_name = "ispc-" +ispc_version +"-linux.tar.gz"
			http_extract("https://github.com/ispc/ispc/releases/download/" +ispc_version +"/" +zip_name,format="tar.gz")
	else:
		ispc_root = deps_dir +"/ispc-" +ispc_version +"-windows"
		if not Path(ispc_root).is_dir():
			print_msg("ISPC not found. Downloading...")
			zip_name = "ispc-" +ispc_version +"-windows.zip"
			http_extract("https://github.com/ispc/ispc/releases/download/" +ispc_version +"/" +zip_name)

	########## TBB ##########
	one_tbb_root = deps_dir +"/tbb2019_20190605oss"
	if not Path(one_tbb_root).is_dir():
		print_msg("oneTBB not found. Downloading...")
		if platform == "linux":
			zip_name = "tbb2019_20190605oss_lin.tgz"
			http_extract("https://github.com/oneapi-src/oneTBB/releases/download/2019_U8/" +zip_name,format="tar.gz")
		else:
			zip_name = "tbb2019_20190605oss_win.zip"
			http_extract("https://github.com/oneapi-src/oneTBB/releases/download/2019_U8/" +zip_name)

	if platform == "linux":
		cmake_args.append("-DDEPENDENCY_TBB_LIBRARY=" +one_tbb_root +"/lib/intel64/gcc4.7/libtbb.so.2")
	else:
		cmake_args.append("-DDEPENDENCY_TBB_LIBRARY=" +one_tbb_root +"/lib/intel64/vc14/tbb.lib")
		cp(one_tbb_root +"/bin/intel64/vc14/tbb.dll",one_tbb_root +"/lib/intel64/vc14/")

	########## OIDN ##########
	os.chdir(deps_dir)
	oidn_root = deps_dir +"/oidn"
	if not Path(oidn_root).is_dir():
		print_msg("oidn not found. Downloading...")
		git_clone("https://github.com/RenderKit/oidn.git")

	os.chdir(oidn_root)
	reset_to_commit("713ec7838ba650f99e0a896549c0dca5eeb3652d") # v2.2.2

	print_msg("Build oidn")
	mkdir("build",cd=True)

	cmake_configure("..",generator,["-DTBB_ROOT=" +one_tbb_root,"-DTBB_INCLUDE_DIR=" +one_tbb_root +"/include"])
	cmake_build(build_config,["OpenImageDenoise"])

	cmake_args.append("-DDEPENDENCY_OPENIMAGEDENOISE_INCLUDE=" +oidn_root +"/include")
	if platform == "linux":
		if generator=="Ninja Multi-Config":
			cmake_args.append("-DDEPENDENCY_OPENIMAGEDENOISE_LIBRARY=" +oidn_root +"/build/"+build_config +"/libOpenImageDenoise.so")
			cmake_args.append("-DDEPENDENCY_OPENIMAGEDENOISE_CORE_LIBRARY=" +oidn_root +"/build/"+build_config +"/libOpenImageDenoise_core.so")
		else:
			cmake_args.append("-DDEPENDENCY_OPENIMAGEDENOISE_LIBRARY=" +oidn_root +"/build/libOpenImageDenoise.so")
			cmake_args.append("-DDEPENDENCY_OPENIMAGEDENOISE_CORE_LIBRARY=" +oidn_root +"/build/libOpenImageDenoise_core.so")

	else:
		cmake_args.append("-DDEPENDENCY_OPENIMAGEDENOISE_LIBRARY=" +oidn_root +"/build/" +build_config +"/OpenImageDenoise.lib")
		cmake_args.append("-DDEPENDENCY_OPENIMAGEDENOISE_CORE_LIBRARY=" +oidn_root +"/build/" +build_config +"/OpenImageDenoise_core.lib")

	########## OCIO ##########
	os.chdir(deps_dir)
	ocio_root = deps_dir +"/OpenColorIO"
	if not Path(ocio_root).is_dir():
		print_msg("ocio not found. Downloading...")
		git_clone("https://github.com/Silverlan/OpenColorIO.git")

	os.chdir(ocio_root)
	# Note: Version 2.2.0 of OpenColorIO introduces a zlib dependency, which causes conflicts with our zlib installation, so we're stuck
	# with the older version for now.
	#Should no longer happen with zlib bump.
	# TODO: minizip-ng broken
	print_msg("Build ocio")
	mkdir("build",cd=True)
	reset_to_commit("8c767e5")

	configArgs = []
	if platform == "linux":
		configArgs.append("-DOCIO_BUILD_PYTHON=OFF")
	cmake_configure("..",generator,configArgs)
	cmake_build(build_config,["OpenColorIO"])

	if platform == "linux":
		cp(ocio_root +"/build/include/OpenColorIO/OpenColorABI.h",ocio_root +"/include/OpenColorIO/")

	cp(ocio_root +"/build/include/OpenColorIO/OpenColorABI.h",ocio_root +"/include/OpenColorIO/")

	cmake_args.append("-DDEPENDENCY_OPENCOLORIO_INCLUDE=" +ocio_root +"/include")
	if platform == "linux":
		if generator=="Ninja Multi-Config":
			cmake_args.append("-DDEPENDENCY_OPENCOLORIO_LIBRARY=" +ocio_root +"/build/src/OpenColorIO/"+build_config +"/libOpenColorIO.so")
		else:
			cmake_args.append("-DDEPENDENCY_OPENCOLORIO_LIBRARY=" +ocio_root +"/build/src/OpenColorIO/libOpenColorIO.so")
	else:
		cmake_args.append("-DDEPENDENCY_OPENCOLORIO_LIBRARY=" +ocio_root +"/build/src/OpenColorIO/" +build_config +"/OpenColorIO.lib")

	########## OIIO ##########
	execbuildscript(os.path.dirname(os.path.realpath(__file__)) +"/build_oiio.py")
	os.chdir(deps_dir)

	########## OpenSubdiv ##########
	os.chdir(deps_dir)
	subdiv_root = deps_dir +"/OpenSubdiv"
	if not Path(subdiv_root).is_dir():
		print_msg("OpenSubdiv not found. Downloading...")
		git_clone("https://github.com/PixarAnimationStudios/OpenSubdiv.git")

	os.chdir(subdiv_root)
	reset_to_commit("7d0ab5530feef693ac0a920585b5c663b80773b3") # v3.6.0

	print_msg("Build OpenSubdiv")
	mkdir("build",cd=True)

	cmake_configure("..",generator,["-DTBB_ROOT=" +one_tbb_root,"-DTBB_INCLUDE_DIR=" +one_tbb_root +"/include","-D NO_PTEX=1","-D NO_DOC=1","-D NO_OMP=1","-D NO_TBB=1","-D NO_CUDA=1","-D NO_OPENCL=1","-D NO_CLEW=1","-D NO_EXAMPLES=1","-D NO_DX=1"])
	cmake_build(build_config,["osd_static_cpu","osd_static_gpu"])

	cmake_args.append("-DDEPENDENCY_OPENSUBDIV_INCLUDE=" +subdiv_root +"")
	if platform == "linux":
		if generator=="Ninja Multi-Config":
			cmake_args.append("-DDEPENDENCY_OPENSUBDIV_LIBRARY=" +subdiv_root +"/build/lib/"+build_config+"/libosdGPU.a")
			cmake_args.append("-DDEPENDENCY_OPENSUBDIV_CPU_LIBRARY=" +subdiv_root +"/build/lib/"+build_config+"/libosdCPU.a")
		else:
			cmake_args.append("-DDEPENDENCY_OPENSUBDIV_LIBRARY=" +subdiv_root +"/build/lib/libosdGPU.a")
			cmake_args.append("-DDEPENDENCY_OPENSUBDIV_CPU_LIBRARY=" +subdiv_root +"/build/lib/libosdCPU.a")
	else:
		cmake_args.append("-DDEPENDENCY_OPENSUBDIV_LIBRARY=" +subdiv_root +"/build/lib/" +build_config +"/osdGPU.lib")
		cmake_args.append("-DDEPENDENCY_OPENSUBDIV_CPU_LIBRARY=" +subdiv_root +"/build/lib/" +build_config +"/osdCPU.lib")

########## cycles ##########

parser = argparse.ArgumentParser(description='pr_unirender build script', allow_abbrev=False, formatter_class=argparse.ArgumentDefaultsHelpFormatter, epilog="")
parser.add_argument("--build-cycles", type=str2bool, nargs='?', const=True, default=False, help="Build the Cycles library (otherwise uses pre-built binaries).")
args,unknown = parser.parse_known_args()
args = vars(args)

build_cycles = build_all or args["build_cycles"]
if build_cycles:
	print_msg("Running cycles build script...")
	cmake_args.append("-DPR_UNIRENDER_WITH_CYCLES=1")
	execbuildscript(os.path.dirname(os.path.realpath(__file__)) +"/build_cycles.py")
else:
	print_msg("Downloading prebuilt cycles binaries...")
	os.chdir(install_dir)
	install_prebuilt_binaries("https://github.com/Silverlan/UniRender_Cycles/releases/download/latest/")

os.chdir(deps_dir)

########## util_ocio ##########
utilocio_root = root +"/external_libs/util_ocio"
if not Path(utilocio_root).is_dir():
    print_msg("util_ocio not found. Downloading...")
    os.chdir(root +"/external_libs")
    git_clone("https://github.com/Silverlan/util_ocio.git","util_ocio")
os.chdir(utilocio_root)
reset_to_commit("19d48ea21c58f79d7359075197c786c705d7817f")

cmake_args.append("-DDEPENDENCY_UTIL_OCIO_INCLUDE=" +utilocio_root +"/include")

########## glog ##########
if platform == "win32":
	os.chdir(deps_dir)
	glog_root = deps_dir +"/glog"
	if not Path(glog_root).is_dir():
	    print_msg("glog not found. Downloading...")
	    git_clone("https://github.com/google/glog")

	os.chdir(glog_root)
	reset_to_commit("b33e3ba")

	print_msg("Build ocio")
	mkdir("build",cd=True)

	cmake_configure("..",generator)
	cmake_build(build_config)

	cp(glog_root +"/src/glog/log_severity.h",glog_root +"/build/glog/")
	cp(glog_root +"/src/glog/platform.h",glog_root +"/build/glog/")

	cmake_args.append("-DDEPENDENCY_CYCLES_GLOG_INCLUDE=" +glog_root +"/build")
	cmake_args.append("-DDEPENDENCY_CYCLES_GLOG_LIBRARY=" +glog_root +"/build/" +build_config +"/glog.lib")

########## gflags ##########
if platform == "win32":
	os.chdir(deps_dir)
	gflags_root = deps_dir +"/gflags"
	if not Path(gflags_root).is_dir():
	    print_msg("gflags not found. Downloading...")
	    git_clone("https://github.com/gflags/gflags.git")

	os.chdir(gflags_root)
	# reset_to_commit("e171aa2") # Causes build errors for unknown reasons
	subprocess.run(["git","reset","--hard","e171aa2"],check=True)

	print_msg("Build gflags")
	mkdir("build_files",cd=True)

	cmake_configure("..",generator)
	cmake_build(build_config)

	cmake_args.append("-DDEPENDENCY_GFLAGS_INCLUDE=" +gflags_root +"/build_files/include")
	cmake_args.append("-DDEPENDENCY_GFLAGS_LIBRARY=" +gflags_root +"/build_files/lib/" +build_config +"/gflags_static.lib")

########## render_raytracing tool ##########
if build_cycles:
    os.chdir(tools)
    rr_tool_root = tools +"/render_raytracing"
    if not Path(rr_tool_root).is_dir():
        print_msg("render_raytracing tool not found. Downloading...")
        git_clone("https://github.com/Silverlan/render_raytracing.git")

    os.chdir(rr_tool_root)
    reset_to_commit("10293584e1c4ae5d674dcdc9b3c82e354e561a11")

    additional_build_targets.append("render_raytracing")
#else:
# TODO: Download pre-built version of render_raytracing tool

########## Unirender ##########
unirender_root = root +"/external_libs/util_raytracing"
if not Path(unirender_root).is_dir():
    print_msg("Unirender not found. Downloading...")
    os.chdir(root +"/external_libs")
    git_clone("https://github.com/Silverlan/UniRender.git","util_raytracing")

os.chdir(unirender_root)
reset_to_commit("f5d113b79a99ed2227f5e81cc637566ddc8f3204")

cmake_args.append("-DDEPENDENCY_UTIL_RAYTRACING_INCLUDE=" +unirender_root +"/include")
