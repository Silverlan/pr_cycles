import os
import tarfile
import subprocess
import argparse
import shutil
import config
from sys import platform
from pathlib import Path

os.chdir(deps_dir)

# These need to match the cycles version that is being used (see build_cycles.py for more information)
cycles_lib_windows_x64_commit_sha = "cdef408"
cycles_lib_linux_x64_commit_sha = "eacf548"
unirender_cycles_release_tag = "2025-08-21"
use_prebuilt_binaries = True

if use_prebuilt_binaries:
	if build_all:
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

		cycles_lib_dir = cycles_deps_dir +"/" +clone_dir_name
		# Note: These need to correspond to the CI workflows
		copy_prebuilt_directory(cycles_lib_dir +"/imath", "imath")
		copy_prebuilt_directory(cycles_lib_dir +"/opencolorio", "opencolorio")
		copy_prebuilt_directory(cycles_lib_dir +"/openimagedenoise", "openimagedenoise")
		copy_prebuilt_directory(cycles_lib_dir +"/openimageio", "openimageio")
		copy_prebuilt_directory(cycles_lib_dir +"/opensubdiv", "opensubdiv")
		copy_prebuilt_directory(cycles_lib_dir +"/tbb", "tbb")

		cycles_deps_dir = cycles_deps_dir +"/" +clone_dir_name

		# OIDN
		oidn_root = cycles_deps_dir +"/openimagedenoise"

		# TBB
		tbb_root = cycles_deps_dir +"/tbb"

		# OpenColorIO
		ocio_root = cycles_deps_dir +"/opencolorio"

		# Required by OpenColorIO
		imath_root = cycles_deps_dir +"/imath"

		# OpenImageIO
		oiio_root = cycles_deps_dir +"/openimageio"

		# OpenSubDiv
		osd_root = cycles_deps_dir +"/opensubdiv"
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

	if platform == "win32":
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

########## cycles ##########

parser = argparse.ArgumentParser(description='pr_unirender build script', allow_abbrev=False, formatter_class=argparse.ArgumentDefaultsHelpFormatter, epilog="")
parser.add_argument("--build-cycles", type=str2bool, nargs='?', const=True, default=False, help="Build the Cycles library (otherwise uses pre-built binaries).")
args,unknown = parser.parse_known_args()
args = vars(args)

build_cycles = build_all or args["build_cycles"]
if build_cycles:
	print_msg("Running cycles build script...")
	execbuildscript(os.path.dirname(os.path.realpath(__file__)) +"/build_cycles.py")
else:
	print_msg("Downloading prebuilt cycles binaries...")
	os.chdir(install_dir)

	staging_dir = get_staging_dir()
	mkpath(staging_dir)
	os.chdir(staging_dir)
	install_prebuilt_binaries("https://github.com/Silverlan/UniRender_Cycles/releases/download/" +unirender_cycles_release_tag +"/")

os.chdir(deps_dir)

########## util_ocio ##########
utilocio_root = root +"/external_libs/util_ocio"
if not Path(utilocio_root).is_dir():
    print_msg("util_ocio not found. Downloading...")
    os.chdir(root +"/external_libs")
    git_clone("https://github.com/Silverlan/util_ocio.git","util_ocio","feat/cxx_module")
os.chdir(utilocio_root)
reset_to_commit("7e1ff73ee7ea03ca7909e628fbcc45d7641e682e")

if platform == "win32" and build_all:
	########## glog ##########
	os.chdir(deps_dir)
	glog_root = deps_dir +"/glog"
	if not Path(glog_root).is_dir():
		print_msg("glog not found. Downloading...")
		git_clone("https://github.com/google/glog")

	os.chdir(glog_root)
	reset_to_commit("b33e3ba")

	print_msg("Build ocio")
	mkdir("build",cd=True)

	cmake_configure("..",generator,["-DCMAKE_POLICY_VERSION_MINIMUM=4.0"])
	cmake_build(build_config)

	copy_prebuilt_headers(glog_root +"/src", "glog")
	copy_prebuilt_headers(glog_root +"/build", "glog")
	copy_prebuilt_binaries(glog_root +"/build/" +build_config, "glog")

	########## gflags ##########
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

	cmake_configure("..",generator,["-DCMAKE_POLICY_VERSION_MINIMUM=4.0"])
	cmake_build(build_config)

	copy_prebuilt_headers(gflags_root +"/build_files/include", "gflags")
	copy_prebuilt_binaries(gflags_root +"/build_files/lib/" +build_config, "gflags")

########## render_raytracing tool ##########
os.chdir(tools)
rr_tool_root = tools +"/render_raytracing"
if not Path(rr_tool_root).is_dir():
	print_msg("render_raytracing tool not found. Downloading...")
	git_clone("https://github.com/Silverlan/render_raytracing.git")

os.chdir(rr_tool_root)
reset_to_commit("b9c4699ef8430769f95a655046f7dd3114da0bb1")

########## Unirender ##########
unirender_root = root +"/external_libs/util_raytracing"
if not Path(unirender_root).is_dir():
    print_msg("Unirender not found. Downloading...")
    os.chdir(root +"/external_libs")
    git_clone("https://github.com/Silverlan/UniRender.git","util_raytracing","feat/cxx_module")

os.chdir(unirender_root)
reset_to_commit("9b0853f4429be9626a76c99af83a370ac43c18bc")
