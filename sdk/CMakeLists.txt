add_library(ptslgui_sdk src/sdk_session.cpp)
add_library(ptslgui::sdk ALIAS ptslgui_sdk)

target_include_directories(ptslgui_sdk PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(ptslgui_sdk PUBLIC ptslgui::core PRIVATE PTSLC_CPP::PTSLC_CPP)
ptslgui_configure_target(ptslgui_sdk)
