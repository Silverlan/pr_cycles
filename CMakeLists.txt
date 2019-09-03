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
link_external_library(client)
link_external_library(cengine)
link_external_library(glslang)
add_external_library(luajit)
link_external_library(alsoundsystem)
if(WIN32)
	add_external_library(lua)
endif()

add_include_dir(cycles)
add_include_dir(cycles_atomic)
add_include_dir(openimageio)
add_include_dir(openexr)
add_include_dir(glm)
add_include_dir(anvil_build)

add_def(ANVIL_VULKAN_CPP)
add_def(USE_VULKAN)

add_def(BOOST_ALL_NO_LIB)
add_def(OIIO_STATIC_BUILD)
add_def(GLEW_STATIC)
add_def(FREEGLUT_STATIC)
add_def(FREEGLUT_LIB_PRAGMAS=0)
add_def(CCL_NAMESPACE_BEGIN=namespace\ ccl\ {)
add_def(CCL_NAMESPACE_END=})

link_generic_module_libraries()
register_pragma_module(${PROJ_NAME})
