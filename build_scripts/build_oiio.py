import os
import subprocess
from sys import platform
from pathlib import Path

########## oiio ##########
print_msg("Building openimageio...")
subprocess.run([vcpkg_root +"/vcpkg","install","openimageio"],check=True)

if platform == "linux":
	cmake_args.append("-DDEPENDENCY_OPENIMAGEIO_INCLUDE=" +deps_dir +"/vcpkg/installed/x64-linux/include")
	cmake_args.append("-DDEPENDENCY_OPENIMAGEIO_LIBRARY=" +deps_dir +"/vcpkg/installed/x64-linux/lib/libOpenImageIO.a")
else:
	cmake_args.append("-DDEPENDENCY_OPENIMAGEIO_INCLUDE=" +deps_dir +"/vcpkg/installed/x64-windows/include")
	cmake_args.append("-DDEPENDENCY_OPENIMAGEIO_LIBRARY=" +deps_dir +"/vcpkg/installed/x64-windows/lib/OpenImageIO.lib")
