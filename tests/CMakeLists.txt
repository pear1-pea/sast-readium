# Tests CMakeLists.txt

cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Test Network Concurrent)
find_package(spdlog REQUIRED)
find_package(PkgConfig REQUIRED)

enable_testing()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(COMMON_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/../app
    ${Qt6Core_INCLUDE_DIRS}
    ${Qt6Widgets_INCLUDE_DIRS}
    ${Qt6Test_INCLUDE_DIRS}
)

set(COMMON_DEFINITIONS
    ${Qt6Core_DEFINITIONS}
    ${Qt6Widgets_DEFINITIONS}
    ${Qt6Test_DEFINITIONS}
)

# ---------------------------------------------------------------------------
# Unit test helper  —  lightweight, no viewer/poppler dependency by default
# ---------------------------------------------------------------------------
function(create_unit_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_include_directories(${TEST_NAME} PRIVATE ${COMMON_INCLUDE_DIRS})
    target_link_libraries(${TEST_NAME} PRIVATE
        Qt6::Core Qt6::Widgets Qt6::Test PkgConfig::POPPLER_QT6)
    target_compile_definitions(${TEST_NAME} PRIVATE ${COMMON_DEFINITIONS})
    set_target_properties(${TEST_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/unit)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

# ---------------------------------------------------------------------------
# Integration test helper  —  links full viewer stack (widgets, poppler, …)
# ---------------------------------------------------------------------------
function(create_integration_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_include_directories(${TEST_NAME} PRIVATE ${COMMON_INCLUDE_DIRS})
    target_link_libraries(${TEST_NAME} PRIVATE
        Qt6::Core Qt6::Widgets Qt6::Test Qt6::Network Qt6::Concurrent
        PkgConfig::POPPLER_QT6
        spdlog::spdlog)
    target_compile_definitions(${TEST_NAME} PRIVATE ${COMMON_DEFINITIONS})
    target_sources(${TEST_NAME} PRIVATE
        ../app/ui/viewer/PDFViewer.cpp
        ../app/ui/viewer/PDFOutlineWidget.cpp
        ../app/ui/viewer/PDFRenderCache.cpp
        ../app/ui/viewer/PDFAnimations.cpp
        ../app/ui/viewer/PDFPrerenderer.cpp
        ../app/ui/continuous/PDFContinuousBlueprint.cpp
        ../app/ui/continuous/PDFContinuousCanvas.cpp
        ../app/ui/continuous/PDFContinuousImageCache.cpp
        ../app/ui/continuous/PDFContinuousRenderScheduler.cpp
        ../app/model/AnnotationModel.cpp
        ../app/controller/SearchController.cpp
        ../app/controller/ZoomController.cpp
        ../app/ui/page/PDFPageWidget.cpp
        ../app/model/DocumentModel.cpp
        ../app/model/SearchModel.cpp
        ../app/model/PDFOutlineModel.cpp
        ../app/model/AsyncDocumentLoader.cpp
        ../app/managers/StyleManager.cpp
        ../app/ui/widgets/SearchWidget.cpp
        ../app/utils/Logger.cpp
        ../app/utils/QtSpdlogBridge.cpp
        ../app/utils/LoggingManager.cpp
        ../app/utils/LoggingMacros.cpp
        ../app/utils/LoggingConfig.cpp
    )
    set_target_properties(${TEST_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/integration)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        TIMEOUT 120
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

# ===== Unit Tests =====
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_zoom_controller.cpp)
    create_unit_test(test_zoom_controller
        unit/test_zoom_controller.cpp)
    target_sources(test_zoom_controller PRIVATE
        ../app/controller/ZoomController.cpp)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_search_controller.cpp)
    create_unit_test(test_search_controller
        unit/test_search_controller.cpp)
    target_sources(test_search_controller PRIVATE
        ../app/controller/SearchController.cpp)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_pdf_continuous_blueprint.cpp)
    create_unit_test(test_pdf_continuous_blueprint
        unit/test_pdf_continuous_blueprint.cpp)
    target_sources(test_pdf_continuous_blueprint PRIVATE
        ../app/ui/continuous/PDFContinuousBlueprint.cpp)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/test_pdf_continuous_render_scheduler.cpp)
    create_unit_test(test_pdf_continuous_render_scheduler
        unit/test_pdf_continuous_render_scheduler.cpp)
    target_sources(test_pdf_continuous_render_scheduler PRIVATE
        ../app/ui/continuous/PDFContinuousRenderScheduler.cpp
        ../app/ui/continuous/PDFContinuousImageCache.cpp
        ../app/utils/Logger.cpp)
    target_link_libraries(test_pdf_continuous_render_scheduler PRIVATE
        Qt6::Concurrent spdlog::spdlog)
endif()

# ===== Integration Tests =====
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_pdf_viewer.cpp)
    create_integration_test(test_pdf_viewer
        integration/test_pdf_viewer.cpp)
endif()

# ===== Smoke Test =====
add_executable(smoke_test smoke_test.cpp)
target_include_directories(smoke_test PRIVATE ${COMMON_INCLUDE_DIRS})
target_link_libraries(smoke_test PRIVATE
    Qt6::Core Qt6::Widgets Qt6::Test)
target_compile_definitions(smoke_test PRIVATE ${COMMON_DEFINITIONS})
set_target_properties(smoke_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
add_test(NAME smoke_test COMMAND smoke_test)
set_tests_properties(smoke_test PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# ===== Info =====
message(STATUS "Test Configuration:")
message(STATUS "  Qt6 version: ${Qt6_VERSION}")
message(STATUS "  Poppler-Qt6 found: ${POPPLER_QT6_FOUND}")

add_custom_target(run_all_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS smoke_test
    COMMENT "Running all tests")
