include("${CMAKE_SOURCE_DIR}/cmake/install_helper.cmake")

set(version "2026-04-16")
set(base_url "https://github.com/Silverlan/UniRender_Cycles/releases/download")

if(NOT config.no_build_networking)
    pr_fetch_prebuilt_binaries("${PRAGMA_DEPS_DIR}" "${base_url}" "${version}" PRIMARY_DIR "${PRAGMA_DEPS_DIR}/cycles")
endif()
