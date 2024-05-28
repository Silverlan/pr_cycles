include(${CMAKE_SOURCE_DIR}/cmake/pr_common.cmake)

set(PR_UNIRENDER_ENABLE_DEPENDENCIES 1 CACHE BOOL "Enable dependencies?")

set(PROJ_NAME pr_unirender)
pr_add_library(${PROJ_NAME} SHARED)

pr_add_dependency(${PROJ_NAME} client TARGET PUBLIC)

pr_add_headers(${PROJ_NAME} "include/")
pr_add_sources(${PROJ_NAME} "src/")

pr_add_compile_definitions(${PROJ_NAME} -DOIIO_STATIC_BUILD -DRT_ENABLE_SUBDIVISION)

if(${PR_UNIRENDER_ENABLE_DEPENDENCIES})
	pr_add_dependency(${PROJ_NAME} util_raytracing TARGET PUBLIC)
	pr_add_dependency(${PROJ_NAME} UniRender_cycles TARGET PUBLIC)
	add_subdirectory(external_libs/cycles)
	add_subdirectory(external_libs/luxcorerender)
	
	set_target_properties(UniRender_cycles PROPERTIES FOLDER modules/offline_render/unirender)
	set_target_properties(UniRender_LuxCoreRender PROPERTIES FOLDER modules/offline_render/unirender)
endif()

pr_finalize(${PROJ_NAME} FOLDER modules/offline_render/unirender)
