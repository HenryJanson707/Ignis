CPMAddPackage(
    NAME stpp
    GITHUB_REPOSITORY PearCoding/stpp 
    GIT_TAG main
    EXCLUDE_FROM_ALL
)

###########################################################
set(IG_DISABLE_DEVICES "" CACHE STRING "Devices to not build a driver for.")
mark_as_advanced(IG_DISABLE_DEVICES)

string(REPLACE " " ";" _DISABLED_DEVICES "${IG_DISABLE_DEVICES}")
list(REMOVE_DUPLICATES _DISABLED_DEVICES)
list(TRANSFORM _DISABLED_DEVICES TOUPPER)

###########################################################
option(IG_ASSUME_NVIDIA "Assume CUDA libraries are available" OFF)
mark_as_advanced(IG_ASSUME_NVIDIA)
if(IG_ASSUME_NVIDIA OR AnyDSL_runtime_HAS_CUDA_SUPPORT)
    set(_HAS_CUDA ON)
else()
    find_library(CUDA_LIBRARY cudart HINTS ${CUDA_DIR} PATHS /usr/local/cuda
    ENV CUDA_PATH
    ENV CUDA_LIB_PATH
    PATH_SUFFIXES "lib/Win32" "lib" "libWin32")
    if(EXISTS "${CUDA_LIBRARY}")
        set(_HAS_CUDA ON)
    endif()
endif()

###########################################################
option(IG_ASSUME_AMD "Assume AMD rocm libraries are available" OFF)
mark_as_advanced(IG_ASSUME_AMD)
if(IG_ASSUME_AMD OR AnyDSL_runtime_HAS_HSA_SUPPORT)
    set(_HAS_HSA ON)
else()
    find_library(HSA_LIBRARY hsa-runtime64 HINTS ${HSA_DIR} PATHS /opt/rocm PATH_SUFFIXES lib)
    if(EXISTS "${HSA_LIBRARY}")
        set(_HAS_HSA ON)
    endif()
endif()

###########################################################
include(CMakeDependentOption)
option(IG_WITH_DEVICE_CPU "Enable CPU device with optional vectorization support" ON)
option(IG_WITH_DEVICE_GPU "Enable GPU device for NVidia or AMD GPUs" ON)
cmake_dependent_option(IG_WITH_DEVICE_CPU_SIMD   "Enable CPU vectorized device" ON  "IG_WITH_DEVICE_CPU" OFF)
cmake_dependent_option(IG_WITH_DEVICE_GPU_NVIDIA "Enable NVidia GPU device"     ON  "IG_WITH_DEVICE_GPU;_HAS_CUDA" OFF)
cmake_dependent_option(IG_WITH_DEVICE_GPU_AMD    "Enable AMD GPU device"        ON  "IG_WITH_DEVICE_GPU;_HAS_HSA"  OFF)

cmake_dependent_option(IG_WITH_DEVICE_CPU_GENERIC_ALWAYS "Always build the generic CPU device" OFF  "IG_WITH_DEVICE_CPU" OFF)

set(DEVICE_SELECTION )

if(IG_WITH_DEVICE_CPU)
    # TODO: Select best feature for current host
    if(IG_WITH_DEVICE_CPU_SIMD)
        list(APPEND DEVICE_SELECTION
            #"SSE42"
            #"AVX"
            "AVX2"
            #"AVX512"
            #"ASIMD"
            )
    else()
        list(APPEND DEVICE_SELECTION "DEFAULT")
    endif()

    if(IG_WITH_DEVICE_CPU_GENERIC_ALWAYS AND IG_WITH_DEVICE_CPU_SIMD)
        list(APPEND DEVICE_SELECTION "DEFAULT")
    endif()
endif()

if(IG_WITH_DEVICE_GPU)
    if(IG_WITH_DEVICE_GPU_NVIDIA)
        list(APPEND DEVICE_SELECTION "NVVM")
    endif()
    if(IG_WITH_DEVICE_GPU_AMD)
        list(APPEND DEVICE_SELECTION "AMDGPU")
    endif()
endif()

