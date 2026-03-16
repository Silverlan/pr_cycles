import os, sys
from pathlib import Path

sys.path.insert(0, str(Path(os.path.abspath(os.path.dirname(__file__))).parent.parent.parent.parent / "build_scripts"))
from scripts.shared import *

def main(cycles_arch="all"):
	build_config_tp = config.build_config_tp
	deps_dir = config.deps_dir
	generator = config.generator
	chdir_mkdir(deps_dir)

	# To update Cycles to a newer version, follow these steps:
	# - Pull all changes from remote blender/cycles:main into the fork at https://github.com/Silverlan/cycles
	# - Find the latest stable release of Cycles on https://github.com/blender/cycles/tags
	# - Rebase the "pragma" branch on main for the commit of that release
	# - Copy the branch with the new Cycles version as branch name
	# - Copy the branch name and commit id to "cycles_commit_sha" below
	# - Update preprocessor definitions for Cycles in CMakeLists.txt of external_libs/cycles/CMakeLists.txt
	# - Update the versions of tbb, oidn, ocio, oiio, opensubdiv libraries in setup.py to match Cycles versions
	# - Go to https://github.com/blender/cycles/tree/main/lib for the commit of the Cycles version
	#   - Grab the commit ids for linux_x64 and windows_x64 and apply them to cycles_lib_*_x64_commit_sha in setup.py
	cycles_commit_sha = "28bd4a8c97e4398417361dd8793a95ca92cda0c2"
	cycles_branch = "v4.4.0"

	########## Cycles ##########
	os.chdir(deps_dir)
	cyclesRoot = deps_dir +"/cycles"
	if not Path(cyclesRoot).is_dir():
		print_msg("cycles not found. Downloading...")
		git_clone("https://github.com/Silverlan/cycles.git",branch=cycles_branch)

	os.chdir(cyclesRoot)

	if platform == "linux":
		cyclesDepsRoot = cyclesRoot +"/lib/linux_x64"
	else:
		cyclesDepsRoot = cyclesRoot +"/lib/windows_x64"
	if platform == "win32":
		cyclesLibDir = cyclesRoot +"/install"
	else:
		cyclesLibDir = cyclesRoot +"/install/lib"

	lastBuildCommit = None
	lastbuildshaFile = cyclesRoot +"/lastbuildsha"
	if Path(lastbuildshaFile).is_file():
		lastBuildCommit = Path(lastbuildshaFile).read_text()

	targetCommit = cycles_commit_sha
	if lastBuildCommit != targetCommit:
		print_msg("Downloading cycles dependencies...")
		subprocess.run(["git","fetch"],check=True)
		subprocess.run(["git","reset","--hard",targetCommit],check=True)
		if platform == "win32":
			scriptPath = cyclesRoot +"/src/cmake/make_update.py"
			python_interpreter = sys.executable
			command = [python_interpreter, scriptPath, "--no-cycles"]
			subprocess.run(command)

	print_msg("Download dependencies")
	os.chdir(cyclesRoot)
	if platform == "linux":
		subprocess.run(["make","update"],check=True)
		lib_path = "lib/linux_x64"
	else:
		subprocess.run(["make.bat","update"],check=True)
		lib_path = "lib/windows_x64"
	# The cycles scripts are supposed to initialize the lfs data but this does not work properly, so we'll force it here
	subprocess.run(["git", "lfs", "install", "--force"],check=True)
	subprocess.run(["git", "-C", lib_path, "lfs", "install", "--local", "--force"],check=True)
	subprocess.run(["git", "-C", lib_path, "lfs", "pull"],check=True)

	if platform == "linux":
		# Patch openvdb
		# Due to a clang compiler error, we have to apply a patch for openvdb manually for now.
		print_msg("Applying openvdb patch...")
		openvdb_root = deps_dir +"/cycles/lib/linux_x64/openvdb"
		script_dir = os.path.dirname(os.path.abspath(__file__))
		os.chdir(script_dir)
		with open("openvdb.patch", "rb") as patch_file:
			patch_data = patch_file.read()
		subprocess.run(["patch", "-N", openvdb_root +"/include/nanovdb/util/GridBuilder.h"], input=patch_data)

	print_msg("Build cycles")

	os.chdir(cyclesRoot)
	mkdir("build",cd=True)
	oiio_root_dir = cyclesDepsRoot + "/openimageio"
	oidn_root_dir = cyclesDepsRoot + "/openimagedenoise"

	# Building cycles rebuilds shaders every time, which can take a very long time.
	# Since there's usually no reason to rebuild the shaders, we'll only build if the head commit has
	# changed since the last build.
	curCommitId = subprocess.check_output(["git","rev-parse","HEAD"]).decode(sys.stdout.encoding)
	if lastBuildCommit != curCommitId:
		args = []
		if platform == "linux":
			zlib = get_library_root_dir("zlib") +"build/libz.a"
			args.append("-DWITH_CYCLES_CUDA_BINARIES=ON")
			args.append("-DWITH_CYCLES_DEVICE_OPTIX=ON")
			args.append("-DWITH_CYCLES_DEVICE_CUDA=ON")
		else:
			zlib = get_library_lib_dir("zlib") +"z.lib"
			args.append("-DWITH_CYCLES_CUDA_BINARIES=ON")
			args.append("-DWITH_CYCLES_DEVICE_OPTIX=ON")
			args.append("-DWITH_CYCLES_DEVICE_CUDA=ON")
			# We have to disable /jumptablerdata because it can cause linker errors when building UniRender_cycles with clang.
			args.append("-DWITH_CYCLES_JUMPTABLERDATA=OFF")
		
		# OSL is disabled because we don't need it and it causes build errors on the GitHub runner.
		args.append("-DWITH_CYCLES_OSL=OFF")

		# Hydra delegate is disabled because we don't need it and it causes build errors on the (Windows) GitHub runner.
		args.append("-DWITH_CYCLES_HYDRA_RENDER_DELEGATE=OFF")
		args.append("-DWITH_CYCLES_USD=OFF")

		if cycles_arch != "all":
			args.append("-DCYCLES_CUDA_BINARIES_ARCH=" +cycles_arch)
		
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
			cmake_build(build_config_tp,targets=["install"])
		else:
			# Note: cycles *has* to be built with msvc on Windows because nvcc does not support
			# any other compilers on Windows.
			cmake_configure("..",generator,args)
			cmake_build(build_config_tp)
			cmake_build(build_config_tp,targets=["install"])

		with open(lastbuildshaFile, 'w') as filetowrite:
			filetowrite.write(curCommitId)
	else:
		print_msg("Head commit has not changed, skipping build...")

	if platform == "linux":
		cycles_lib_dir = cyclesRoot +"/lib/linux_x64/"
	else:
		cycles_lib_dir = cyclesRoot +"/lib/windows_x64/"
	# Note: These need to correspond to the CI workflows
	copy_prebuilt_directory(cycles_lib_dir +"alembic", "alembic")
	copy_prebuilt_directory(cycles_lib_dir +"embree", "embree")
	copy_prebuilt_directory(cycles_lib_dir +"jpeg", "jpeg")
	copy_prebuilt_directory(cycles_lib_dir +"llvm", "llvm")
	copy_prebuilt_directory(cycles_lib_dir +"openexr", "openexr")
	copy_prebuilt_directory(cycles_lib_dir +"openvdb", "openvdb")
	copy_prebuilt_directory(cycles_lib_dir +"png", "png")
	copy_prebuilt_directory(cycles_lib_dir +"tiff", "tiff")
	copy_prebuilt_directory(cycles_lib_dir +"zstd", "zstd")
	if platform == "win32":
		copy_prebuilt_directory(cycles_lib_dir +"pugixml", "pugixml")

	copy_prebuilt_directory(cyclesRoot +"/third_party/atomic", dest_dir=get_library_include_dir("cycles"))
	copy_prebuilt_directory(cyclesRoot +"/third_party/cuew", dest_dir=get_library_include_dir("cycles"))
	copy_prebuilt_directory(cyclesRoot +"/third_party/hipew", dest_dir=get_library_include_dir("cycles"))
	copy_prebuilt_directory(cyclesRoot +"/third_party/libc_compat", dest_dir=get_library_include_dir("cycles"))
	copy_prebuilt_directory(cyclesRoot +"/third_party/mikktspace", dest_dir=get_library_include_dir("cycles"))
	copy_prebuilt_directory(cyclesRoot +"/third_party/sky", dest_dir=get_library_include_dir("cycles"))

	cycles_build_lib_dir = cyclesRoot +"/build/lib"
	if platform == "win32":
		cycles_build_lib_dir += "/" +build_config_tp
	copy_prebuilt_binaries(cycles_build_lib_dir, "cycles")
	copy_prebuilt_binaries(cyclesRoot +"/install/lib", "cycles")
	if platform == "win32":
		copy_files(["*.dll"], cyclesRoot +"/install", get_library_root_dir("cycles") +"/bin")
	copy_files(["*.ptx", "*.cubin", "*.zst"], cyclesRoot +"/install/lib", get_library_root_dir("cycles") +"/lib/lib")

	copy_prebuilt_directory(cyclesRoot +"/install/source", dest_dir=get_library_root_dir("cycles") +"/source")
	copy_prebuilt_headers(cyclesRoot +"/src", "cycles")

	########## checks ##########

	cyclesCmakeCacheFile = cyclesRoot +"/build/CMakeCache.txt"

	strIdx = open(cyclesCmakeCacheFile, 'r').read().find('WITH_CYCLES_DEVICE_CUDA:BOOL=OFF')
	if strIdx != -1:
		print_msg("CUDA is disabled for Cycles! Is CUDA installed on the system?")

	strIdx = open(cyclesCmakeCacheFile, 'r').read().find('WITH_CYCLES_DEVICE_OPTIX:BOOL=OFF')
	if strIdx != -1:
		print_msg("OptiX is disabled for Cycles! Is OptiX installed on the system?")

if __name__ == "__main__":
	main(sys.argv[1] if len(sys.argv) > 1 else "all")
