cmake_minimum_required(VERSION 3.24)
project(SpinePreview LANGUAGES C CXX)
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
target_compile_definitions(imgui PRIVATE IMGUI_IMPL_OPENGL_ES2)
target_link_libraries(imgui PUBLIC SDL3::SDL3-static ANGLE_GLESv2)
add_executable(SpinePreview MACOSX_BUNDLE Sources/main.cpp)
target_compile_features(SpinePreview PRIVATE cxx_std_17)
target_compile_options(SpinePreview PRIVATE -Wall -Wextra -Werror)
target_link_libraries(SpinePreview PRIVATE imgui ANGLE_EGL)
set_target_properties(SpinePreview PROPERTIES
    OUTPUT_NAME "SpinePreview"
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
        "$<TARGET_BUNDLE_DIR:SpinePreview>/Contents/Resources/Licenses/DearImGui.txt")
