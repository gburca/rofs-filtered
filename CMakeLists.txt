cmake_minimum_required(VERSION 3.1)

project(rofs-filtered VERSION 1.8)

include(GNUInstallDirs)
include(CheckIncludeFile)
# let cmake find our custom FindFUSE script
set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
     "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/InstallIf.cmake)

add_definitions(-D_GNU_SOURCE)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "-Wall")

# find fuse library
find_package (FUSE REQUIRED)
include_directories (${FUSE_INCLUDE_DIR})
add_definitions(-D_FILE_OFFSET_BITS=64)

# generate config file
check_include_file(dirent.h HAVE_DIRENT_H)
configure_file(config.h.in config.h)
add_definitions(-DHAVE_CONFIG_H)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# create and configure targets
add_executable(rofs-filtered rofs-filtered.cpp)
target_link_libraries(rofs-filtered ${FUSE_LIBRARIES})

# configure installation
install(TARGETS rofs-filtered DESTINATION ${CMAKE_INSTALL_BINDIR})
install_if_not_exists(${CMAKE_CURRENT_SOURCE_DIR}/rofs-filtered.rc ${CMAKE_INSTALL_SYSCONFDIR})
install_if_not_exists(${CMAKE_CURRENT_SOURCE_DIR}/rofs-filtered-invert.rc ${CMAKE_INSTALL_SYSCONFDIR})

# unit testing
include( CTest )
enable_testing()
add_subdirectory(test)
