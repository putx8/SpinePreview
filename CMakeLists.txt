cmake_minimum_required(VERSION 3.24)
project(SpinePreview LANGUAGES C CXX OBJCXX)
set(SPINE_VERSION "3.8" CACHE STRING "Spine runtime version")
if(NOT SPINE_VERSION STREQUAL "3.8")
    message(FATAL_ERROR "Only the bundled Spine 3.8 runtime is integrated in this build.")
endif()
set(SPINE_APP_NAME "SpinePreview${SPINE_VERSION}")
if(NOT APPLE)
    message(FATAL_ERROR "This first build uses macOS ANGLE libraries. Windows/Linux integration is not implemented yet.")
endif()
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
# SDL owns the window and events. All GL calls go through ANGLE.
foreach(subsystem OPENGL OPENGLES VULKAN AUDIO JOYSTICK HAPTIC SENSOR CAMERA RENDER GPU)
    set(SDL_${subsystem} OFF CACHE BOOL "" FORCE)
endforeach()
# Dependencies are checked into the repository. Configuration never fetches them.
set(SDL3_SOURCE_DIR "${CMAKE_SOURCE_DIR}/Vendor/SDL3")
set(imgui_SOURCE_DIR "${CMAKE_SOURCE_DIR}/Vendor/imgui")
add_subdirectory("${SDL3_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/vendor/SDL3" EXCLUDE_FROM_ALL)
set(ANGLE_DIR "${CMAKE_SOURCE_DIR}/Vendor/ANGLE")
foreach(library EGL GLESv2)
    add_library(ANGLE_${library} SHARED IMPORTED)
    set_target_properties(ANGLE_${library} PROPERTIES
        IMPORTED_LOCATION "${ANGLE_DIR}/lib/lib${library}.dylib"
        INTERFACE_INCLUDE_DIRECTORIES "${ANGLE_DIR}/include")
endforeach()
add_library(imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp" "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp" "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp")
target_include_directories(imgui PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
target_include_directories(imgui PUBLIC "${CMAKE_SOURCE_DIR}/Sources")
target_compile_definitions(imgui PRIVATE IMGUI_IMPL_OPENGL_ES2)
target_compile_definitions(imgui PUBLIC IMGUI_USER_CONFIG="SpineImGuiConfig.h")
target_link_libraries(imgui PUBLIC SDL3::SDL3-static ANGLE_GLESv2)
set(SPINE_DIR "${CMAKE_SOURCE_DIR}/Vendor/Spine38/spine-cpp/spine-cpp")
file(GLOB SPINE_SOURCES CONFIGURE_DEPENDS "${SPINE_DIR}/src/spine/*.cpp")
add_library(spine38 STATIC ${SPINE_SOURCES})
target_include_directories(spine38 SYSTEM PUBLIC "${SPINE_DIR}/include")
target_compile_features(spine38 PUBLIC cxx_std_11)
add_library(resource_files STATIC Sources/ResourceFiles.mm)
set_source_files_properties(Sources/ResourceFiles.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_include_directories(resource_files PUBLIC "${CMAKE_SOURCE_DIR}/Sources")
target_compile_features(resource_files PUBLIC cxx_std_17)
target_link_libraries(resource_files PUBLIC "-framework Foundation" "-framework ImageIO")
add_executable(SpinePreview MACOSX_BUNDLE Sources/main.cpp Sources/SpinePreview.cpp Sources/FileLoader.cpp)
target_compile_features(SpinePreview PRIVATE cxx_std_17)
target_compile_options(SpinePreview PRIVATE -Wall -Wextra -Werror)
target_link_libraries(SpinePreview PRIVATE imgui ANGLE_EGL spine38 resource_files "-framework CoreGraphics")
set_target_properties(SpinePreview PROPERTIES
    OUTPUT_NAME "${SPINE_APP_NAME}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Sources/Info.plist"
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "@executable_path/../Frameworks")
add_custom_command(TARGET SpinePreview POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_DIR:SpinePreview>/Contents/Frameworks"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${ANGLE_DIR}/lib/libEGL.dylib" "${ANGLE_DIR}/lib/libGLESv2.dylib"
        "$<TARGET_BUNDLE_DIR:SpinePreview>/Contents/Frameworks"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_DIR:SpinePreview>/Contents/Resources/Licenses"
    COMMAND "${CMAKE_COMMAND}" -E copy "${SDL3_SOURCE_DIR}/LICENSE.txt"
        "$<TARGET_BUNDLE_DIR:SpinePreview>/Contents/Resources/Licenses/SDL3.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy "${imgui_SOURCE_DIR}/LICENSE.txt"
        "$<TARGET_BUNDLE_DIR:SpinePreview>/Contents/Resources/Licenses/DearImGui.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/Vendor/Spine38/LICENSE"
        "$<TARGET_BUNDLE_DIR:SpinePreview>/Contents/Resources/Licenses/Spine.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/Vendor/Spine38/examples/spineboy/export"
        "$<TARGET_BUNDLE_DIR:SpinePreview>/Contents/Resources/spineboy")

option(SPINE_BUILD_TESTS "Build resource loading regression tests" OFF)
if(SPINE_BUILD_TESTS)
    enable_testing()
    add_executable(resource_files_tests tests/resource_files_tests.cpp)
    target_link_libraries(resource_files_tests PRIVATE resource_files SDL3::SDL3-static)
    add_test(NAME resource_files COMMAND resource_files_tests
        "${CMAKE_SOURCE_DIR}/Vendor/Spine38/examples/spineboy/export" "$<TARGET_FILE:SpinePreview>")
endif()
