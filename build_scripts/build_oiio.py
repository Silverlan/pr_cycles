import os
import subprocess
from sys import platform
from pathlib import Path

########## oiio ##########
print_msg("Building openimageio...")


if platform == "linux":
	triplet_dir = os.path.abspath(__file__)+"/triplets"
	subprocess.run([vcpkg_root +"/vcpkg","install","openimageio:x64-linux-dynamic",f"--overlay-triplets=\"{triplet_dir}\""],check=True)
	cmake_args.append("-DDEPENDENCY_OPENIMAGEIO_INCLUDE=" +deps_dir +"/vcpkg/installed/x64-linux-dynamic/include")
	cmake_args.append("-DDEPENDENCY_OPENIMAGEIO_LIBRARY=" +deps_dir +"/vcpkg/installed/x64-linux-dynamic/lib/libOpenImageIO.so")
else:
	subprocess.run([vcpkg_root +"/vcpkg","install","openimageio"],check=True)
	cmake_args.append("-DDEPENDENCY_OPENIMAGEIO_INCLUDE=" +deps_dir +"/vcpkg/installed/x64-windows/include")
	cmake_args.append("-DDEPENDENCY_OPENIMAGEIO_LIBRARY=" +deps_dir +"/vcpkg/installed/x64-windows/lib/OpenImageIO.lib")
