import os
import tarfile
import subprocess
import argparse
from sys import platform
from pathlib import Path

os.chdir(deps_dir)

########## ISPC ##########
if platform == "linux":
	ispc_root = deps_dir +"/ispc-v1.17.0-linux.tar.gz"
	if not Path(ispc_root).is_dir():
	    print_msg("ISPC not found. Downloading...")
	    zip_name = "ispc-v1.17.0-linux.tar.gz"
	    http_extract("https://github.com/ispc/ispc/releases/download/v1.17.0/" +zip_name,format="tar.gz")
else:
	ispc_root = deps_dir +"/ispc-v1.17.0-windows"
	if not Path(ispc_root).is_dir():
	    print_msg("ISPC not found. Downloading...")
	    zip_name = "ispc-v1.17.0-windows.zip"
	    http_extract("https://github.com/ispc/ispc/releases/download/v1.17.0/" +zip_name)

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
    git_clone("https://github.com/OpenImageDenoise/oidn.git")

os.chdir(oidn_root)
reset_to_commit("d959bac5b7130b31c41095811ddfbe58c4cf03f4")

print_msg("Build oidn")
mkdir("build",cd=True)

cmake_configure("..",generator,["-DTBB_ROOT=" +one_tbb_root,"-DTBB_INCLUDE_DIR=" +one_tbb_root +"/include"])
cmake_build(build_config,["OpenImageDenoise"])

cmake_args.append("-DDEPENDENCY_OPENIMAGEDENOISE_INCLUDE=" +oidn_root +"/include")
if platform == "linux":
	if generator=="Ninja Multi-Config":
		cmake_args.append("-DDEPENDENCY_OPENIMAGEDENOISE_LIBRARY=" +oidn_root +"/build/"+build_config +"/libOpenImageDenoise.so")
	else:
		cmake_args.append("-DDEPENDENCY_OPENIMAGEDENOISE_LIBRARY=" +oidn_root +"/build/libOpenImageDenoise.so")

else:
	cmake_args.append("-DDEPENDENCY_OPENIMAGEDENOISE_LIBRARY=" +oidn_root +"/build/" +build_config +"/OpenImageDenoise.lib")

########## OCIO ##########
os.chdir(deps_dir)
ocio_root = deps_dir +"/OpenColorIO"
if not Path(ocio_root).is_dir():
    print_msg("ocio not found. Downloading...")
    git_clone("https://github.com/SlawekNowy/OpenColorIO.git")

os.chdir(ocio_root)
# Note: Version 2.2.0 of OpenColorIO introduces a zlib dependency, which causes conflicts with our zlib installation, so we're stuck
# with the older version for now.
#Should no longer happen with zlib bump.
# TODO: minizip-ng broken
print_msg("Build ocio")
mkdir("build",cd=True)
reset_to_commit("025e7c07794913a8cf8191247777393300797a0b")

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

########## cycles ##########

parser = argparse.ArgumentParser(description='pr_unirender build script', allow_abbrev=False, formatter_class=argparse.ArgumentDefaultsHelpFormatter, epilog="")
parser.add_argument("--build-cycles", type=str2bool, nargs='?', const=True, default=False, help="Build the Cycles library (otherwise uses pre-built binaries).")
args,unknown = parser.parse_known_args()
args = vars(args)

if build_all or args["build_cycles"]:
	print_msg("Running cycles build script...")
	execbuildscript(os.path.dirname(os.path.realpath(__file__)) +"/build_cycles.py")
else:
	print_msg("Downloading prebuilt cycles binaries...")
	os.chdir(install_dir)
	install_prebuilt_binaries("https://github.com/Silverlan/UniRender_Cycles/releases/download/latest/")

os.chdir(deps_dir)

########## OpenSubdiv ##########
os.chdir(deps_dir)
subdiv_root = deps_dir +"/OpenSubdiv"
if not Path(subdiv_root).is_dir():
    print_msg("OpenSubdiv not found. Downloading...")
    git_clone("https://github.com/PixarAnimationStudios/OpenSubdiv.git")

os.chdir(subdiv_root)
reset_to_commit("82ab1b9f54c87fdd7e989a3470d53e137b8ca270")

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

########## util_ocio ##########
utilocio_root = root +"/external_libs/util_ocio"
if not Path(utilocio_root).is_dir():
    print_msg("util_ocio not found. Downloading...")
    os.chdir(root +"/external_libs")
    git_clone("https://github.com/Silverlan/util_ocio.git","util_ocio")

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
os.chdir(tools)
rr_tool_root = tools +"/render_raytracing"
if not Path(rr_tool_root).is_dir():
    print_msg("render_raytracing tool not found. Downloading...")
    git_clone("https://github.com/Silverlan/render_raytracing.git")

os.chdir(rr_tool_root)
reset_to_commit("f0223483207962d93a52b6e86df561b9439246b2")

additional_build_targets.append("render_raytracing")

########## Unirender ##########
unirender_root = root +"/external_libs/util_raytracing"
if not Path(unirender_root).is_dir():
    print_msg("Unirender not found. Downloading...")
    os.chdir(root +"/external_libs")
    git_clone("https://github.com/Silverlan/UniRender.git","util_raytracing")

os.chdir(unirender_root)
reset_to_commit("6dc26132832fe71c3663ed640ca772ece68812d6")

cmake_args.append("-DDEPENDENCY_UTIL_RAYTRACING_INCLUDE=" +unirender_root +"/include")
