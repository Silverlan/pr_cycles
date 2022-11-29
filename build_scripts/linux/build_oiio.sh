
# vcpkg
cd "$deps"
vcpkgRoot="$deps/vcpkg"
if [ ! -d "$vcpkgRoot" ]; then
	print_hmsg "vcpkg not found, downloading..."
	git clone https://github.com/Microsoft/vcpkg.git
	validate_result
	print_hmsg "Done!"
fi

cd vcpkg
git reset --hard 7d9775a3c3ffef3cbad688d7271a06803d3a2f51
validate_result
cd ..

bash ./vcpkg/bootstrap-vcpkg.sh -disableMetrics
validate_result

# oiio
print_hmsg "Building openimageio..."
./vcpkg/vcpkg install openimageio
validate_result
print_hmsg "Done!"

cmakeArgs=" $cmakeArgs -DDEPENDENCY_OPENIMAGEIO_INCLUDE=\"$deps/vcpkg/installed/x64-linux/include\" -DDEPENDENCY_OPENIMAGEIO_LIBRARY=\"$deps/vcpkg/installed/x64-linux/lib/libOpenImageIO.a\" "
