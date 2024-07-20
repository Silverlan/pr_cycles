set(INSTALL_PATH "modules/unirender")
set(DEPENDENCY_GLEW_LIBRARY_BIN "./" CACHE STRING "")
pr_install_create_directory("${INSTALL_PATH}")
pr_install_targets(pr_unirender INSTALL_DIR "${INSTALL_PATH}")
pr_install_binary(openimagedenoise WIN "OpenImageDenoise.dll" LIN "libOpenImageDenoise.so" INSTALL_DIR "${INSTALL_PATH}")
pr_install_binary(tbb WIN "tbb.dll" LIN "libtbb.so" INSTALL_DIR "${INSTALL_PATH}")

# Install util_raytracing
pr_install_targets(util_raytracing)

# Install OpenColorIO
pr_install_binary(opencolorio WIN "OpenColorIO_2_4.dll" LIN "libOpenColorIO.so")

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
    pr_install_targets(png INSTALL_DIR "${CYCLES_INSTALL_DIR}")
    if(NOT "${DEPENDENCY_CYCLES_BUILD_LOCATION}" STREQUAL "")
        pr_install_directory("${DEPENDENCY_CYCLES_BUILD_LOCATION}/src/kernel/" INSTALL_DIR "${CYCLES_INSTALL_DIR}/lib" FILES_MATCHING PATTERN "*.ptx" PATTERN "*.cubin" PATTERN "CMakeFiles" EXCLUDE PATTERN "cycles_kernel.dir" EXCLUDE PATTERN "osl" EXCLUDE PATTERN "x64" EXCLUDE)
    endif()
    if(NOT "${DEPENDENCY_CYCLES_ROOT}" STREQUAL "")
        pr_install_directory("${DEPENDENCY_CYCLES_ROOT}/src/kernel" INSTALL_DIR "${CYCLES_INSTALL_DIR}/source")
        pr_install_directory("${DEPENDENCY_CYCLES_ROOT}/src/util" INSTALL_DIR "${CYCLES_INSTALL_DIR}/source")
    endif()

    pr_install_binary(cycles_glog WIN "glog.dll" LIN "libglog.so" INSTALL_DIR "${CYCLES_INSTALL_DIR}")
    pr_install_binary(BIN_DIR "${DEPENDENCY_CYCLES_OPENVDB_LIBRARY_PATH}/../bin/" WIN "openvdb.dll" LIN "libopenvdb.so" INSTALL_DIR "${CYCLES_INSTALL_DIR}")
endif()
