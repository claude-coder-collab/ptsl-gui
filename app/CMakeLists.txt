set(app_data_dir "${CMAKE_CURRENT_BINARY_DIR}/data")
set(app_catalog "${app_data_dir}/catalog.json")
set(app_proto "${app_data_dir}/PTSL.proto")
ptslgui_generate_catalog("${PTSLGUI_SDK_PROTO}" "${app_catalog}")
add_custom_command(
    OUTPUT "${app_proto}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${PTSLGUI_SDK_PROTO}" "${app_proto}"
    DEPENDS "${PTSLGUI_SDK_PROTO}"
    VERBATIM)

qt_add_executable(ptslgui_app MACOSX_BUNDLE main.cpp)
set_target_properties(ptslgui_app PROPERTIES
    OUTPUT_NAME "PTSL GUI"
    MACOSX_BUNDLE_BUNDLE_NAME "PTSL GUI"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.claude-coder-collab.ptsl-gui"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    BUILD_RPATH "@executable_path/../Frameworks"
    INSTALL_RPATH "@executable_path/../Frameworks")
set_source_files_properties("${app_catalog}" "${app_proto}" PROPERTIES GENERATED TRUE)
qt_add_resources(ptslgui_app ptsl_data
    PREFIX "/ptsl"
    BASE "${app_data_dir}"
    FILES "${app_catalog}" "${app_proto}")
target_link_libraries(ptslgui_app PRIVATE ptslgui::ui ptslgui::sdk)
ptslgui_configure_target(ptslgui_app)

add_custom_command(TARGET ptslgui_app POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:ptslgui_app>/Frameworks"
    COMMAND rm -rf "$<TARGET_BUNDLE_CONTENT_DIR:ptslgui_app>/Frameworks/PTSLC_CPP.framework"
    COMMAND ditto "${PTSLGUI_SDK_FRAMEWORK_ROOT}/PTSLC_CPP.framework"
            "$<TARGET_BUNDLE_CONTENT_DIR:ptslgui_app>/Frameworks/PTSLC_CPP.framework"
    COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:ptslgui_app>"
    COMMENT "Bundling PTSL SDK framework and ad-hoc signing"
    VERBATIM)
