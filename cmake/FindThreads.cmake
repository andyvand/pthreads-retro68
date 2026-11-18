# FindThreads.cmake override for Retro68 (installed by pthreads-retro68).
#
# The Retro68 toolchain files append <prefix>/cmake to CMAKE_MODULE_PATH,
# so this module shadows CMake's own FindThreads and makes
#
#   find_package(Threads REQUIRED)
#   target_link_libraries(app PRIVATE Threads::Threads)
#
# resolve to the Thread Manager based pthreads library.  For any other
# platform it defers to CMake's stock module.

if(NOT CMAKE_SYSTEM_NAME MATCHES "^(Retro68|RetroPPC|RetroCarbon)$")
    include(${CMAKE_ROOT}/Modules/FindThreads.cmake)
    return()
endif()

set(_ft_quiet)
if(Threads_FIND_QUIETLY)
    set(_ft_quiet QUIET)
endif()

find_package(Threads CONFIG ${_ft_quiet}
    PATHS "${CMAKE_CURRENT_LIST_DIR}/../lib/cmake/Threads"
    NO_DEFAULT_PATH)
if(NOT Threads_FOUND)
    find_package(Threads CONFIG ${_ft_quiet})
endif()
unset(_ft_quiet)

include(FindPackageHandleStandardArgs)
set(_ft_cfg "${Threads_CONFIG}")
find_package_handle_standard_args(Threads
    REQUIRED_VARS _ft_cfg
    REASON_FAILURE_MESSAGE "pthreads-retro68 is not installed for ${CMAKE_SYSTEM_NAME}")
unset(_ft_cfg)
