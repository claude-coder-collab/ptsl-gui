find_package(Catch2 3 REQUIRED)
include(Catch)

set(fixture_catalog "${CMAKE_CURRENT_BINARY_DIR}/fixture_catalog.json")
ptslgui_generate_catalog("${PTSLGUI_FIXTURE_PROTO}" "${fixture_catalog}")
set(test_data_sources "${fixture_catalog}")
set(test_definitions
    PTSLGUI_FIXTURE_PROTO="${PTSLGUI_FIXTURE_PROTO}"
    PTSLGUI_FIXTURE_CATALOG="${fixture_catalog}")

if(PTSLGUI_SDK_PROTO)
    set(sdk_catalog "${CMAKE_CURRENT_BINARY_DIR}/sdk_catalog.json")
    ptslgui_generate_catalog("${PTSLGUI_SDK_PROTO}" "${sdk_catalog}")
    list(APPEND test_data_sources "${sdk_catalog}")
    list(APPEND test_definitions PTSLGUI_SDK_PROTO="${PTSLGUI_SDK_PROTO}" PTSLGUI_SDK_CATALOG="${sdk_catalog}")
endif()

add_custom_target(ptslgui_test_data DEPENDS ${test_data_sources})

add_executable(ptslgui_core_tests
    core/test_catalog.cpp
    core/test_controller.cpp
    core/test_fake_session.cpp
    core/test_history.cpp
    core/test_protocol.cpp
    core/test_sample.cpp
    core/test_schema.cpp
    core/test_sequence.cpp
    core/test_time_format.cpp
    core/test_sdk_proto.cpp
    core/test_version.cpp)
add_dependencies(ptslgui_core_tests ptslgui_test_data)
target_include_directories(ptslgui_core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/core")
target_compile_definitions(ptslgui_core_tests PRIVATE ${test_definitions})
target_link_libraries(ptslgui_core_tests PRIVATE ptslgui::core Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
ptslgui_configure_target(ptslgui_core_tests)
catch_discover_tests(ptslgui_core_tests)

if(TARGET ptslgui_ui)
    add_executable(ptslgui_ui_tests ui/main.cpp ui/test_form_editor.cpp ui/test_history_settings.cpp ui/test_main_window.cpp
        ui/test_sequence_panel.cpp)
    add_dependencies(ptslgui_ui_tests ptslgui_test_data)
    target_include_directories(ptslgui_ui_tests PRIVATE "${PROJECT_SOURCE_DIR}/ui/src")
    target_compile_definitions(ptslgui_ui_tests PRIVATE ${test_definitions})
    target_link_libraries(ptslgui_ui_tests PRIVATE ptslgui::ui Qt6::Test Catch2::Catch2 nlohmann_json::nlohmann_json)
    ptslgui_configure_target(ptslgui_ui_tests)
    catch_discover_tests(ptslgui_ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(TARGET ptslgui_sdk)
    add_executable(ptslgui_sdk_tests sdk/test_sdk_session.cpp)
    target_compile_definitions(ptslgui_sdk_tests PRIVATE ${test_definitions})
    target_link_libraries(ptslgui_sdk_tests PRIVATE ptslgui::sdk Catch2::Catch2WithMain)
    set_target_properties(ptslgui_sdk_tests PROPERTIES BUILD_RPATH "${PTSLGUI_SDK_FRAMEWORK_ROOT}")
    ptslgui_configure_target(ptslgui_sdk_tests)
    catch_discover_tests(ptslgui_sdk_tests)
endif()
