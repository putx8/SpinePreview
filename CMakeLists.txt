cmake_minimum_required(VERSION 3.24)
project(SpinePreview LANGUAGES C CXX OBJCXX)
set(SPINE_RELEASE_VERSION "1.0.0" CACHE STRING "Application release version, independent of the Spine runtime")
set(SPINE_BUILD_NUMBER "1" CACHE STRING "Numeric application build number")
if(NOT SPINE_RELEASE_VERSION MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)(-[0-9A-Za-z.-]+)?$")
    message(FATAL_ERROR "SPINE_RELEASE_VERSION must be MAJOR.MINOR.PATCH with an optional prerelease suffix")
endif()
set(SPINE_BUNDLE_VERSION "${CMAKE_MATCH_1}")
if(NOT SPINE_BUILD_NUMBER MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "SPINE_BUILD_NUMBER must be a positive integer")
endif()
set(SPINE_VERSION "all" CACHE STRING "Spine runtime to build: all, 2.1, or 3.8")
set_property(CACHE SPINE_VERSION PROPERTY STRINGS all 2.1 3.8)
if(NOT SPINE_VERSION MATCHES "^(all|2\\.1|3\\.8)$")
    message(FATAL_ERROR "SPINE_VERSION must be all, 2.1, or 3.8")
endif()
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
set_source_files_properties(Sources/AppProcess.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
set(SPINE_APP_ICON "${CMAKE_SOURCE_DIR}/Assets/AppIcon/AppIcon.icns")
set_source_files_properties("${SPINE_APP_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
option(SPINE_BUILD_TESTS "Build resource loading regression tests" OFF)
if(SPINE_BUILD_TESTS)
    enable_testing()
endif()

function(add_spine_preview version suffix adapter runtime_dir sample_dir sample_json)
    set(SPINE_APP_NAME "SpinePreview${version}")
    set(SPINE_BUNDLE_ID "local.spinepreview.v${suffix}")
    if(version STREQUAL "3.8")
        set(target SpinePreview)
        set(resources resource_files)
        file(GLOB runtime_sources CONFIGURE_DEPENDS "${runtime_dir}/src/spine/*.cpp")
    else()
        set(target SpinePreview21)
        set(resources resource_files21)
        file(GLOB runtime_sources CONFIGURE_DEPENDS "${runtime_dir}/src/spine/*.c")
    endif()
    add_library(spine${suffix} STATIC ${runtime_sources})
    target_include_directories(spine${suffix} SYSTEM PUBLIC "${runtime_dir}/include")
    if(version STREQUAL "3.8")
        target_compile_features(spine${suffix} PUBLIC cxx_std_11)
    endif()
    add_library(${resources} STATIC Sources/ResourceFiles.mm)
    target_compile_options(${resources} PRIVATE -fobjc-arc)
    target_include_directories(${resources} PUBLIC "${CMAKE_SOURCE_DIR}/Sources")
    target_compile_features(${resources} PUBLIC cxx_std_17)
    target_compile_definitions(${resources} PUBLIC SPINE_RUNTIME_VERSION="${version}")
    target_link_libraries(${resources} PUBLIC "-framework Foundation" "-framework ImageIO")
    add_executable(${target} MACOSX_BUNDLE Sources/main.cpp Sources/FileLoader.cpp Sources/AppProcess.mm
        Sources/PreviewTexture.cpp "Sources/Runtimes/${adapter}.cpp" "${SPINE_APP_ICON}")
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    target_compile_definitions(${target} PRIVATE SPINE_APP_NAME="${SPINE_APP_NAME}" SPINE_SAMPLE_JSON="${sample_json}")
    target_link_libraries(${target} PRIVATE imgui ANGLE_EGL spine${suffix} ${resources} "-framework CoreGraphics" "-framework AppKit")
    configure_file("${CMAKE_SOURCE_DIR}/Sources/Info.plist"
        "${CMAKE_CURRENT_BINARY_DIR}/Info${suffix}.plist" @ONLY)
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${SPINE_APP_NAME}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info${suffix}.plist"
        BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "@executable_path/../Frameworks")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_DIR:${target}>/Contents/Frameworks"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${ANGLE_DIR}/lib/libEGL.dylib" "${ANGLE_DIR}/lib/libGLESv2.dylib"
            "$<TARGET_BUNDLE_DIR:${target}>/Contents/Frameworks"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_DIR:${target}>/Contents/Resources/Licenses"
        COMMAND "${CMAKE_COMMAND}" -E copy "${SDL3_SOURCE_DIR}/LICENSE.txt"
            "$<TARGET_BUNDLE_DIR:${target}>/Contents/Resources/Licenses/SDL3.txt"
        COMMAND "${CMAKE_COMMAND}" -E copy "${imgui_SOURCE_DIR}/LICENSE.txt"
            "$<TARGET_BUNDLE_DIR:${target}>/Contents/Resources/Licenses/DearImGui.txt"
        COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/Vendor/Spine${suffix}/LICENSE"
            "$<TARGET_BUNDLE_DIR:${target}>/Contents/Resources/Licenses/Spine.txt"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${sample_dir}"
            "$<TARGET_BUNDLE_DIR:${target}>/Contents/Resources/spineboy")
    if(SPINE_BUILD_TESTS)
        if(version STREQUAL "3.8")
            add_executable(resource_files_tests tests/resource_files_tests.cpp)
            target_link_libraries(resource_files_tests PRIVATE ${resources} SDL3::SDL3-static)
            add_test(NAME resource_files COMMAND resource_files_tests "${sample_dir}" "$<TARGET_FILE:${target}>")
        else()
            add_executable(runtime21_tests tests/runtime21_tests.cpp)
            target_compile_definitions(runtime21_tests PRIVATE SPINE_APP_NAME="${SPINE_APP_NAME}")
            target_link_libraries(runtime21_tests PRIVATE ${resources} SDL3::SDL3-static)
            add_test(NAME runtime21 COMMAND runtime21_tests "${sample_dir}" "$<TARGET_FILE:${target}>"
                "${CMAKE_SOURCE_DIR}/Vendor/Spine21/spine-cocos2dx/3/example/Resources/common")
        endif()
    endif()
endfunction()
if(SPINE_VERSION STREQUAL "all" OR SPINE_VERSION STREQUAL "3.8")
    add_spine_preview("3.8" "38" "Spine38"
        "${CMAKE_SOURCE_DIR}/Vendor/Spine38/spine-cpp/spine-cpp"
        "${CMAKE_SOURCE_DIR}/Vendor/Spine38/examples/spineboy/export" "spineboy-pro.json")
endif()
if(SPINE_VERSION STREQUAL "all" OR SPINE_VERSION STREQUAL "2.1")
    add_spine_preview("2.1" "21" "Spine21"
        "${CMAKE_SOURCE_DIR}/Vendor/Spine21/spine-c"
        "${CMAKE_SOURCE_DIR}/Vendor/Spine21/spine-c/data" "spineboy.json")
endif()
