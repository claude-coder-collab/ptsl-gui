add_library(ptslgui_ui
    include/ptslgui/ui/app_settings.hpp
    include/ptslgui/ui/main_window.hpp
    src/app_settings.cpp
    src/command_browser.cpp
    src/command_browser.hpp
    src/form_editor.cpp
    src/form_editor.hpp
    src/history_panel.cpp
    src/history_panel.hpp
    src/main_window.cpp
    src/preferences_dialog.cpp
    src/preferences_dialog.hpp
    src/request_editor.cpp
    src/request_editor.hpp
    src/response_view.cpp
    src/response_view.hpp
    src/sequence_panel.cpp
    src/sequence_panel.hpp)
add_library(ptslgui::ui ALIAS ptslgui_ui)

set_target_properties(ptslgui_ui PROPERTIES AUTOMOC ON)
target_include_directories(ptslgui_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(ptslgui_ui PUBLIC ptslgui::core Qt6::Widgets PRIVATE Qt6::Concurrent nlohmann_json::nlohmann_json)
ptslgui_configure_target(ptslgui_ui)
