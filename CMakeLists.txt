# pthreads-retro68 - POSIX threads on top of the classic Mac OS Thread Manager
#
# Configure with one of the Retro68 toolchain files, e.g.
#
#   cmake -S . -B build-68k \
#         -DCMAKE_TOOLCHAIN_FILE=/opt/retro68/m68k-apple-macos/cmake/retro68.toolchain.cmake
#   cmake --build build-68k && cmake --install build-68k
#
# Supported toolchains / CMAKE_SYSTEM_NAME:
#   Retro68      68k            -> libpthread.a
#   RetroPPC     PowerPC (CFM)  -> libpthread.a          (+ ThreadsLib)
#   RetroCarbon  PowerPC Carbon -> libpthread-carbon.a   (+ CarbonLib)
cmake_minimum_required(VERSION 3.16)

project(pthreads-retro68 VERSION 1.0.0 LANGUAGES C)

# Retro68's add_application() links with the C++ driver.
enable_language(CXX)

if(NOT CMAKE_SYSTEM_NAME MATCHES "^(Retro68|RetroPPC|RetroCarbon)$")
    message(FATAL_ERROR
        "pthreads-retro68 must be built with a Retro68 toolchain file, e.g.\n"
        "  -DCMAKE_TOOLCHAIN_FILE=/opt/retro68/m68k-apple-macos/cmake/retro68.toolchain.cmake")
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(PTHREADS_RETRO68_BUILD_EXAMPLES "Build the example/test application" ON)
option(PTHREADS_RETRO68_INSTALL_FINDTHREADS
    "Install a FindThreads.cmake into the toolchain's CMake module directory so that find_package(Threads) resolves to this library"
    ON)

# ---------------------------------------------------------------------------
# Per-target naming
# ---------------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "RetroCarbon")
    set(PTR_LIB_NAME   pthread-carbon)
    set(PTR_PC_NAME    pthread-carbon)
    set(PTR_SYS_LIBS   "")            # CarbonLib is linked by -carbon
else()
    set(PTR_LIB_NAME   pthread)
    set(PTR_PC_NAME    pthread)
    if(CMAKE_SYSTEM_NAME STREQUAL "RetroPPC")
        set(PTR_SYS_LIBS ThreadsLib)  # CFM import library for the Thread Manager
    else()
        set(PTR_SYS_LIBS "")          # 68k: A-traps, nothing to link
    endif()
endif()

set(PTR_INCLUDE_SUBDIR "pthreads-retro68")
set(PTR_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/pthreads-retro68")
set(PTR_THREADS_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/Threads")

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------
add_library(pthread STATIC
    src/pt_thread.c
    src/pt_mutex.c
    src/pt_cond.c
    src/pt_rwlock.c
    src/pt_barrier_spin.c
    src/pt_tsd.c
    src/pt_attr.c
    src/pt_time.c
    src/pt_sem.c
)
add_library(pthreads-retro68::pthread ALIAS pthread)

set_target_properties(pthread PROPERTIES
    OUTPUT_NAME "${PTR_LIB_NAME}"
    C_STANDARD 99
    C_EXTENSIONS ON
    EXPORT_NAME pthread
)

target_include_directories(pthread
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PTR_INCLUDE_SUBDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(pthread PRIVATE
    -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing)

if(PTR_SYS_LIBS)
    target_link_libraries(pthread INTERFACE ${PTR_SYS_LIBS})
endif()

# ---------------------------------------------------------------------------
# Example / smoke test application
# ---------------------------------------------------------------------------
if(PTHREADS_RETRO68_BUILD_EXAMPLES AND COMMAND add_application)
    add_application(PThreadTest examples/pthread_test.c CONSOLE)
    target_link_libraries(PThreadTest pthread)
endif()

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------
install(TARGETS pthread
    EXPORT pthreads-retro68-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PTR_INCLUDE_SUBDIR})

install(FILES include/pthread.h include/semaphore.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PTR_INCLUDE_SUBDIR})

# One export file per system name: RetroPPC and RetroCarbon share a prefix.
install(EXPORT pthreads-retro68-targets
    NAMESPACE pthreads-retro68::
    FILE pthreads-retro68-targets-${CMAKE_SYSTEM_NAME}.cmake
    DESTINATION ${PTR_CMAKE_DIR})

configure_package_config_file(
    cmake/pthreads-retro68-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/pthreads-retro68-config.cmake
    INSTALL_DESTINATION ${PTR_CMAKE_DIR}
    NO_SET_AND_CHECK_MACRO NO_CHECK_REQUIRED_COMPONENTS_MACRO)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/pthreads-retro68-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/pthreads-retro68-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/pthreads-retro68-config-version.cmake
    DESTINATION ${PTR_CMAKE_DIR})

# "Threads" package: find_package(Threads CONFIG) -> Threads::Threads
configure_package_config_file(
    cmake/ThreadsConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ThreadsConfig.cmake
    INSTALL_DESTINATION ${PTR_THREADS_CMAKE_DIR}
    PATH_VARS PTR_CMAKE_DIR
    NO_SET_AND_CHECK_MACRO NO_CHECK_REQUIRED_COMPONENTS_MACRO)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/ThreadsConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
    ARCH_INDEPENDENT)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/ThreadsConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/ThreadsConfigVersion.cmake
    DESTINATION ${PTR_THREADS_CMAKE_DIR})

# FindThreads.cmake override.  The Retro68 toolchain files put
# <prefix>/cmake on CMAKE_MODULE_PATH, which takes precedence over CMake's
# own FindThreads module, so plain find_package(Threads) works.
if(PTHREADS_RETRO68_INSTALL_FINDTHREADS)
    install(FILES cmake/FindThreads.cmake DESTINATION cmake)
endif()

# pkg-config
if(PTR_SYS_LIBS)
    set(PTR_PC_SYS_LIBS "-l${PTR_SYS_LIBS}")
else()
    set(PTR_PC_SYS_LIBS "")
endif()
configure_file(pkgconfig/pthread.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/${PTR_PC_NAME}.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${PTR_PC_NAME}.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
