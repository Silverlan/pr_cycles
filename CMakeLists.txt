cmake_minimum_required(VERSION 3.12)

include("../CMakeShared.txt")

set(PROJ_NAME pr_unirender)

project(${PROJ_NAME} CXX)

# set(PRECOMPILED_HEADER "stdafx")

link_external_library(prosper)
link_external_library(glfw)
link_external_library(iglfw)
link_external_library(wgui)
link_external_library(util_image)
link_external_library(util_raytracing)
link_external_library(client)
link_external_library(cengine)
add_external_library(luajit)
link_external_library(materialsystem)
link_external_library(cmaterialsystem)
link_external_library(alsoundsystem)
link_external_library(opensubdiv)
link_external_library(util_udm)
link_external_library(lz4)
if(WIN32)
	add_external_library(lua)
endif()

add_include_dir(glm)

add_external_library(openimageio)
link_external_library(openimagedenoise)

add_def(BOOST_ALL_NO_LIB)
add_def(OIIO_STATIC_BUILD)
add_def(RT_ENABLE_SUBDIVISION) # TODO: Make this optional

link_generic_module_libraries()
register_pragma_module(${PROJ_NAME})

target_include_directories(${PROJ_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../pr_dds/include")

add_dependencies(${PROJ_NAME} util_raytracing)

add_subdirectory(external_libs/cycles)
add_subdirectory(external_libs/luxcorerender)

set_target_properties(pr_unirender PROPERTIES FOLDER modules/offline_render/unirender)
set_target_properties(UniRender_Cycles PROPERTIES FOLDER modules/offline_render/unirender)
set_target_properties(UniRender_LuxCoreRender PROPERTIES FOLDER modules/offline_render/unirender)

set_property(GLOBAL PROPERTY PRAGMA_MODULE_SKIP_TARGET_PROPERTY_FOLDER 1)
