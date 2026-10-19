cmake_minimum_required(VERSION 3.20)
project(EduVM VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(json)

configure_file(src/core/Version.hpp.in ${CMAKE_BINARY_DIR}/generated/eduvm/Version.hpp @ONLY)

add_library(eduvm_core STATIC
    src/core/AiTutor.cpp
    src/core/Grader.cpp
    src/core/GuestAgent.cpp
    src/core/HostInfo.cpp
    src/core/Http.cpp
    src/core/Lab.cpp
    src/core/LabRunner.cpp
    src/core/Paths.cpp
    src/core/Process.cpp
    src/core/Progress.cpp
    src/core/QemuBackend.cpp
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

add_executable(eduvm src/cli/main.cpp)
target_link_libraries(eduvm PRIVATE eduvm_core)

enable_testing()
add_executable(eduvm_tests tests/test_main.cpp)
target_link_libraries(eduvm_tests PRIVATE eduvm_core)
add_test(NAME eduvm_tests COMMAND eduvm_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

install(TARGETS eduvm RUNTIME DESTINATION bin)
install(DIRECTORY labs/ DESTINATION share/eduvm/labs)
