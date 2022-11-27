
cd $deps

# ISPC
$ispcRoot="$deps/ispc-v1.17.0-windows"
if(![System.IO.Directory]::Exists("$ispcRoot")){
    print_hmsg "ISPC not found. Downloading..."
    Invoke-WebRequest "https://github.com/ispc/ispc/releases/download/v1.17.0/ispc-v1.17.0-windows.zip" -OutFile "ispc-v1.17.0-windows.zip"
    validate_result

    # Extract ISPC
    print_hmsg "Extracting ISPC..."
    Add-Type -Assembly "System.IO.Compression.Filesystem"
    [System.IO.Compression.ZipFile]::ExtractToDirectory("$PWD/ispc-v1.17.0-windows.zip", "$PWD")
    validate_result

    rm "ispc-v1.17.0-windows.zip"
    print_hmsg "Done!"
}

# TBB
$oneTBBRoot="$deps/tbb2019_20190605oss"
if(![System.IO.Directory]::Exists("$oneTBBRoot")){
    print_hmsg "oneTBB not found. Downloading..."
    Invoke-WebRequest "https://github.com/oneapi-src/oneTBB/releases/download/2019_U8/tbb2019_20190605oss_win.zip" -OutFile "tbb2019_20190605oss_win.zip"
    validate_result

    # Extract oneTBB
    print_hmsg "Extracting oneTBB..."
    Add-Type -Assembly "System.IO.Compression.Filesystem"
    [System.IO.Compression.ZipFile]::ExtractToDirectory("$PWD/tbb2019_20190605oss_win.zip", "$PWD")
    validate_result

    rm tbb2019_20190605oss_win.zip
    print_hmsg "Done!"
}

$global:cmakeArgs+=" -DDEPENDENCY_TBB_LIBRARY=`"$oneTBBRoot/lib/intel64/vc14/tbb.lib`" "

# OIDN
cd "$deps"
$oidnRoot="$deps/oidn"
if(![System.IO.Directory]::Exists($oidnRoot)){
    print_hmsg "oidn not found. Downloading..."
    git clone "https://github.com/OpenImageDenoise/oidn.git" --recurse-submodules
    validate_result
    print_hmsg "Done!"
}

cd oidn
git reset --hard d959bac5b7130b31c41095811ddfbe58c4cf03f4
validate_result

print_hmsg "Build oidn"
[System.IO.Directory]::CreateDirectory("$PWD/build")
cd build

cmake .. -G "$generator" -DTBB_ROOT="$oneTBBRoot" -DTBB_INCLUDE_DIR="$oneTBBRoot/include"
validate_result
cmake --build "." --config "$buildConfig"
validate_result

$global:cmakeArgs+=" -DDEPENDENCY_OPENIMAGEDENOISE_INCLUDE=`"$oidnRoot/include`" -DDEPENDENCY_OPENIMAGEDENOISE_LIBRARY=`"$oidnRoot/build/$buildConfig/OpenImageDenoise.lib`" "

# OCIO
cd "$deps"
$ocioRoot="$deps/OpenColorIO"
if(![System.IO.Directory]::Exists($ocioRoot)){
    print_hmsg "ocio not found. Downloading..."
    git clone "https://github.com/AcademySoftwareFoundation/OpenColorIO.git" --recurse-submodules
    validate_result
    print_hmsg "Done!"
}

cd OpenColorIO
git reset --hard fb9390ece9504ae30aa0213d8fb8f44a5550554f
validate_result

print_hmsg "Build ocio"
[System.IO.Directory]::CreateDirectory("$PWD/build")
cd build

cmake .. -G "$generator"
validate_result
cmake --build "." --config "$buildConfig"
validate_result

cp "$ocioRoot/build/include/OpenColorIO/OpenColorABI.h" "$ocioRoot/include/OpenColorIO/"
validate_result

$global:cmakeArgs+=" -DDEPENDENCY_OPENCOLORIO_INCLUDE=`"$ocioRoot/include`" -DDEPENDENCY_OPENCOLORIO_LIBRARY=`"$ocioRoot/build/src/OpenColorIO/$buildConfig/OpenColorIO.lib`" "

# OIIO
& "$PSScriptRoot/windows/build_oiio.ps1"
cd "$deps"

# cycles
print_hmsg "Running cycles build script..."
& "$PSScriptRoot/windows/build_cycles.ps1"
validate_result
cd "$deps"
print_hmsg "Done!"

# OpenSubdiv
cd "$deps"
$subdivRoot="$deps/opensubdiv"
if(![System.IO.Directory]::Exists($subdivRoot)){
    print_hmsg "OpenSubdiv not found. Downloading..."
    git clone "https://github.com/PixarAnimationStudios/OpenSubdiv.git" --recurse-submodules
    validate_result
    print_hmsg "Done!"
}

