cmake_minimum_required(VERSION 3.12)


#############

function(add_def DEF)
	set(DEFINITIONS ${DEFINITIONS} ${DEF} PARENT_SCOPE)
endfunction(add_def)

function(register_pragma_module IDENTIFIER)
	set(LIB_TYPE SHARED)
	if(ENABLE_STATIC_LIBRARY_FLAG)
		option(CONFIG_STATIC_LIBRARY "Build as static library?" OFF)
	endif()
	if((DEFINED CONFIG_STATIC_LIBRARY) AND (${CONFIG_STATIC_LIBRARY}))
		set(LIB_TYPE STATIC)
	endif()

	foreach(def IN LISTS DEFINITIONS)
		add_definitions(-D${def})
	endforeach(def)

	function(def_vs_filters FILE_LIST)
		foreach(source IN LISTS FILE_LIST)
		    get_filename_component(source_path "${source}" PATH)
		    string(REPLACE "${CMAKE_CURRENT_LIST_DIR}" "" source_path_relative "${source_path}")
		    string(REPLACE "/" "\\" source_path_msvc "${source_path_relative}")
		    source_group("${source_path_msvc}" FILES "${source}")
		endforeach()
	endfunction(def_vs_filters)

	file(GLOB_RECURSE SRC_FILES
	    "${CMAKE_CURRENT_LIST_DIR}/src/*.h"
	    "${CMAKE_CURRENT_LIST_DIR}/src/*.hpp"
	    "${CMAKE_CURRENT_LIST_DIR}/src/*.cpp"
	    "${CMAKE_CURRENT_LIST_DIR}/src/*.c"
	    "${CMAKE_CURRENT_LIST_DIR}/include/*.h"
	    "${CMAKE_CURRENT_LIST_DIR}/include/*.hpp"
	)
	set(SRC_FILES_CUSTOM)
	set(SRC_FILES_CUSTOM_PATHS)
	foreach(SRC_PATH IN LISTS SOURCE_PATHS)
		set(TMP_SRC_FILES)
		file(GLOB_RECURSE TMP_SRC_FILES
		    "${SRC_PATH}/src/*.h"
		    "${SRC_PATH}/src/*.hpp"
		    "${SRC_PATH}/src/*.cpp"
		    "${SRC_PATH}/src/*.c"
		    "${SRC_PATH}/include/*.h"
		    "${SRC_PATH}/include/*.hpp"
		)
		foreach(filePath IN LISTS TMP_SRC_FILES)
			list(APPEND SRC_FILES_CUSTOM ${filePath})
			list(APPEND SRC_FILES_CUSTOM_PATHS ${SRC_PATH})
		endforeach()
	endforeach()

	set(LIB_SRC_FILES)
	list(APPEND LIB_SRC_FILES ${SRC_FILES})
	list(APPEND LIB_SRC_FILES ${SRC_FILES_CUSTOM})
	add_library(${PROJ_NAME} ${LIB_TYPE} ${LIB_SRC_FILES})

	if(WIN32)
		target_compile_options(${PROJ_NAME} PRIVATE /wd4251)
	endif()

	list(LENGTH SRC_FILES_CUSTOM count)
	math(EXPR count "${count}-1")
	if(${count} GREATER 0)
		foreach(i RANGE ${count})
		  list(GET SRC_FILES_CUSTOM ${i} source)
		  get_filename_component(source_path ${source} DIRECTORY)
		  list(GET SRC_FILES_CUSTOM_PATHS ${i} source_origin)

		  string(REPLACE "${source_origin}/" "" group_name "${source_path}")
		  string(REPLACE "/" "\\" group_name_msvc "${group_name}")

		  source_group(${group_name_msvc} FILES "${source}")
		  target_include_directories(${PROJ_NAME} PRIVATE ${source_origin}/include)
		  target_include_directories(${PROJ_NAME} PRIVATE ${source_origin}/src)
		endforeach()
	endif()

	def_vs_filters("${SRC_FILES}")

	foreach(LIB IN LISTS LIBRARIES)
		target_link_libraries(${PROJ_NAME} ${${LIB}})
	endforeach(LIB)

	target_include_directories(${PROJ_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)
	target_include_directories(${PROJ_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)

	foreach(INCLUDE_PATH IN LISTS INCLUDE_DIRS)
		target_include_directories(${PROJ_NAME} PRIVATE ${${INCLUDE_PATH}})
	endforeach(INCLUDE_PATH)

	set(TARGET_PROPERTIES LINKER_LANGUAGE CXX)
	if(DEFINED PRECOMPILED_HEADER)
		set(TARGET_PROPERTIES ${TARGET_PROPERTIES} COMPILE_FLAGS /Yu"${PRECOMPILED_HEADER}.h")
		set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/src/${PRECOMPILED_HEADER}.cpp" PROPERTIES COMPILE_FLAGS "/Yc${PRECOMPILED_HEADER}.h")
	endif()
	set_target_properties(${PROJ_NAME} PROPERTIES ${TARGET_PROPERTIES})
endfunction(register_pragma_module)

macro(link_generic_module_libraries)
	link_external_library(sharedutils)
	link_external_library(mathutil)
	link_external_library(vfilesystem)
	link_external_library(shared)
	link_external_library(luasystem)
	link_external_library(LuaJIT)
	link_external_library(luabind)
	link_external_library(materialsystem)
	link_external_library(datasystem)
	link_external_library(ishared)
	link_external_library(util_udm)

	add_include_dir(boost)
endmacro(link_generic_module_libraries)

set(PR_UNIRENDER_ENABLE_DEPENDENCIES 1 CACHE BOOL "Enable dependencies?")

#############

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

if(${PR_UNIRENDER_ENABLE_DEPENDENCIES})
	add_dependencies(${PROJ_NAME} util_raytracing)
	add_subdirectory(external_libs/cycles)
	add_subdirectory(external_libs/luxcorerender)
	
	set_target_properties(pr_unirender PROPERTIES FOLDER modules/offline_render/unirender)
	set_target_properties(UniRender_Cycles PROPERTIES FOLDER modules/offline_render/unirender)
	set_target_properties(UniRender_LuxCoreRender PROPERTIES FOLDER modules/offline_render/unirender)
	
	set_property(GLOBAL PROPERTY PRAGMA_MODULE_SKIP_TARGET_PROPERTY_FOLDER 1)
endif()
