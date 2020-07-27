#  Copyright (c) Alan Ludwig. All rights reserved.
#  Licensed under the MIT License.

cmake_minimum_required(VERSION 3.10)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)


# set the project name
project(bubbles)

# add the executable
add_executable(${PROJECT_NAME}  
    src/main.cc
    inc/eventloop_timer_utilities.h
    src/eventloop_timer_utilities.c
)

target_include_directories(${PROJECT_NAME} PRIVATE inc)


azsphere_configure_tools(TOOLS_REVISION "20.04")
azsphere_configure_api(TARGET_API_SET "5+Beta2004")


TARGET_LINK_LIBRARIES(${PROJECT_NAME} applibs pthread gcc_s c)

azsphere_target_add_image_package(${PROJECT_NAME})

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")