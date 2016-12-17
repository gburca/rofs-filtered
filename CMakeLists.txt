cmake_minimum_required(VERSION 3.0)

project(rofs-filtered VERSION 1.6)

include(GNUInstallDirs)
include(CheckIncludeFile)
# let cmake find out custom FindFUSE script
set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
     "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_definitions(-D_GNU_SOURCE)
set(CMAKE_C_FLAGS "-Wall -std=c99")

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
add_executable(rofs-filtered rofs-filtered.c)
target_link_libraries(rofs-filtered ${FUSE_LIBRARIES})

# configure installation
install(TARGETS rofs-filtered DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/rofs-filtered.rc DESTINATION ${CMAKE_INSTALL_SYSCONFDIR})

