set(PCK "openimagedenoise")

if (${PCK}_FOUND)
  return()
endif()

find_path(${PCK}_INCLUDE_DIR
  NAMES OpenImageDenoise/oidn.hpp
  HINTS
    ${PRAGMA_DEPS_DIR}/openimagedenoise/include
)

find_library(${PCK}_LIBRARY
  NAMES OpenImageDenoise
  HINTS
    ${PRAGMA_DEPS_DIR}/openimagedenoise/lib
)

find_library(${PCK}_CORE_LIBRARY
  NAMES OpenImageDenoise_core libOpenImageDenoise_core.so.2.3.2
  HINTS
    ${PRAGMA_DEPS_DIR}/openimagedenoise/lib
)

set(LIB_VARS "${PCK}_LIBRARY" "${PCK}_CORE_LIBRARY")
set(LIB_CORE_VALUES "${${PCK}_LIBRARY}" "${${PCK}_CORE_LIBRARY}")
set(LIB_DEVICE_VALUES)

set(REQ_VARS ${PCK}_INCLUDE_DIR)
if(UNIX)
  find_library(${PCK}_DEVICE_CPU_LIBRARY
    NAMES OpenImageDenoise_device_cpu libOpenImageDenoise_device_cpu.so.2.3.2
    HINTS
      ${PRAGMA_DEPS_DIR}/openimagedenoise/lib
  )

  find_library(${PCK}_DEVICE_CUDA_LIBRARY
    NAMES OpenImageDenoise_device_cuda libOpenImageDenoise_device_cuda.so.2.3.2
    HINTS
      ${PRAGMA_DEPS_DIR}/openimagedenoise/lib
  )

  find_library(${PCK}_DEVICE_HIP_LIBRARY
    NAMES OpenImageDenoise_device_hip libOpenImageDenoise_device_hip.so.2.3.2
    HINTS
      ${PRAGMA_DEPS_DIR}/openimagedenoise/lib
  )

  find_library(${PCK}_DEVICE_SYCL_LIBRARY
    NAMES OpenImageDenoise_device_sycl libOpenImageDenoise_device_sycl.so.2.3.2
    HINTS
      ${PRAGMA_DEPS_DIR}/openimagedenoise/lib
  )

  set(LIB_VARS ${LIB_VARS} ${PCK}_DEVICE_CPU_LIBRARY ${PCK}_DEVICE_CUDA_LIBRARY ${PCK}_DEVICE_HIP_LIBRARY ${PCK}_DEVICE_SYCL_LIBRARY)
  set(LIB_DEVICE_VALUES ${LIB_DEVICE_VALUES} ${${PCK}_DEVICE_CPU_LIBRARY} ${${PCK}_DEVICE_CUDA_LIBRARY} ${${PCK}_DEVICE_HIP_LIBRARY} ${${PCK}_DEVICE_SYCL_LIBRARY})
  set(REQ_VARS ${REQ_VARS} ${LIB_VARS})
else()
  find_file(
    ${PCK}_OIDN_RUNTIME
    NAMES OpenImageDenoise.dll
    HINTS ${PRAGMA_DEPS_DIR}/openimagedenoise/bin
  )
  find_file(
    ${PCK}_OIDN_CORE_RUNTIME
    NAMES OpenImageDenoise_core.dll
    HINTS ${PRAGMA_DEPS_DIR}/openimagedenoise/bin
  )
  find_file(
    ${PCK}_OIDN_DEVICE_CPU_RUNTIME
    NAMES OpenImageDenoise_device_cpu.dll
    HINTS ${PRAGMA_DEPS_DIR}/openimagedenoise/bin
  )
  find_file(
    ${PCK}_OIDN_DEVICE_CUDA_RUNTIME
    NAMES OpenImageDenoise_device_cuda.dll
    HINTS ${PRAGMA_DEPS_DIR}/openimagedenoise/bin
  )
  find_file(
    ${PCK}_OIDN_DEVICE_HIP_RUNTIME
    NAMES OpenImageDenoise_device_hip.dll
    HINTS ${PRAGMA_DEPS_DIR}/openimagedenoise/bin
  )
  find_file(
    ${PCK}_OIDN_DEVICE_SYCL_RUNTIME
    NAMES OpenImageDenoise_device_sycl.dll
    HINTS ${PRAGMA_DEPS_DIR}/openimagedenoise/bin
  )
  set(REQ_VARS ${REQ_VARS} ${PCK}_OIDN_RUNTIME ${PCK}_OIDN_CORE_RUNTIME ${PCK}_OIDN_DEVICE_CPU_RUNTIME ${PCK}_OIDN_DEVICE_CUDA_RUNTIME ${PCK}_OIDN_DEVICE_HIP_RUNTIME ${PCK}_OIDN_DEVICE_SYCL_RUNTIME)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(${PCK}
  REQUIRED_VARS ${REQ_VARS}
)

if(${PCK}_FOUND)
  set(${PCK}_LIBRARIES   ${LIB_CORE_VALUES})
  set(${PCK}_device_LIBRARIES   ${LIB_DEVICE_VALUES})
  set(${PCK}_INCLUDE_DIRS ${${PCK}_INCLUDE_DIR})
  if(WIN32)
    set(${PCK}_RUNTIME   ${${PCK}_OIDN_RUNTIME} ${${PCK}_OIDN_CORE_RUNTIME})
    set(${PCK}_device_RUNTIME   ${${PCK}_OIDN_DEVICE_CPU_RUNTIME} ${${PCK}_OIDN_DEVICE_CUDA_RUNTIME} ${${PCK}_OIDN_DEVICE_HIP_RUNTIME} ${${PCK}_OIDN_DEVICE_SYCL_RUNTIME})
  endif()
endif()
