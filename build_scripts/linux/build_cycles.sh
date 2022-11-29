
# cycles
cd "$deps"
cyclesRoot="$deps/cycles"
if [ ! -d "$cyclesRoot" ]; then
	print_hmsg "cycles not found. Downloading..."
	git clone git://git.blender.org/cycles.git --recurse-submodules
	validate_result
fi
cd cycles

unset lastBuildCommit
if [ -f "$cyclesRoot/lastbuildsha" ]; then
    lastBuildCommit=$(cat "$cyclesRoot/lastbuildsha")
fi

targetCommit="b1882be6b1f2e27725ee672d87c5b6f8d6113eb1"
if [ "$lastBuildCommit" != "$targetCommit" ]; then
    print_hmsg "Downloading cycles dependencies..."
    make update
    validate_result

    # The update command above will unfortunately update Cycles to the last commit. This behavior
    # can't be disabled, so we have to reset the commit back to the one we want here.
    # This can cause issues if the Cycles update-script updates the dependencies to newer versions
    # that aren't compatible with the commit we're using, but it can't be helped.
    git reset --hard "$targetCommit"
    validate_result
    print_hmsg "Done!"
fi

# Building the cycles executable causes build errors. We don't need it, but unfortunately cycles doesn't provide us with a
# way to disable it, so we'll have to make some changes to the CMake configuration file.
sed -i -e 's/if(WITH_CYCLES_STANDALONE)/if(false)/g' "$cyclesRoot/src/app/CMakeLists.txt"

print_hmsg "Build cycles"
mkdir -p $PWD/build
cd build

# Cycles doesn't cache properly and rebuilds every time, which can take a long time. For this reason
# we'll only build if the head commit has changed since the last build.
curCommitId=$(git rev-parse HEAD)
validate_result
if [ "$lastBuildCommit" != "$curCommitId" ]; then
	cmake .. -G "$generator" -DWITH_CYCLES_CUDA_BINARIES=ON -DWITH_CYCLES_DEVICE_OPTIX=ON -DWITH_CYCLES_DEVICE_CUDA=ON \
		-DZLIB_INCLUDE_DIR="$zlibRoot" -DZLIB_LIBRARY="$zlibRoot/build/libz.a"
	validate_result
	cmake --build "." --config "$buildConfig"
	validate_result

	echo $curCommitId >| "$cyclesRoot/lastbuildsha"
else
	print_hmsg "Head commit has not changed, skipping build..."
fi

print_hmsg "Done!"

cyclesDepsRoot="$deps/lib/linux_centos7_x86_64"
cmakeArgs=" $cmakeArgs -DDEPENDENCY_CYCLES_INCLUDE=\"$deps/cycles/src\" "
cmakeArgs=" $cmakeArgs -DDEPENDENCY_CYCLES_ROOT=\"$deps/cycles\" "
cmakeArgs=" $cmakeArgs -DDEPENDENCY_CYCLES_BUILD_LOCATION=\"$deps/cycles/build\" "
cmakeArgs=" $cmakeArgs -DDEPENDENCY_CYCLES_ATOMIC_INCLUDE=\"$deps/cycles\third_party\atomic\" "
cmakeArgs=" $cmakeArgs -DDEPENDENCY_CYCLES_OPENIMAGEIO_INCLUDE=\"$cyclesDepsRoot/OpenImageIO/include\" "
cmakeArgs=" $cmakeArgs -DDEPENDENCY_CYCLES_PUGIXML_INCLUDE=\"$cyclesDepsRoot/pugixml/include\" "
cmakeArgs=" $cmakeArgs -DDEPENDENCY_CYCLES_OPENIMAGEDENOISE_INCLUDE=\"$cyclesDepsRoot/openimagedenoise/include\" "
cmakeArgs=" $cmakeArgs -DDEPENDENCY_CYCLES_OPENEXR_INCLUDE=\"$cyclesDepsRoot/openexr/include\" "
cmakeArgs=" $cmakeArgs -DDEPENDENCY_CYCLES_EMBREE_INCLUDE=\"$cyclesDepsRoot/embree/include\" "
cmakeArgs=" $cmakeArgs -DDEPENDENCY_CYCLES_OSL_INCLUDE=\"$cyclesDepsRoot/osl/include\" "
cmakeArgs=" $cmakeArgs -DDEPENDENCY_CYCLES_TBB_INCLUDE=\"$cyclesDepsRoot/tbb/include\" "
cmakeArgs=" $cmakeArgs -DDEPENDENCY_CYCLES_TBB_LIBRARY=\"$cyclesDepsRoot/tbb/lib/libtbb.a\" "
cmakeArgs=" $cmakeArgs -DDEPENDENCY_CYCLES_OPENVDB_LIBRARY_PATH=\"$cyclesDepsRoot/openvdb/lib\" "

