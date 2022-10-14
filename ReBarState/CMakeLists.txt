# CMakeList.txt : CMake project for ReBarState, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

project ("ReBarState")
set(CMAKE_CXX_STANDARD 17)

add_executable (ReBarState "ReBarState.cpp")

if(WIN32)
    set_target_properties(ReBarState PROPERTIES LINK_FLAGS " /MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\" ")
endif()