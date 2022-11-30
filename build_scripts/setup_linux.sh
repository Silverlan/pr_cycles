SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

cd $deps

# ISPC
ispcRoot="$deps/ispc-v1.17.0-linux"
if [ ! -d "$ispcRoot" ]; then
	print_hmsg "ISPC not found. Downloading..."
    wget "https://github.com/ispc/ispc/releases/download/v1.17.0/ispc-v1.17.0-linux.tar.gz"
    validate_result

	# Extract ISPC
	print_hmsg "Extracting ISPC..."
    tar -zxvf "ispc-v1.17.0-linux.tar.gz"
    validate_result

	rm "ispc-v1.17.0-linux.tar.gz"
	print_hmsg "Done!"
fi

# TBB
oneTBBRoot="$deps/tbb2019_20190605oss"
if [ ! -d "$oneTBBRoot" ]; then
	print_hmsg "oneTBB not found. Downloading..."
	wget "https://github.com/oneapi-src/oneTBB/releases/download/2019_U8/tbb2019_20190605oss_lin.tgz"
	validate_result

	# Extract oneTBB
	print_hmsg "Extracting oneTBB..."

	tar -zxvf "$PWD/tbb2019_20190605oss_lin.tgz"
	validate_result

	rm "tbb2019_20190605oss_lin.tgz"
	print_hmsg "Done!"
fi

cmakeArgs=" $cmakeArgs -DDEPENDENCY_TBB_LIBRARY=\"$oneTBBRoot/lib/intel64/gcc4.7/libtbb.so.2\" "

# OIDN
cd "$deps"
oidnRoot="$deps/oidn"
if [ ! -d "$oidnRoot" ]; then
	print_hmsg "oidn not found. Downloading..."
	git clone "https://github.com/OpenImageDenoise/oidn.git" --recurse-submodules
	validate_result
	print_hmsg "Done!"
fi

cd oidn
git reset --hard d959bac5b7130b31c41095811ddfbe58c4cf03f4
validate_result

print_hmsg "Build oidn"
mkdir -p "$PWD/build"
cd build

cmake .. -G "$generator" -DTBB_ROOT="$oneTBBRoot" -DTBB_INCLUDE_DIR="$oneTBBRoot/include"
validate_result
cmake --build "." --config "$buildConfig"
validate_result

cmakeArgs=" $cmakeArgs -DDEPENDENCY_OPENIMAGEDENOISE_INCLUDE=\"$oidnRoot/include\" -DDEPENDENCY_OPENIMAGEDENOISE_LIBRARY=\"$oidnRoot/build/libOpenImageDenoise.so\" "

# OCIO
cd "$deps"
ocioRoot="$deps/OpenColorIO"
if [ ! -d "$ocioRoot" ]; then
	print_hmsg "ocio not found. Downloading..."
	git clone "https://github.com/AcademySoftwareFoundation/OpenColorIO.git" --recurse-submodules
	validate_result
	print_hmsg "Done!"
fi

cd OpenColorIO
git reset --hard fb9390ece9504ae30aa0213d8fb8f44a5550554f
validate_result

print_hmsg "Build ocio"
mkdir -p "$PWD/build"
cd build

cmake .. -G "$generator" -DOCIO_BUILD_PYTHON=OFF
validate_result
cmake --build "." --config "$buildConfig"
validate_result

cp $ocioRoot/build/include/OpenColorIO/OpenColorABI.h $ocioRoot/include/OpenColorIO/
validate_result

cmakeArgs=" $cmakeArgs -DDEPENDENCY_OPENCOLORIO_INCLUDE=\"$ocioRoot/include\" -DDEPENDENCY_OPENCOLORIO_LIBRARY=\"$ocioRoot/build/src/OpenColorIO/libOpenColorIO.so\" "

# OIIO
source "$SCRIPT_DIR/linux/build_oiio.sh"
validate_result
cd "$deps"

# cycles
print_hmsg "Running cycles build script..."
source "$SCRIPT_DIR/linux/build_cycles.sh"
validate_result
cd "$deps"
print_hmsg "Done!"

# OpenSubdiv
cd "$deps"
subdivRoot="$deps/OpenSubdiv"
if [ ! -d "$subdivRoot" ]; then
	print_hmsg "OpenSubdiv not found. Downloading..."
	git clone "https://github.com/PixarAnimationStudios/OpenSubdiv.git" --recurse-submodules
	validate_result
	print_hmsg "Done!"
fi

cd OpenSubdiv
git reset --hard 82ab1b9f54c87fdd7e989a3470d53e137b8ca270
validate_result

print_hmsg "Build OpenSubdiv"
mkdir -p "$PWD/build"
cd build

cmake .. -G "$generator" -DTBB_ROOT="$oneTBBRoot" -DTBB_INCLUDE_DIR="$oneTBBRoot/include" \
    -D NO_PTEX=1 -D NO_DOC=1 \
    -D NO_OMP=1 -D NO_TBB=1 -D NO_CUDA=1 -D NO_OPENCL=1 -D NO_CLEW=1 -D NO_EXAMPLES=1
validate_result
cmake --build "." --config "$buildConfig"
validate_result

cmakeArgs=" $cmakeArgs -DDEPENDENCY_OPENSUBDIV_INCLUDE=\"$subdivRoot\" -DDEPENDENCY_OPENSUBDIV_LIBRARY=\"$subdivRoot/build/lib/libosdGPU.a\" -DDEPENDENCY_OPENSUBDIV_CPU_LIBRARY=\"$subdivRoot/build/lib/libosdCPU.a\" "

# util_ocio
utilOcioRoot="$root/external_libs/util_ocio"
if [ ! -d "$utilOcioRoot" ]; then
	print_hmsg "util_ocio not found. Downloading..."
	cd "$root/external_libs"
	git clone "https://github.com/Silverlan/util_ocio.git" --recurse-submodules util_ocio
	validate_result
	print_hmsg "Done!"
	cd ..
fi

cmakeArgs=" $cmakeArgs -DDEPENDENCY_UTIL_OCIO_INCLUDE=\"$utilOcioRoot/include\" "

# render_raytracing tool
cd "$tools"
rrToolRoot="$tools/render_raytracing"
if [ ! -d "$rrToolRoot" ]; then
    print_hmsg "render_raytracing tool not found. Downloading..."
    git clone "https://github.com/Silverlan/render_raytracing.git" --recurse-submodules
    validate_result
    print_hmsg "Done!"
else
    print_hmsg "Updating 'render_raytracing' tool..."
    git pull
    validate_result
    print_hmsg "Done!"
fi

additionalBuildTargets=" $additionalBuildTargets render_raytracing "

# Unirender
unirenderRoot="$root/external_libs/util_raytracing"
if [ ! -d "$unirenderRoot" ]; then
	print_hmsg "Unirender not found. Downloading..."
	cd "$root/external_libs"
	git clone "https://github.com/Silverlan/UniRender.git" --recurse-submodules util_raytracing
	validate_result
	print_hmsg "Done!"
	cd ..
fi

cmakeArgs=" $cmakeArgs -DDEPENDENCY_UTIL_RAYTRACING_INCLUDE=\"$unirenderRoot/include\" "