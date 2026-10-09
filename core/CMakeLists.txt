add_library(ptslgui_core
    src/catalog.cpp
    src/controller.cpp
    src/fake_session.cpp
    src/history.cpp
    src/protocol.cpp
    src/sample.cpp
    src/schema.cpp
    src/time_format.cpp
    src/version.cpp)
add_library(ptslgui::core ALIAS ptslgui_core)

target_include_directories(ptslgui_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(ptslgui_core
    PRIVATE protobuf::libprotobuf protobuf::libprotoc nlohmann_json::nlohmann_json)
ptslgui_configure_target(ptslgui_core)