if(NOT DEVICE_SELECTION)
    message(FATAL_ERROR "No devices selected. Aborting")
endif()

set(DEVICES )
foreach(dev IN LISTS DEVICE_SELECTION)
    if(NOT "${dev}" IN_LIST _DISABLED_DEVICES)
        list(APPEND DEVICES "-D DEVICE_${dev}")
    endif()
endforeach()

if(NOT DEVICES)
    message(FATAL_ERROR "All devices are filtered out. Aborting")
endif()

###########################################################
function(extract_name out_var in_var)
    string(REGEX REPLACE "^([a-zA-Z]+_)(.+)$" "\\2" _t1 ${in_var})
    string(TOLOWER ${_t1} _t2)
    set(${out_var} ${_t2} PARENT_SCOPE)
endfunction()

function(var_to_name out_var variant)
    string(REPLACE " " ";" _l1 "${variant}")
    set(_names )
    foreach(_var1 ${_l1})
        if(NOT "${_var1}" STREQUAL "-D")
            extract_name(_n1 ${_var1})
            list(APPEND _names "${_n1}")
        endif()
    endforeach()
    string(JOIN "_" _names2 ${_names})
    set(${out_var} ${_names2} PARENT_SCOPE)
endfunction()

function(cross out_list list1 list2 sep)
    set(_list )
    foreach(_var1 ${${list1}})
        foreach(_var2 ${${list2}})
        list(APPEND _list "${_var1}${sep}${_var2}")
        endforeach()
    endforeach()
    set(${out_list} ${_list} PARENT_SCOPE)
endfunction()

set(VARIANTS ${DEVICES})

if(NOT VARIANTS)
    message(FATAL_ERROR "No variants available. This error should have been catched earlier... Aborting")
endif()

###########################################################
option(IG_BUILD_DRIVER_PARALLEL "Build driver files in parallel. Not recommended" OFF)

set(_targets )
foreach(var ${VARIANTS})
    var_to_name(var_name ${var})
    message(VERBOSE "Driver: ${var_name} = ${var}")
    set(out_file "${CMAKE_CURRENT_BINARY_DIR}/driver_${var_name}.art")
    separate_arguments(args UNIX_COMMAND ${var})
    add_custom_command(
        OUTPUT "${out_file}"
        COMMAND $<TARGET_FILE:stpp> ${args} ${CMAKE_CURRENT_SOURCE_DIR}/tracer.artpp > ${out_file}
        MAIN_DEPENDENCY tracer.artpp
        COMMENT "Generating variant file: ${out_file}" 
        VERBATIM)
    set(_objs ) # Reset list
    set(_interface_name driver_interface_${var_name})
    anydsl_runtime_wrap(_objs
        NAME "driver_${var_name}"
        FRONTEND "artic"
        CLANG_FLAGS ${ARTIC_CLANG_FLAGS}
        ARTIC_FLAGS --log-level info -Wall
        FILES ${ARTIC_MAIN_SRC} ${out_file})
    set(_target_name ig_driver_${var_name})
    add_library(${_target_name} MODULE ${_objs} glue.cpp ShallowArray.h)
    set_target_properties(${_target_name} PROPERTIES PREFIX "")
    target_compile_definitions(${_target_name} PRIVATE ${args})
    target_link_libraries(${_target_name} PRIVATE ${AnyDSL_runtime_LIBRARIES} ig_lib_runtime)
    target_include_directories(${_target_name} PUBLIC $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>)
    add_dependencies(${_target_name} artic_c_interface)

    # Some of the compilation process requires a lot of memory.
    # Prevent parallel build of the drivers (per default) by chaining them together
    # This will rebuild all the drivers up the line,
    # however drivers are dynamically defined and ment to be rebuild anyway.
    if(NOT IG_BUILD_DRIVER_PARALLEL)
        list(LENGTH _targets _length)
        if(_length GREATER 0)
            list(GET _targets -1 _prev)
            add_dependencies(${_target_name} ${_prev})
        endif()
    endif()

    list(APPEND _targets ${_target_name})
endforeach()

add_custom_target(ignis_drivers)
add_dependencies(ignis_drivers ${_targets})
