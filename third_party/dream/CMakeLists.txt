cmake_minimum_required(VERSION 3.25)

# ANAN port: Dream's DRM receiver core (GPL v2 or later), without its Qt GUI,
# sound drivers or transmitter. See README.aether.md for the origin, the file
# selection and the local changes.
#
# Built WITHOUT Qt on purpose: Dream switches its threads, settings and logging
# to Qt when QT_CORE_LIB is defined, and that path pulls in its GUI-era code.
# Nothing here links Qt, so QT_CORE_LIB stays undefined.

if(NOT FFTW3_FOUND)
    message(FATAL_ERROR "Dream needs FFTW3 (double precision), like WDSP")
endif()

find_package(Threads REQUIRED)

file(GLOB_RECURSE DREAM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

add_library(aether_dream STATIC
    ${DREAM_SOURCES}
    aether/Pacer.cpp
    aether/fdk_dyn.cpp
    aether/DrmEngine.cpp
)
add_library(aether::dream ALIAS aether_dream)

set_target_properties(aether_dream PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    C_STANDARD 11
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

# Consumers see aether/DrmEngine.h only (a Qt-free interface). Dream includes
# its own headers as "src/..." from the tree root and relative to each file;
# the FDK-AAC headers are only for types (aether/fdk_dyn.cpp loads the library
# at run time).
target_include_directories(aether_dream
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../fdk-aac/include"
        ${FFTW3_INCLUDE_DIRS}
)

target_compile_definitions(aether_dream
    PRIVATE
        NO_QT
        AETHER_DREAM        # AetherSDR's cuts: no Windows MME sound driver, ...
        HAVE_STDINT_H
        HAVE_LIBZ
        HAVE_LIBFDK_AAC
        HAVE_USAC
        $<$<NOT:$<PLATFORM_ID:Windows>>:HAVE_DLFCN_H>
)

target_link_directories(aether_dream PRIVATE ${FFTW3_LIBRARY_DIRS})
target_link_libraries(aether_dream PRIVATE ${FFTW3_LIBRARIES} Threads::Threads)
if(USE_SYSTEM_ZLIB)
    target_link_libraries(aether_dream PRIVATE PkgConfig::zlib)
else()
    target_link_libraries(aether_dream PRIVATE zlibstatic)
endif()

if(WIN32)
    target_compile_definitions(aether_dream PRIVATE
        FFTW_DLL _USE_MATH_DEFINES NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
        _WINSOCK_DEPRECATED_NO_WARNINGS)
    target_link_libraries(aether_dream PRIVATE ws2_32)
else()
    target_link_libraries(aether_dream PRIVATE ${CMAKE_DL_LIBS})
endif()

if(MSVC)
    target_compile_options(aether_dream PRIVATE /w /bigobj /EHsc)
else()
    target_compile_options(aether_dream PRIVATE -w)
endif()
