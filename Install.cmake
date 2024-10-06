set(INSTALL_PATH "modules/unirender")
set(DEPENDENCY_GLEW_LIBRARY_BIN "./" CACHE STRING "")
pr_install_create_directory("${INSTALL_PATH}")
pr_install_targets(pr_unirender INSTALL_DIR "${INSTALL_PATH}")

if(WIN32)
    set(BIN_DIR "bin/")
else()
    set(BIN_DIR "lib/")
endif()

# Install util_raytracing
pr_install_targets(util_raytracing)
pr_install_binary(opencolorio WIN "OpenColorIO_2_3.dll" LIN "libOpenColorIO.so" BIN_DIR "${UNIRENDER_PREBUILT_BINARY_LOCATION}/opencolorio/${BIN_DIR}")

# Required by OpenColorIO
pr_install_binary(imath WIN "imath.dll" LIN "libImath.so" INSTALL_DIR "${CYCLES_INSTALL_DIR}" BIN_DIR "${UNIRENDER_PREBUILT_BINARY_LOCATION}/imath/${BIN_DIR}")

pr_install_binary(openimagedenoise WIN "OpenImageDenoise.dll" LIN "libOpenImageDenoise.so" BIN_DIR "${UNIRENDER_PREBUILT_BINARY_LOCATION}/openimagedenoise/${BIN_DIR}")
pr_install_binary(openimagedenoise_core WIN "OpenImageDenoise_core.dll" LIN "libOpenImageDenoise_core.so.2.3.0" BIN_DIR "${UNIRENDER_PREBUILT_BINARY_LOCATION}/openimagedenoise/${BIN_DIR}")
pr_install_binary(openimagedenoise_device_cpu WIN "OpenImageDenoise_device_cpu.dll" LIN "liOpenImageDenoise_device_cpu.so.2.3.0" BIN_DIR "${UNIRENDER_PREBUILT_BINARY_LOCATION}/openimagedenoise/${BIN_DIR}")
pr_install_binary(openimagedenoise_device_cuda WIN "OpenImageDenoise_device_cuda.dll" LIN "liOpenImageDenoise_device_cuda.so.2.3.0" BIN_DIR "${UNIRENDER_PREBUILT_BINARY_LOCATION}/openimagedenoise/${BIN_DIR}")
pr_install_binary(openimagedenoise_device_hip WIN "OpenImageDenoise_device_hip.dll" LIN "liOpenImageDenoise_device_hip.so.2.3.0" BIN_DIR "${UNIRENDER_PREBUILT_BINARY_LOCATION}/openimagedenoise/${BIN_DIR}")
pr_install_binary(openimagedenoise_device_sycl WIN "OpenImageDenoise_device_sycl.dll" LIN "liOpenImageDenoise_device_sycl.so.2.3.0" BIN_DIR "${UNIRENDER_PREBUILT_BINARY_LOCATION}/openimagedenoise/${BIN_DIR}")

# assets
pr_install_directory("${CMAKE_CURRENT_LIST_DIR}/assets/" INSTALL_DIR "modules/")

if(PR_UNIRENDER_WITH_CYCLES)
	# render_raytracing tool
	pr_install_targets(render_raytracing render_raytracing_lib)
endif()

