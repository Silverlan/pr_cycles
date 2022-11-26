
# cycles
cd "$deps"
$cyclesRoot="$deps/cycles"
if(![System.IO.Directory]::Exists("$cyclesRoot")){
	print_hmsg "cycles not found. Downloading..."
	git clone git://git.blender.org/cycles.git --recurse-submodules
	validate_result
}
cd cycles

$lastBuildCommit = $null
if([System.IO.File]::Exists("$cyclesRoot/lastbuildsha")){
    $lastBuildCommit = Get-Content -Path "$cyclesRoot/lastbuildsha"
}

$targetCommit="b1882be6b1f2e27725ee672d87c5b6f8d6113eb1"
if($lastBuildCommit -ne $targetCommit)
{
    print_hmsg "Downloading cycles dependencies..."
    cmd.exe /C "make.bat update"
    validate_result

    # The update commit above will unfortunately update Cycles to the last commit. This behavior
    # can't be disabled, so we have to reset the commit back to the one we want here.
    # This can cause issues if the Cycles update-script updates the dependencies to newer versions
    # that aren't compatible with the commit we're using, but it can't be helped.
    git reset --hard "$targetCommit"
    validate_result
    print_hmsg "Done!"
}

# We need to add the --allow-unsupported-compiler flag to a cycles CMake configuration file manually,
# otherwise compilation will fail for newer versions of Visual Studio.
$kernelCmakePath="$cyclesRoot/src/kernel/CMakeLists.txt"
$unsupportedCompilerFlag = Select-String -Path "$kernelCmakePath" -Pattern '--allow-unsupported-compiler'
if ($unsupportedCompilerFlag -eq $null) {
	# Only set the flag if it hasn't been set yet
	(Get-Content "$kernelCmakePath").replace('${CUDA_NVCC_FLAGS}', '${CUDA_NVCC_FLAGS} --allow-unsupported-compiler') | Set-Content "$kernelCmakePath"
}

print_hmsg "Build cycles"
[System.IO.Directory]::CreateDirectory("$PWD/build")
cd build

# Building cycles rebuilds shaders every time, which can take a very long time.
# Since there's usually no reason to rebuild the shaders, we'll only build if the head commit has
# changed since the last build.
$curCommitId = git rev-parse HEAD
validate_result
if ($lastBuildCommit -ne $curCommitId) {
	cmake .. -G "$generator" -DWITH_CYCLES_CUDA_BINARIES=ON -DWITH_CYCLES_DEVICE_OPTIX=ON -DWITH_CYCLES_DEVICE_CUDA=ON `
		-DZLIB_INCLUDE_DIR="$zlibRoot" -DZLIB_LIBRARY="$zlibRoot/build/$buildConfig/zlibstatic.lib"
	validate_result
	cmake --build "." --config "$buildConfig"
	validate_result

	$curCommitId | Set-Content "$cyclesRoot/lastbuildsha"
}
else
{
	print_hmsg "Head commit has not changed, skipping build..."
}

print_hmsg "Done!"

$cyclesDepsRoot="$deps/lib/win64_vc15"
$global:cmakeArgs += " -DDEPENDENCY_CYCLES_INCLUDE=`"$deps/cycles/src`" "
$global:cmakeArgs += " -DDEPENDENCY_CYCLES_ROOT=`"$deps/cycles`" "
$global:cmakeArgs += " -DDEPENDENCY_CYCLES_BUILD_LOCATION=`"$deps/cycles/build`" "
$global:cmakeArgs += " -DDEPENDENCY_CYCLES_ATOMIC_INCLUDE=`"$deps/cycles\third_party\atomic`" "
$global:cmakeArgs += " -DDEPENDENCY_CYCLES_OPENIMAGEIO_INCLUDE=`"$cyclesDepsRoot/OpenImageIO/include`" "
$global:cmakeArgs += " -DDEPENDENCY_CYCLES_PUGIXML_INCLUDE=`"$cyclesDepsRoot/pugixml/include`" "
$global:cmakeArgs += " -DDEPENDENCY_CYCLES_OPENIMAGEDENOISE_INCLUDE=`"$cyclesDepsRoot/openimagedenoise/include`" "
$global:cmakeArgs += " -DDEPENDENCY_CYCLES_OPENEXR_INCLUDE=`"$cyclesDepsRoot/openexr/include`" "
$global:cmakeArgs += " -DDEPENDENCY_CYCLES_EMBREE_INCLUDE=`"$cyclesDepsRoot/embree/include`" "
$global:cmakeArgs += " -DDEPENDENCY_CYCLES_OSL_INCLUDE=`"$cyclesDepsRoot/osl/include`" "
$global:cmakeArgs += " -DDEPENDENCY_CYCLES_TBB_INCLUDE=`"$cyclesDepsRoot/tbb/include`" "
$global:cmakeArgs += " -DDEPENDENCY_CYCLES_TBB_LIBRARY=`"$cyclesDepsRoot/tbb/lib/tbb.lib`" "
$global:cmakeArgs += " -DDEPENDENCY_CYCLES_OPENVDB_LIBRARY_PATH=`"$cyclesDepsRoot/openvdb/lib`" "

$global:cmakeArgs += " -DDEPENDENCY_CYCLES_DEPENDENCIES_LOCATION=`"$cyclesDepsRoot`" "
$global:cmakeArgs += " -DDEPENDENCY_CYCLES_LIBRARY_LOCATION=`"$cyclesRoot/build/lib/$buildConfig`" "

$global:cmakeArgs += " -DDEPENDENCY_OPENEXR_INCLUDE=`"$cyclesDepsRoot/openexr/include`" "
$global:cmakeArgs += " -DDEPENDENCY_OPENEXR_UTIL_LIBRARY=`"$cyclesDepsRoot/openexr/lib/OpenEXRUtil_s.lib`" "
$global:cmakeArgs += " -DDEPENDENCY_OPENEXR_IMATH_INCLUDE=`"$cyclesDepsRoot/imath/include`" "
$global:cmakeArgs += " -DDEPENDENCY_OPENEXR_IMATH_LIBRARY=`"$cyclesDepsRoot/imath/lib/Imath_s.lib`" "
$global:cmakeArgs += " -DDEPENDENCY_OPENEXR_ILMTHREAD_LIBRARY=`"$cyclesDepsRoot/openexr/lib/IlmThread_s.lib`" "
$global:cmakeArgs += " -DDEPENDENCY_OPENEXR_IEX_LIBRARY=`"$cyclesDepsRoot/openexr/lib/Iex_s.lib`" "

$global:cmakeArgs += " -DDEPENDENCY_JPEG_LIBRARY=`"$cyclesDepsRoot/jpeg/lib/libjpeg.lib`" "
$global:cmakeArgs += " -DDEPENDENCY_TIFF_LIBRARY=`"$cyclesDepsRoot/tiff/lib/libtiff.lib`" "

Exit 0