cmakeArgs=" $cmakeArgs -DDEPENDENCY_CYCLES_DEPENDENCIES_LOCATION=\"$cyclesDepsRoot\" "
cmakeArgs=" $cmakeArgs -DDEPENDENCY_CYCLES_LIBRARY_LOCATION=\"$cyclesRoot/build/lib\" "

cmakeArgs=" $cmakeArgs -DDEPENDENCY_CYCLES_OPENEXR_INCLUDE=\"$deps/lib/linux_centos7_x86_64/openexr/include\" "
cmakeArgs=" $cmakeArgs -DDEPENDENCY_CYCLES_OPENIMAGEDENOISE_INCLUDE=\"$deps/lib/linux_centos7_x86_64/openimagedenoise/include\" "
cmakeArgs=" $cmakeArgs -DDEPENDENCY_CYCLES_OPENIMAGEIO_INCLUDE=\"$deps/lib/linux_centos7_x86_64/openimageio/include\" "
cmakeArgs=" $cmakeArgs -DDEPENDENCY_CYCLES_OSL_INCLUDE=\"$deps/lib/linux_centos7_x86_64/osl/include\" "
cmakeArgs=" $cmakeArgs -DDEPENDENCY_CYCLES_PUGIXML_INCLUDE=\"$deps/lib/linux_centos7_x86_64/pugixml/include\" "
cmakeArgs=" $cmakeArgs -DDEPENDENCY_CYCLES_EMBREE_INCLUDE=\"$deps/lib/linux_centos7_x86_64/embree/include\" "

cmakeArgs=" $cmakeArgs -DDEPENDENCY_OPENEXR_INCLUDE=\"$cyclesDepsRoot/openexr/include\" "
cmakeArgs=" $cmakeArgs -DDEPENDENCY_OPENEXR_UTIL_LIBRARY=\"$cyclesDepsRoot/openexr/lib/libOpenEXR.a\" "
cmakeArgs=" $cmakeArgs -DDEPENDENCY_OPENEXR_IMATH_INCLUDE=\"$cyclesDepsRoot/imath/include\" "
cmakeArgs=" $cmakeArgs -DDEPENDENCY_OPENEXR_IMATH_LIBRARY=\"$cyclesDepsRoot/imath/lib/libImath.a\" "
cmakeArgs=" $cmakeArgs -DDEPENDENCY_OPENEXR_ILMTHREAD_LIBRARY=\"$cyclesDepsRoot/openexr/lib/libIlmThread.a\" "
cmakeArgs=" $cmakeArgs -DDEPENDENCY_OPENEXR_IEX_LIBRARY=\"$cyclesDepsRoot/openexr/lib/libIex.a\" "

cmakeArgs=" $cmakeArgs -DDEPENDENCY_JPEG_LIBRARY=\"$cyclesDepsRoot/jpeg/lib/libjpeg.a\" "
cmakeArgs=" $cmakeArgs -DDEPENDENCY_TIFF_LIBRARY=\"$cyclesDepsRoot/tiff/lib/libtiff.a\" "

additionalBuildTargets=" $additionalBuildTargets UniRender_cycles "
