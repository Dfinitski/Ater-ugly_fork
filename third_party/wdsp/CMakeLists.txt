cmake_minimum_required(VERSION 3.25)

if(NOT FFTW3_FOUND)
    message(FATAL_ERROR "WDSP 2.10 requires FFTW3")
endif()

find_package(Threads REQUIRED)

# ANAN-100D port: PureSignal engine. ON = Thetis' classic calcc/iqc
# (classic/README.md), OFF (default) = WDSP 2.10's rewritten one. The classic build
# copies upstream/ into the build tree and lays classic/ over it, so every
# TU (they include each other's headers by quoted relative paths) sees the
# classic calcc.h / iqc.h / TXA.h and upstream/ stays pristine.
# A string, not the former bool AETHER_WDSP_PS_CLASSIC: an existing build
# tree keeps a cached bool at its old value, so changing the default would not
# reach it. "wdsp210" (default) or "classic".
set(AETHER_WDSP_PS_ENGINE "wdsp210" CACHE STRING "PureSignal engine: wdsp210 or classic")
set_property(CACHE AETHER_WDSP_PS_ENGINE PROPERTY STRINGS wdsp210 classic)
if(AETHER_WDSP_PS_ENGINE STREQUAL "classic")
    set(AETHER_WDSP_PS_CLASSIC ON)
else()
    set(AETHER_WDSP_PS_CLASSIC OFF)
endif()
if(AETHER_WDSP_PS_CLASSIC)
    set(WDSP_SRC_DIR "${CMAKE_CURRENT_BINARY_DIR}/wdsp_ps_classic")
    file(GLOB _wdsp_up "${CMAKE_CURRENT_SOURCE_DIR}/upstream/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/upstream/*.h")
    file(GLOB _wdsp_cl "${CMAKE_CURRENT_SOURCE_DIR}/classic/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/classic/*.h")
    file(COPY ${_wdsp_up} DESTINATION "${WDSP_SRC_DIR}")
    file(COPY ${_wdsp_cl} DESTINATION "${WDSP_SRC_DIR}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_wdsp_up} ${_wdsp_cl})
    message(STATUS "WDSP: PureSignal = Thetis classic calcc/iqc")
else()
    set(WDSP_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/upstream")
    message(STATUS "WDSP: PureSignal = WDSP 2.10")
endif()

file(GLOB WDSP_UPSTREAM_SOURCES CONFIGURE_DEPENDS
    "${WDSP_SRC_DIR}/*.c")
# Upstream's unreferenced debug-only snoop TU uses a pre-C99 implicit-int
# function definition. Keep the snapshot pristine and leave that dormant
# diagnostic out of the portable library.
list(FILTER WDSP_UPSTREAM_SOURCES EXCLUDE REGEX "/snoop\\.c$")

add_library(aether_wdsp STATIC
    ${WDSP_UPSTREAM_SOURCES}
    port/wdsp_port.c
)
add_library(aether::wdsp ALIAS aether_wdsp)

set_target_properties(aether_wdsp PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

target_include_directories(aether_wdsp
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${WDSP_SRC_DIR}"
        ${FFTW3_INCLUDE_DIRS}
        $<$<NOT:$<PLATFORM_ID:Windows>>:${CMAKE_CURRENT_SOURCE_DIR}/port/include>
)

target_link_directories(aether_wdsp PRIVATE ${FFTW3_LIBRARY_DIRS})
target_link_libraries(aether_wdsp PRIVATE ${FFTW3_LIBRARIES} Threads::Threads)
if(AETHER_WDSP_PS_CLASSIC)
    # The host side adapts (GetPSDisp differs): seen by everything linking WDSP.
    target_compile_definitions(aether_wdsp PUBLIC AETHER_WDSP_PS_CLASSIC=1)
endif()

if(WIN32)
    target_compile_definitions(aether_wdsp PRIVATE FFTW_DLL)
    if(MSVC)
        target_compile_options(aether_wdsp PRIVATE
            "/FI${CMAKE_CURRENT_SOURCE_DIR}/port/include/wdsp_alloc_guard.h")
    else()
        target_compile_options(aether_wdsp PRIVATE
            -include "${CMAKE_CURRENT_SOURCE_DIR}/port/include/wdsp_alloc_guard.h")
    endif()
    target_link_libraries(aether_wdsp PRIVATE avrt)
else()
    # WDSP 2.10 introduced three TUs -- extrapolate.c, nurbs_fit.c and
    # nurbs_spline.c -- that call _aligned_malloc()/_aligned_free() without
    # including comm.h. On MSVC those resolve to the CRT and the omission is
    # invisible; everywhere else they are macros in the port header, which
    # those TUs never reach, and the build fails with implicit declarations.
    #
    # Force-include the port header for the same reason Windows force-includes
    # its allocation guard above: the portability surface is the build's job,
    # not a patch to the pristine snapshot. The header is #pragma once and its
    # whole body is #ifndef _WIN32, so this is idempotent for the TUs that
    # already reach it through comm.h -> Windows.h.
    target_compile_options(aether_wdsp PRIVATE
        -include "${CMAKE_CURRENT_SOURCE_DIR}/port/include/wdsp_port.h")
endif()

if(MSVC)
    target_compile_options(aether_wdsp PRIVATE /w)
else()
    target_compile_options(aether_wdsp PRIVATE -w)
endif()
