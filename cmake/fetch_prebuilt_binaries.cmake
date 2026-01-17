include("${CMAKE_SOURCE_DIR}/cmake/install_helper.cmake")

set(version "2026-01-11")
set(base_url "https://github.com/Silverlan/UniRender_Cycles/releases/download/${version}")

pr_fetch_prebuilt_binaries("${PRAGMA_DEPS_DIR}/cycles" "${base_url}" "${version}")
