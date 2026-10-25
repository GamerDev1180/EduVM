cmake_minimum_required(VERSION 3.20)
project(EduVM VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    URL https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.5.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(json glfw imgui)
find_package(OpenGL REQUIRED)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

configure_file(src/core/Version.hpp.in ${CMAKE_BINARY_DIR}/generated/eduvm/Version.hpp @ONLY)

add_library(eduvm_core STATIC
    src/core/Diagnostics.cpp
    src/core/HostInfo.cpp
    src/core/Http.cpp
    src/core/Paths.cpp
    src/core/OsDetect.cpp
    src/core/Process.cpp
    src/core/QemuBackend.cpp
    src/core/RfbClient.cpp
    src/core/QmpClient.cpp
    src/core/Settings.cpp
    src/core/Strings.cpp
    src/core/TcpSocket.cpp
    src/core/Updater.cpp
    src/core/VmConfig.cpp
    src/core/VmRepository.cpp
)
target_include_directories(eduvm_core PUBLIC src ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(eduvm_core PUBLIC nlohmann_json::nlohmann_json)

if(WIN32)
    target_compile_definitions(eduvm_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(eduvm_core PUBLIC winhttp ws2_32)
else()
    find_package(CURL REQUIRED)
    target_link_libraries(eduvm_core PUBLIC CURL::libcurl)
endif()

if(MSVC)
    target_compile_options(eduvm_core PUBLIC /W4 /utf-8 /permissive- /EHsc)
else()
    target_compile_options(eduvm_core PUBLIC -Wall -Wextra -Wpedantic)
endif()

add_executable(EduVM WIN32
    src/app/main.cpp
    src/app/App.cpp
    src/app/AppDialogs.cpp
    src/app/AppVm.cpp
    src/app/Keymap.cpp
    src/app/Platform.cpp
    src/app/StatusMonitor.cpp
    src/app/Theme.cpp
    src/app/VmScreen.cpp
)
target_link_libraries(EduVM PRIVATE eduvm_core imgui)
if(WIN32)
    target_link_libraries(EduVM PRIVATE comdlg32 shell32)
    if(MSVC)
        # WIN32 subsystem (no console window) but keep a standard main().
        target_link_options(EduVM PRIVATE /ENTRY:mainCRTStartup)
    endif()
endif()

enable_testing()
add_executable(eduvm_tests tests/test_main.cpp)
target_link_libraries(eduvm_tests PRIVATE eduvm_core)
add_test(NAME eduvm_tests COMMAND eduvm_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

install(TARGETS EduVM RUNTIME DESTINATION bin)
