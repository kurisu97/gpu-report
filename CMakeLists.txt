cmake_minimum_required(VERSION 3.16)

project(gpu-report
    VERSION 1.0.0
    DESCRIPTION "Linux GPU Diagnostic & Capability Reporter"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)
pkg_check_modules(NCURSESW REQUIRED IMPORTED_TARGET ncursesw)
pkg_check_modules(LIBXML2 REQUIRED IMPORTED_TARGET libxml-2.0)

set(GPU_REPORT_CORE_SOURCES
    src/core/Strings.cpp
    src/core/Process.cpp
    src/core/Sysfs.cpp
    src/core/SystemInfo.cpp
    src/cli/Arguments.cpp
    src/gpu/Codecs.cpp
    src/gpu/PCIDatabase.cpp
    src/gpu/GPUInfo.cpp
    src/gpu/GPUDetector.cpp
    src/diagnostics/Diagnostics.cpp
    src/diagnostics/FFmpegDetector.cpp
    src/diagnostics/VulkanDetector.cpp
    src/diagnostics/VAAPIDetector.cpp
    src/diagnostics/NvidiaCodecTable.cpp
    src/diagnostics/NVENCDetector.cpp
    src/diagnostics/NVDECDetector.cpp
    src/diagnostics/DiagnosticEngine.cpp
    src/report/Recommendations.cpp
    src/report/ReportGenerator.cpp
    src/localization/Localization.cpp
    src/xml/XMLExporter.cpp)

add_library(gpu_core STATIC ${GPU_REPORT_CORE_SOURCES})

target_include_directories(gpu_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(gpu_core PUBLIC
    PkgConfig::LIBXML2
    ${CMAKE_DL_LIBS})

target_compile_definitions(gpu_core PUBLIC
    GPU_REPORT_BUILD_LOCALES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/locales")

target_compile_options(gpu_core PRIVATE
    -Wall -Wextra -Wpedantic)

add_executable(gpu-report
    src/main.cpp
    src/tui/TUI.cpp)

target_link_libraries(gpu-report PRIVATE
    gpu_core
    PkgConfig::NCURSESW)

target_compile_options(gpu-report PRIVATE
    -Wall -Wextra -Wpedantic)

install(TARGETS gpu-report
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY locales/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/gpu-report/locales)

install(FILES
    README.md
    README.en.md
    gitrd.md
    LICENSE
    DESTINATION ${CMAKE_INSTALL_DOCDIR})

install(DIRECTORY docs/
    DESTINATION ${CMAKE_INSTALL_DOCDIR})

install(DIRECTORY examples/
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples)

include(CTest)

if(BUILD_TESTING)
    add_executable(test_localization tests/test_localization.cpp)
    target_link_libraries(test_localization PRIVATE gpu_core)
    target_compile_options(test_localization PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME localization COMMAND test_localization)

    add_executable(test_xml_export tests/test_xml_export.cpp)
    target_link_libraries(test_xml_export PRIVATE gpu_core)
    target_compile_options(test_xml_export PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME xml_export COMMAND test_xml_export)

    add_executable(test_gpu_detection tests/test_gpu_detection.cpp)
    target_link_libraries(test_gpu_detection PRIVATE gpu_core)
    target_compile_options(test_gpu_detection PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME gpu_detection COMMAND test_gpu_detection)

    add_executable(test_arguments tests/test_arguments.cpp)
    target_link_libraries(test_arguments PRIVATE gpu_core)
    target_compile_options(test_arguments PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME arguments COMMAND test_arguments)

    add_executable(test_recommendations tests/test_recommendations.cpp)
    target_link_libraries(test_recommendations PRIVATE gpu_core)
    target_compile_options(test_recommendations PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME recommendations COMMAND test_recommendations)

    add_executable(test_report tests/test_report.cpp)
    target_link_libraries(test_report PRIVATE gpu_core)
    target_compile_options(test_report PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME report COMMAND test_report)

    add_executable(test_nvidia tests/test_nvidia.cpp)
    target_link_libraries(test_nvidia PRIVATE gpu_core)
    target_compile_options(test_nvidia PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME nvidia COMMAND test_nvidia)
endif()
