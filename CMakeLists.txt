cmake_minimum_required(VERSION 3.12)

include("../CMakeShared.txt")

set(PROJ_NAME pr_cycles)

project(${PROJ_NAME} CXX)

# set(PRECOMPILED_HEADER "stdafx")

link_external_library(prosper)
link_external_library(vulkan)
link_external_library(anvil)
link_external_library(glfw)
link_external_library(iglfw)
link_external_library(wgui)
link_external_library(util_image)
link_external_library(util_raytracing)
link_external_library(client)
link_external_library(cengine)
link_external_library(glslang)
add_external_library(luajit)
link_external_library(materialsystem)
link_external_library(cmaterialsystem)
link_external_library(alsoundsystem)
link_external_library(opensubdiv)
if(WIN32)
	add_external_library(lua)
endif()

add_include_dir(cycles)
add_include_dir(cycles_atomic)
add_include_dir(openimageio)
add_include_dir(openexr)
add_include_dir(glm)
add_include_dir(anvil_build)

set(DEPENDENCY_CYCLES_LIBRARY_LOCATION "" CACHE FILEPATH "Path to cycles libraries.")
set(DEPENDENCY_OPENEXR_LIBRARY_LOCATION "" CACHE FILEPATH "Path to openexr libraries.")
add_external_library(openimageio)
link_external_library(openimagedenoise)
add_external_library(tiff)
add_external_library(lpng)
add_external_library(zlib)
add_external_library(jpeg)
add_external_library(glew)

add_def(ANVIL_VULKAN_CPP)
add_def(USE_VULKAN)

add_def(BOOST_ALL_NO_LIB)
add_def(OIIO_STATIC_BUILD)
add_def(GLEW_STATIC)
add_def(FREEGLUT_STATIC)
add_def(FREEGLUT_LIB_PRAGMAS=0)
add_def(CCL_NAMESPACE_BEGIN=namespace\ ccl\ {)
add_def(CCL_NAMESPACE_END=})
add_def(RT_ENABLE_SUBDIVISION) # TODO: Make this optional

option(CONFIG_ENABLE_CYCLES_LOGGING "Enable logging for the Cycles renderer. Requires glog and gflags, as well as Cycles built with logging enabled!" OFF)

if(CONFIG_ENABLE_CYCLES_LOGGING)
	link_external_library(glog)
	link_external_library(gflags)
	add_include_dir(glog_os)

	add_def(ENABLE_CYCLES_LOGGING)
endif()

link_generic_module_libraries()
register_pragma_module(${PROJ_NAME})