# Cycles
if(PR_UNIRENDER_WITH_CYCLES)
    set(CYCLES_INSTALL_DIR "${INSTALL_PATH}/cycles")
    pr_install_create_directory("${CYCLES_INSTALL_DIR}/cache/kernels")
    pr_install_targets(UniRender_cycles INSTALL_DIR "${CYCLES_INSTALL_DIR}")
    if(NOT "${DEPENDENCY_CYCLES_BUILD_LOCATION}" STREQUAL "")
        pr_install_directory("${DEPENDENCY_CYCLES_BUILD_LOCATION}/src/kernel/" INSTALL_DIR "${CYCLES_INSTALL_DIR}/lib" PATTERN "*.zst" PATTERN "CMakeFiles" EXCLUDE PATTERN "cycles_kernel.dir" EXCLUDE PATTERN "osl" EXCLUDE PATTERN "x64" EXCLUDE)
    endif()
    if(NOT "${DEPENDENCY_CYCLES_ROOT}" STREQUAL "")
        pr_install_directory("${DEPENDENCY_CYCLES_ROOT}/src/kernel" INSTALL_DIR "${CYCLES_INSTALL_DIR}/source")
        pr_install_directory("${DEPENDENCY_CYCLES_ROOT}/src/util" INSTALL_DIR "${CYCLES_INSTALL_DIR}/source")
    endif()

    pr_install_binary(cycles_glog WIN "glog.dll" LIN "libglog.so" INSTALL_DIR "${CYCLES_INSTALL_DIR}")
    pr_install_binary(BIN_DIR "${DEPENDENCY_CYCLES_OPENVDB_LIBRARY_PATH}/../bin/" WIN "openvdb.dll" LIN "libopenvdb.so" INSTALL_DIR "${CYCLES_INSTALL_DIR}")

    pr_install_binary(cycles_openimageio WIN "OpenImageIO.dll" LIN "libOpenImageIO.so" INSTALL_DIR "${CYCLES_INSTALL_DIR}" BIN_DIR "${DEPENDENCY_CYCLES_DEPENDENCIES_LOCATION}/OpenImageIO/${BIN_DIR}")
    pr_install_binary(cycles_openimageio_util WIN "OpenImageIO_Util.dll" LIN "libOpenImageIO_Util.so" INSTALL_DIR "${CYCLES_INSTALL_DIR}" BIN_DIR "${DEPENDENCY_CYCLES_DEPENDENCIES_LOCATION}/OpenImageIO/${BIN_DIR}")
    
    pr_install_binary(cycles_openimagedenoise WIN "openimagedenoise.dll" LIN "libOpenImageDenoise.so" INSTALL_DIR "${CYCLES_INSTALL_DIR}" BIN_DIR "${DEPENDENCY_CYCLES_DEPENDENCIES_LOCATION}/openimagedenoise/${BIN_DIR}")
    pr_install_binary(cycles_openimagedenoise_core WIN "OpenImageDenoise_core.dll" LIN "libOpenImageDenoise_core.so.2.3.0" INSTALL_DIR "${CYCLES_INSTALL_DIR}" BIN_DIR "${DEPENDENCY_CYCLES_DEPENDENCIES_LOCATION}/openimagedenoise/${BIN_DIR}")
    pr_install_binary(cycles_openimagedenoise_device_cpu WIN "OpenImageDenoise_device_cpu.dll" LIN "libOpenImageDenoise_device_cpu.so.2.3.0" INSTALL_DIR "${CYCLES_INSTALL_DIR}" BIN_DIR "${DEPENDENCY_CYCLES_DEPENDENCIES_LOCATION}/openimagedenoise/${BIN_DIR}")
    pr_install_binary(cycles_openimagedenoise_device_cuda WIN "OpenImageDenoise_device_cuda.dll" LIN "libOpenImageDenoise_device_cuda.so.2.3.0" INSTALL_DIR "${CYCLES_INSTALL_DIR}" BIN_DIR "${DEPENDENCY_CYCLES_DEPENDENCIES_LOCATION}/openimagedenoise/${BIN_DIR}")
    pr_install_binary(cycles_openimagedenoise_device_hip WIN "OpenImageDenoise_device_hip.dll" LIN "libOpenImageDenoise_device_hip.so.2.3.0" INSTALL_DIR "${CYCLES_INSTALL_DIR}" BIN_DIR "${DEPENDENCY_CYCLES_DEPENDENCIES_LOCATION}/openimagedenoise/${BIN_DIR}")
    pr_install_binary(cycles_openimagedenoise_device_sycl WIN "OpenImageDenoise_device_sycl.dll" LIN "libOpenImageDenoise_device_sycl.so.2.3.0" INSTALL_DIR "${CYCLES_INSTALL_DIR}" BIN_DIR "${DEPENDENCY_CYCLES_DEPENDENCIES_LOCATION}/openimagedenoise/${BIN_DIR}")

    pr_install_binary(cycles_tbb WIN "tbb.dll" LIN "libtbb.so" INSTALL_DIR "${CYCLES_INSTALL_DIR}" BIN_DIR "${DEPENDENCY_CYCLES_DEPENDENCIES_LOCATION}/tbb/${BIN_DIR}")
    pr_install_binary(cycles_embree WIN "embree4.dll" LIN "libembree4.so" INSTALL_DIR "${CYCLES_INSTALL_DIR}" BIN_DIR "${DEPENDENCY_CYCLES_DEPENDENCIES_LOCATION}/embree/${BIN_DIR}")
    pr_install_binary(cycles_opencolorio WIN "OpenColorIO_2_3.dll" LIN "libOpenColorIO.so" INSTALL_DIR "${CYCLES_INSTALL_DIR}" BIN_DIR "${DEPENDENCY_CYCLES_DEPENDENCIES_LOCATION}/opencolorio/${BIN_DIR}")
    pr_install_binary(cycles_sycl7 WIN "sycl7.dll" LIN "libsycl.so.7" INSTALL_DIR "${CYCLES_INSTALL_DIR}" BIN_DIR "${DEPENDENCY_CYCLES_DEPENDENCIES_LOCATION}/dpcpp/${BIN_DIR}")
    pr_install_binary(cycles_sycl6 WIN "sycl6.dll" LIN "libsycl.so.6" INSTALL_DIR "${CYCLES_INSTALL_DIR}" BIN_DIR "${DEPENDENCY_CYCLES_DEPENDENCIES_LOCATION}/dpcpp/${BIN_DIR}")
    pr_install_binary(cycles_openexr WIN "OpenEXR.dll" LIN "libOpenEXR.so" INSTALL_DIR "${CYCLES_INSTALL_DIR}" BIN_DIR "${DEPENDENCY_CYCLES_DEPENDENCIES_LOCATION}/openexr/${BIN_DIR}")
    pr_install_binary(cycles_openexrcore WIN "OpenEXRCore.dll" LIN "libOpenEXRCore.so" INSTALL_DIR "${CYCLES_INSTALL_DIR}" BIN_DIR "${DEPENDENCY_CYCLES_DEPENDENCIES_LOCATION}/openexr/${BIN_DIR}")
    pr_install_binary(cycles_ilmthread WIN "IlmThread.dll" LIN "libIlmThread.so" INSTALL_DIR "${CYCLES_INSTALL_DIR}" BIN_DIR "${DEPENDENCY_CYCLES_DEPENDENCIES_LOCATION}/openexr/${BIN_DIR}")
    pr_install_binary(cycles_iex WIN "Iex.dll" LIN "libIex.so" INSTALL_DIR "${CYCLES_INSTALL_DIR}" BIN_DIR "${DEPENDENCY_CYCLES_DEPENDENCIES_LOCATION}/openexr/${BIN_DIR}")
    pr_install_binary(cycles_boost_thread WIN "boost_thread-vc142-mt-x64-1_82.dll" LIN "libboost_thread.so" INSTALL_DIR "${CYCLES_INSTALL_DIR}" BIN_DIR "${DEPENDENCY_CYCLES_DEPENDENCIES_LOCATION}/boost/lib/")

    if(WIN32)
        pr_install_binary(cycles_proxy_loader WIN "pi_win_proxy_loader.dll" INSTALL_DIR "${CYCLES_INSTALL_DIR}" BIN_DIR "${DEPENDENCY_CYCLES_DEPENDENCIES_LOCATION}/dpcpp/${BIN_DIR}")
    endif()
endif()