cd OpenSubdiv
git reset --hard 82ab1b9f54c87fdd7e989a3470d53e137b8ca270
validate_result

print_hmsg "Build OpenSubdiv"
[System.IO.Directory]::CreateDirectory("$PWD/build")
cd build

cmake .. -G "$generator" -DTBB_ROOT="$oneTBBRoot" -DTBB_INCLUDE_DIR="$oneTBBRoot/include" `
    -D NO_PTEX=1 -D NO_DOC=1 `
    -D NO_OMP=1 -D NO_TBB=1 -D NO_CUDA=1 -D NO_OPENCL=1 -D NO_CLEW=1 -D NO_EXAMPLES=1
validate_result
cmake --build "." --config "$buildConfig"
validate_result

$global:cmakeArgs+=" -DDEPENDENCY_OPENSUBDIV_INCLUDE=`"$subdivRoot`" -DDEPENDENCY_OPENSUBDIV_LIBRARY=`"$subdivRoot/build/lib/$buildConfig/osdGPU.lib`" -DDEPENDENCY_OPENSUBDIV_CPU_LIBRARY=`"$subdivRoot/build/lib/$buildConfig/osdCPU.lib`" "

# util_ocio
$utilOcioRoot="$root/external_libs/util_ocio"
if(![System.IO.Directory]::Exists($utilOcioRoot)){
    print_hmsg "util_ocio not found. Downloading..."
    cd "$root/external_libs"
    git clone "https://github.com/Silverlan/util_ocio.git" --recurse-submodules util_ocio
    validate_result
    print_hmsg "Done!"
    cd ..
}

$global:cmakeArgs+=" -DDEPENDENCY_UTIL_OCIO_INCLUDE=`"$utilOcioRoot/include`" "

# glog
cd "$deps"
$glogRoot="$deps/glog"
if(![System.IO.Directory]::Exists($glogRoot)){
    print_hmsg "glog not found. Downloading..."
    git clone "https://github.com/google/glog" --recurse-submodules
    validate_result
    print_hmsg "Done!"
}

cd glog
git reset --hard b33e3ba
validate_result

print_hmsg "Build ocio"
[System.IO.Directory]::CreateDirectory("$PWD/build")
cd build

cmake .. -G "$generator"
validate_result
cmake --build "." --config "$buildConfig"
validate_result

cp "$glogRoot/src/glog/log_severity.h" "$glogRoot/build/glog/"
cp "$glogRoot/src/glog/platform.h" "$glogRoot/build/glog/"

$global:cmakeArgs+=" -DDEPENDENCY_CYCLES_GLOG_INCLUDE=`"$glogRoot/build`" -DDEPENDENCY_CYCLES_GLOG_LIBRARY=`"$glogRoot/build/$buildConfig/glog.lib`" "

# gflags
cd "$deps"
$gflagsRoot="$deps/gflags"
if(![System.IO.Directory]::Exists($gflagsRoot)){
    print_hmsg "gflags not found. Downloading..."
    git clone "https://github.com/gflags/gflags.git" --recurse-submodules
    validate_result
    print_hmsg "Done!"
}

cd gflags
git reset --hard e171aa2
validate_result

print_hmsg "Build gflags"
[System.IO.Directory]::CreateDirectory("$PWD/build_files")
cd build_files

cmake .. -G "$generator"
validate_result
cmake --build "." --config "$buildConfig"
validate_result

$global:cmakeArgs+=" -DDEPENDENCY_GFLAGS_INCLUDE=`"$gflagsRoot/build_files/include`" -DDEPENDENCY_GFLAGS_LIBRARY=`"$gflagsRoot/build_files/lib/$buildConfig/gflags_static.lib`" "

# render_raytracing tool
cd "$tools"
$rrToolRoot="$tools/render_raytracing"
if(![System.IO.Directory]::Exists($rrToolRoot)){
    print_hmsg "render_raytracing tool not found. Downloading..."
    git clone "https://github.com/Silverlan/render_raytracing.git" --recurse-submodules
    validate_result
    print_hmsg "Done!"
}
else{
    print_hmsg "Updating 'render_raytracing' tool..."
    git pull
    validate_result
    print_hmsg "Done!"
}

$global:additionalBuildTargets+=" render_raytracing "

# Unirender
$unirenderRoot="$root/external_libs/util_raytracing"
if(![System.IO.Directory]::Exists($unirenderRoot)){
    print_hmsg "Unirender not found. Downloading..."
    cd "$root/external_libs"
    git clone "https://github.com/Silverlan/UniRender.git" --recurse-submodules util_raytracing
    validate_result
    print_hmsg "Done!"
    cd ..
}

$global:cmakeArgs+=" -DDEPENDENCY_UTIL_RAYTRACING_INCLUDE=`"$unirenderRoot/include`" "
