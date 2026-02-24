include(${CMAKE_SOURCE_DIR}/cmake/pr_common.cmake)

pr_fetch_external_lib("util_ocio"               "https://github.com/Silverlan/util_ocio"             "2ba870769ae06e2da95e1bda540f9304d93384cc")
pr_fetch_external_lib("util_raytracing"         "https://github.com/Silverlan/UniRender"             "67c63c2c5ceca84d273a895d7bf11d43d0a23e27")
pr_fetch_third_party_repository("render_raytracing"         "https://github.com/Silverlan/render_raytracing"     "7e9212d1fdde5a97f743ef7132ae495b67e279ee" "tools/render_raytracing")
include("cmake/fetch_prebuilt_binaries.cmake")

set(PR_UNIRENDER_ENABLE_DEPENDENCIES 1 CACHE BOOL "Enable dependencies?")
set(PR_UNIRENDER_WITH_CYCLES 1 CACHE BOOL "Enable Cycles renderer?")
set(PR_UNIRENDER_WITH_LUXCORERENDER 0 CACHE BOOL "Enable LuxCoreRender renderer?")

set(PROJ_NAME pr_unirender)
pr_add_module(${PROJ_NAME} "modules/unirender/")

pr_add_dependency(${PROJ_NAME} client TARGET PUBLIC)

pr_init_module(${PROJ_NAME})

pr_add_compile_definitions(${PROJ_NAME} -DOIIO_STATIC_BUILD -DRT_ENABLE_SUBDIVISION -DENABLE_TEST_AMBIENT_OCCLUSION)

if(UNIX)
	# HACK / FIX: The cycles library uses OpenImageDenoise with CUDA for denoising.
	# For some unknown reason, this causes a segfault when OpenImageDenoise calls cuInit().
	# If we call any OpenImageDenoise function that initializes the CUDA context
	# *before* loading the cycles module, this crash does not happen.
	# To do so, we create a dependency to OpenImageDenoise and call oidnIsCUDADeviceSupported.
	# (but any other oidn CUDA function would do as well.)
	# This problem only occurs on Linux.
	list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/external_libs/cycles/cmake/modules")
	pr_add_third_party_dependency(${PROJ_NAME} openimagedenoise)
endif()

if(${PR_UNIRENDER_ENABLE_DEPENDENCIES})
	pr_add_dependency(${PROJ_NAME} util_raytracing TARGET PUBLIC)
	if(PR_UNIRENDER_WITH_CYCLES)
		set(ROLLBACK_CYCLES_4_1_1 ON CACHE BOOL "Enable Rollback to Cycles 4.1.1")
		add_subdirectory(external_libs/cycles)
		set_target_properties(UniRender_cycles PROPERTIES FOLDER modules/offline_render/unirender)
		#pr_add_dependency(${PROJ_NAME} UniRender_cycles TARGET PUBLIC)
		add_dependencies(${PROJ_NAME} UniRender_cycles)
		add_dependencies(${PROJ_NAME} render_raytracing)
	endif()
	if(PR_UNIRENDER_WITH_LUXCORERENDER)
		add_subdirectory(external_libs/luxcorerender)
		set_target_properties(UniRender_LuxCoreRender PROPERTIES FOLDER modules/offline_render/unirender)
	endif()
endif()

pr_finalize(${PROJ_NAME} FOLDER modules/offline_render/unirender)