target_include_directories(${PROJ_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../pr_dds/include")

target_link_libraries(pr_cycles
	opengl32
	glu32
)

add_dependencies(${PROJ_NAME} util_raytracing)

if(WIN32)
	set(DEPENDENCY_CYCLES_UTIL_LIBRARY ${DEPENDENCY_CYCLES_LIBRARY_LOCATION}/cycles_util.lib CACHE PATH "Path to library." FORCE)
	set(DEPENDENCY_CYCLES_DEVICE_LIBRARY ${DEPENDENCY_CYCLES_LIBRARY_LOCATION}/cycles_device.lib CACHE PATH "Path to library." FORCE)
	set(DEPENDENCY_CYCLES_RENDER_LIBRARY ${DEPENDENCY_CYCLES_LIBRARY_LOCATION}/cycles_render.lib CACHE PATH "Path to library." FORCE)
	set(DEPENDENCY_CYCLES_KERNEL_LIBRARY ${DEPENDENCY_CYCLES_LIBRARY_LOCATION}/cycles_kernel.lib CACHE PATH "Path to library." FORCE)
	set(DEPENDENCY_CYCLES_GRAPH_LIBRARY ${DEPENDENCY_CYCLES_LIBRARY_LOCATION}/cycles_graph.lib CACHE PATH "Path to library." FORCE)
	set(DEPENDENCY_CYCLES_SUBD_LIBRARY ${DEPENDENCY_CYCLES_LIBRARY_LOCATION}/cycles_subd.lib CACHE PATH "Path to library." FORCE)
	set(DEPENDENCY_CYCLES_BVH_LIBRARY ${DEPENDENCY_CYCLES_LIBRARY_LOCATION}/cycles_bvh.lib CACHE PATH "Path to library." FORCE)
	set(DEPENDENCY_CYCLES_CLEW_LIBRARY ${DEPENDENCY_CYCLES_LIBRARY_LOCATION}/extern_clew.lib CACHE PATH "Path to library." FORCE)
	set(DEPENDENCY_CYCLES_CUEW_LIBRARY ${DEPENDENCY_CYCLES_LIBRARY_LOCATION}/extern_cuew.lib CACHE PATH "Path to library." FORCE)
	set(DEPENDENCY_CYCLES_NUMAAPI_LIBRARY ${DEPENDENCY_CYCLES_LIBRARY_LOCATION}/extern_numaapi.lib CACHE PATH "Path to library." FORCE)

	set(DEPENDENCY_OPENEXR_HALF_LIBRARY ${DEPENDENCY_OPENEXR_LIBRARY_LOCATION}/Half-2_3_static.lib CACHE PATH "Path to library." FORCE)
	set(DEPENDENCY_OPENEXR_IEX_LIBRARY ${DEPENDENCY_OPENEXR_LIBRARY_LOCATION}/Iex-2_3_static.lib CACHE PATH "Path to library." FORCE)
	set(DEPENDENCY_OPENEXR_IEX_MATH_LIBRARY ${DEPENDENCY_OPENEXR_LIBRARY_LOCATION}/IexMath-2_3_static.lib CACHE PATH "Path to library." FORCE)
	set(DEPENDENCY_OPENEXR_ILM_IMF_LIBRARY ${DEPENDENCY_OPENEXR_LIBRARY_LOCATION}/IlmImf-2_3_static.lib CACHE PATH "Path to library." FORCE)
	set(DEPENDENCY_OPENEXR_ILM_IMF_UTIL ${DEPENDENCY_OPENEXR_LIBRARY_LOCATION}/IlmImfUtil-2_3_static.lib CACHE PATH "Path to library." FORCE)
	set(DEPENDENCY_OPENEXR_ILM_THREAD ${DEPENDENCY_OPENEXR_LIBRARY_LOCATION}/IlmThread-2_3_static.lib CACHE PATH "Path to library." FORCE)
	set(DEPENDENCY_OPENEXR_IMATH ${DEPENDENCY_OPENEXR_LIBRARY_LOCATION}/Imath-2_3_static.lib CACHE PATH "Path to library." FORCE)
	target_link_libraries(pr_cycles
		${DEPENDENCY_CYCLES_UTIL_LIBRARY}
		${DEPENDENCY_CYCLES_DEVICE_LIBRARY}
		${DEPENDENCY_CYCLES_RENDER_LIBRARY}
		${DEPENDENCY_CYCLES_KERNEL_LIBRARY}
		${DEPENDENCY_CYCLES_GRAPH_LIBRARY}
		${DEPENDENCY_CYCLES_SUBD_LIBRARY}
		${DEPENDENCY_CYCLES_BVH_LIBRARY}
		${DEPENDENCY_CYCLES_CLEW_LIBRARY}
		${DEPENDENCY_CYCLES_CUEW_LIBRARY}
		${DEPENDENCY_CYCLES_NUMAAPI_LIBRARY}

		${DEPENDENCY_OPENEXR_HALF_LIBRARY}
		${DEPENDENCY_OPENEXR_IEX_LIBRARY}
		${DEPENDENCY_OPENEXR_IEX_MATH_LIBRARY}
		${DEPENDENCY_OPENEXR_ILM_IMF_LIBRARY}
		${DEPENDENCY_OPENEXR_ILM_IMF_UTIL}
		${DEPENDENCY_OPENEXR_ILM_THREAD}
		${DEPENDENCY_OPENEXR_IMATH}
		${DEPENDENCY_BOOST_LIBRARY_LOCATION}/libboost_filesystem-vc142-mt-x64-1_72.lib
		${DEPENDENCY_BOOST_LIBRARY_LOCATION}/libboost_date_time-vc142-mt-x64-1_72.lib
		${DEPENDENCY_BOOST_LIBRARY_LOCATION}/libboost_thread-vc142-mt-x64-1_72.lib
		${DEPENDENCY_BOOST_LIBRARY_LOCATION}/libboost_chrono-vc142-mt-x64-1_72.lib
		${DEPENDENCY_BOOST_LIBRARY_LOCATION}/libboost_regex-vc142-mt-x64-1_72.lib
	)
endif()
