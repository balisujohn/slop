cmake_minimum_required(VERSION 3.10)
project(slop)

set(SD_BUILD_SHARED_LIB ON)

set(TARGET slop)

set(SD_USE_CUBLAS ON)


# Set the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define the source directories
set(IMGUI_DIR ./)
set(SOURCES
    main.cpp
    ${IMGUI_DIR}/imgui/imgui.cpp
    ${IMGUI_DIR}/imgui/imgui_demo.cpp
    ${IMGUI_DIR}/imgui/imgui_draw.cpp
    ${IMGUI_DIR}/imgui/imgui_tables.cpp
    ${IMGUI_DIR}/imgui/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp

)

# Add the executable target
add_executable(slop ${SOURCES})

# Specify include directories
target_include_directories(slop PRIVATE
    ${IMGUI_DIR}/imgui
    ${IMGUI_DIR}/backends
    ${IMGUI_DIR}/stable-diffusion.cpp
    ${IMGUI_DIR}
)

# Find and link GLFW library
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLFW REQUIRED glfw3)
    add_subdirectory(stable-diffusion.cpp)
    target_link_libraries(slop PRIVATE ${GLFW_LIBRARIES} -lGL)
    target_link_libraries(slop PRIVATE stable-diffusion)

    target_include_directories(slop PRIVATE ${GLFW_INCLUDE_DIRS})
elseif (APPLE)
    find_package(glfw3 REQUIRED)
    target_link_libraries(slop PRIVATE glfw)
    target_link_libraries(slop PRIVATE "-framework OpenGL")
elseif (MSYS)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLFW REQUIRED glfw3)
    target_link_libraries(slop PRIVATE ${GLFW_LIBRARIES} -lGL)
    target_include_directories(slop PRIVATE ${GLFW_INCLUDE_DIRS})
endif()

# Add a custom target to clean build artifacts
add_custom_target(clean-all
    COMMAND ${CMAKE_BUILD_TOOL} clean
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/Clean.cmake
)


