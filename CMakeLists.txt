include(${CMAKE_SOURCE_DIR}/cmake/pr_common.cmake)

set(PR_UNIRENDER_ENABLE_DEPENDENCIES 1 CACHE BOOL "Enable dependencies?")
set(PR_UNIRENDER_WITH_CYCLES 0 CACHE BOOL "Enable Cycles renderer?")
set(PR_UNIRENDER_WITH_LUXCORERENDER 0 CACHE BOOL "Enable LuxCoreRender renderer?")

set(PROJ_NAME pr_unirender)
pr_add_library(${PROJ_NAME} SHARED)

pr_add_dependency(${PROJ_NAME} client TARGET PUBLIC)

pr_init_module(${PROJ_NAME})

pr_add_compile_definitions(${PROJ_NAME} -DOIIO_STATIC_BUILD -DRT_ENABLE_SUBDIVISION)

if(${PR_UNIRENDER_ENABLE_DEPENDENCIES})
	pr_add_dependency(${PROJ_NAME} util_raytracing TARGET PUBLIC)
	if(PR_UNIRENDER_WITH_CYCLES)
		add_subdirectory(external_libs/cycles)
		set_target_properties(UniRender_cycles PROPERTIES FOLDER modules/offline_render/unirender)
	endif()
	if(PR_UNIRENDER_WITH_LUXCORERENDER)
		add_subdirectory(external_libs/luxcorerender)
		set_target_properties(UniRender_LuxCoreRender PROPERTIES FOLDER modules/offline_render/unirender)
	endif()
endif()

pr_finalize(${PROJ_NAME} FOLDER modules/offline_render/unirender)
