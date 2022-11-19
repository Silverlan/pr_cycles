
# vcpkg
cd "$deps"
$env:VCPKG_DEFAULT_TRIPLET = 'x64-windows'
$vcpkgRoot="$deps/vcpkg"
if(![System.IO.Directory]::Exists($vcpkgRoot)){
	print_hmsg "vcpkg not found, downloading..."
	git clone https://github.com/Microsoft/vcpkg.git
	validate_result
	print_hmsg "Done!"
}
./vcpkg/bootstrap-vcpkg.bat -disableMetrics
validate_result

# oiio
print_hmsg "Building openimageio..."
./vcpkg/vcpkg install openimageio
validate_result
print_hmsg "Done!"

$global:cmakeArgs += " -DDEPENDENCY_OPENIMAGEIO_INCLUDE=`"$deps/vcpkg/installed/x64-windows/include`" -DDEPENDENCY_OPENIMAGEIO_LIBRARY=`"$deps/vcpkg/installed/x64-windows/lib/OpenImageIO.lib`" "
