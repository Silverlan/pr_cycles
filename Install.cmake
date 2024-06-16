set(INSTALL_PATH "modules/unirender")
set(DEPENDENCY_GLEW_LIBRARY_BIN "./" CACHE STRING "")
pr_install_create_directory("${INSTALL_PATH}")
pr_install_targets(pr_unirender INSTALL_DIR "${INSTALL_PATH}")
pr_install_files(
    "${DEPENDENCY_OPENIMAGEDENOISE_LIBRARY}"
    "${DEPENDENCY_TBB_LIBRARY}"
    INSTALL_DIR "${INSTALL_PATH}"
)

# Install util_raytracing
pr_install_targets(util_raytracing)

# Install OpenColorIO
if(WIN32)
    pr_install_files("${DEPENDENCY_OPENCOLORIO_LIBRARY}/OpenColorIO_2_4.dll")
else()
    pr_install_files("${DEPENDENCY_OPENCOLORIO_LIBRARY}/libOpenColorIO.so")
endif()

if(NOT ${DEPENDENCY_GLEW_LIBRARY_BIN} STREQUAL "")
    if(WIN32)
        pr_install_files("${DEPENDENCY_GLEW_LIBRARY_BIN}/glew32.dll" INSTALL_DIR "${INSTALL_PATH}")
    else()
        pr_install_files("${DEPENDENCY_GLEW_LIBRARY_BIN}/libglew32.so" INSTALL_DIR "${INSTALL_PATH}")
    endif()
endif()
if(WIN32)
    pr_install_files("${DEPENDENCY_TBB_LIBRARY}/tbb.dll" INSTALL_DIR "${INSTALL_PATH}")
else()
    pr_install_files("${DEPENDENCY_TBB_LIBRARY}/libtbb.so" INSTALL_DIR "${INSTALL_PATH}")
endif()

# assets
pr_install_directory("${CMAKE_CURRENT_LIST_DIR}/assets/" INSTALL_DIR "modules/")

# render_raytracing tool
pr_install_targets(render_raytracing render_raytracing_lib)

# Cycles
if(PR_UNIRENDER_WITH_CYCLES)
    set(CYCLES_INSTALL_DIR "${CYCLES_INSTALL_DIR}/cycles")
    pr_install_targets(UniRender_cycles INSTALL_DIR "${CYCLES_INSTALL_DIR}")
    if(DEFINED DEPENDENCY_GLEW_LIBRARY_BIN AND NOT "${DEPENDENCY_CYCLES_BUILD_LOCATION}" STREQUAL "")
        pr_install_directory("${DEPENDENCY_CYCLES_BUILD_LOCATION}/src/kernel/" INSTALL_DIR "${CYCLES_INSTALL_DIR}/lib" FILES_MATCHING PATTERN "*.ptx" PATTERN "*.cubin" PATTERN "CMakeFiles" EXCLUDE PATTERN "cycles_kernel.dir" EXCLUDE PATTERN "osl" EXCLUDE PATTERN "x64" EXCLUDE)
    endif()
    if(DEFINED DEPENDENCY_GLEW_LIBRARY_BIN AND NOT "${DEPENDENCY_CYCLES_ROOT}" STREQUAL "")
        pr_install_directory("${DEPENDENCY_CYCLES_ROOT}/src/kernel" INSTALL_DIR "${CYCLES_INSTALL_DIR}/source")
        pr_install_directory("${DEPENDENCY_CYCLES_ROOT}/src/util" INSTALL_DIR "${CYCLES_INSTALL_DIR}/source")
    endif()

    if(WIN32)
        pr_install_files("${DEPENDENCY_CYCLES_GLOG_LIBRARY}/glog.dll" INSTALL_DIR "${CYCLES_INSTALL_DIR}")
        pr_install_files("${DEPENDENCY_CYCLES_OPENVDB_LIBRARY_PATH}/../bin/openvdb.dll" INSTALL_DIR "${CYCLES_INSTALL_DIR}")
    else()
        pr_install_files("${DEPENDENCY_CYCLES_GLOG_LIBRARY}/libglog.so" INSTALL_DIR "${CYCLES_INSTALL_DIR}")
        pr_install_files("${DEPENDENCY_CYCLES_OPENVDB_LIBRARY_PATH}/../bin/libopenvdb.so" INSTALL_DIR "${CYCLES_INSTALL_DIR}")
    endif()
endif()
