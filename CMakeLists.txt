#  Copyright (c) Alan Ludwig. All rights reserved.
#  Licensed under the MIT License.

cmake_minimum_required(VERSION 3.10)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)


# set the project name
project(bubbles)

azsphere_configure_tools(TOOLS_REVISION "20.04")
azsphere_configure_api(TARGET_API_SET "5+Beta2004")

# add the executable
add_executable(${PROJECT_NAME}  
    src/main.c
    inc/eventloop_timer_utilities.h
    src/eventloop_timer_utilities.c
    inc/parson.h
    src/parson.c
)

target_include_directories(${PROJECT_NAME} PRIVATE inc)
target_include_directories(${PROJECT_NAME} PUBLIC ${AZURE_SPHERE_API_SET_DIR}/usr/include/azureiot)
target_compile_definitions(${PROJECT_NAME} PUBLIC AZURE_IOT_HUB_CONFIGURED)
TARGET_LINK_LIBRARIES(${PROJECT_NAME} m azureiot applibs pthread gcc_s c)


find_program(POWERSHELL powershell.exe)

if (POWERSHELL)
    # Run validate_manifest script during build
    add_custom_target(ValidateManifest ALL
        COMMAND ${POWERSHELL} -ExecutionPolicy Bypass -NoProfile -NonInteractive -File ${CMAKE_SOURCE_DIR}/script/validate_manifest.ps1
        DEPENDS ${CMAKE_SOURCE_DIR}/app_manifest.json)
else()
    # Warn users without PowerShell to update their manifest
    add_custom_target(ValidateManifest ALL
        COMMAND echo "Please ensure that you have updated app_manifest.json as described in IoTCentral.md or IoTHub.md, as appropriate."
        DEPENDS ${CMAKE_SOURCE_DIR}/app_manifest.json)
endif()

azsphere_target_add_image_package(${PROJECT_NAME})

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")