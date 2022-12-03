import os
import subprocess
from sys import platform
from pathlib import Path

########## vcpkg ##########
os.chdir(deps_dir)
if platform == "win32":
	os.environ["VCPKG_DEFAULT_TRIPLET"] = "x64-windows"
vcpkgRoot = deps_dir +"/vcpkg"
if not Path(vcpkgRoot).is_dir():
	print_msg("vcpkg not found, downloading...")
	git_clone("https://github.com/Microsoft/vcpkg.git")

if platform == "linux":
	os.chdir("vcpkg")
	subprocess.run(["git","reset","--hard","7d9775a3c3ffef3cbad688d7271a06803d3a2f51"],check=True)
	os.chdir("..")

	subprocess.run([vcpkgRoot +"/bootstrap-vcpkg.sh","-disableMetrics"],check=True,shell=True)
else:
	subprocess.run([vcpkgRoot +"/bootstrap-vcpkg.bat","-disableMetrics"],check=True,shell=True)

########## oiio ##########
print_msg("Building openimageio...")
subprocess.run([vcpkgRoot +"/vcpkg","install","openimageio"],check=True)

if platform == "linux":
	cmake_args.append("-DDEPENDENCY_OPENIMAGEIO_INCLUDE=" +deps_dir +"/vcpkg/installed/x64-linux/include")
	cmake_args.append("-DDEPENDENCY_OPENIMAGEIO_LIBRARY=" +deps_dir +"/vcpkg/installed/x64-linux/lib/libOpenImageIO.a")
else:
	cmake_args.append("-DDEPENDENCY_OPENIMAGEIO_INCLUDE=" +deps_dir +"/vcpkg/installed/x64-windows/include")
	cmake_args.append("-DDEPENDENCY_OPENIMAGEIO_LIBRARY=" +deps_dir +"/vcpkg/installed/x64-windows/lib/OpenImageIO.lib")
