set(INSTALL_PATH "modules/unirender")
# set(DEPENDENCY_GLEW_LIBRARY_BIN "./" CACHE STRING "")
pr_install_create_directory("${INSTALL_PATH}")
pr_install_targets(pr_unirender INSTALL_DIR "${INSTALL_PATH}")

if(WIN32)
    set(BIN_DIR "bin/")
else()
    set(BIN_DIR "lib/")
endif()

set(CYCLES_INSTALL_DIR "${INSTALL_PATH}/cycles")

# Install util_raytracing
pr_install_targets(util_raytracing)

# assets
pr_install_directory("${CMAKE_CURRENT_LIST_DIR}/assets/" INSTALL_DIR "modules")

# Cycles
if(PR_UNIRENDER_WITH_CYCLES)
    pr_install_component("UniRender_cycles")

    set(INSTALL_PATH_CYCLES "${INSTALL_PATH}/cycles")
    set(cycles_deps_install_dir "${PRAGMA_DEPS_DIR}/cycles")
    if(UNIX)
        pr_install_directory("${cycles_deps_install_dir}/lib/" INSTALL_DIR "${INSTALL_PATH_CYCLES}" PATTERN "*" PATTERN "*.a" EXCLUDE)
    else()
        pr_install_directory("${cycles_deps_install_dir}/bin/" INSTALL_DIR "${INSTALL_PATH_CYCLES}" PATTERN "*")
        pr_install_directory("${cycles_deps_install_dir}/lib/" INSTALL_DIR "${INSTALL_PATH_CYCLES}" PATTERN "*.zst")

        list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/external_libs/cycles/cmake/modules")
        pr_install_binaries(glog INSTALL_DIR "${INSTALL_PATH_CYCLES}")
    endif()
    pr_install_directory("${cycles_deps_install_dir}/source/" INSTALL_DIR "${INSTALL_PATH_CYCLES}/source")

	# render_raytracing tool
	pr_install_targets(render_raytracing render_raytracing_lib)
    
    pr_install_targets(UniRender_cycles INSTALL_DIR "${CYCLES_INSTALL_DIR}")

    pr_install_create_directory("${CYCLES_INSTALL_DIR}/cache/kernels")
endif()
