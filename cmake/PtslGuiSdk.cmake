set(PTSLGUI_FIXTURE_PROTO "${PROJECT_SOURCE_DIR}/tests/fixtures/proto/fixture.proto")

file(GLOB _ptslgui_sdk_dirs LIST_DIRECTORIES true "${PROJECT_SOURCE_DIR}/PTSL_SDK_CPP.*")
list(SORT _ptslgui_sdk_dirs COMPARE NATURAL ORDER DESCENDING)
set(_ptslgui_default_sdk "")
foreach(dir IN LISTS _ptslgui_sdk_dirs)
    if(IS_DIRECTORY "${dir}")
        set(_ptslgui_default_sdk "${dir}")
        break()
    endif()
endforeach()

set(PTSL_SDK_DIR "${_ptslgui_default_sdk}" CACHE PATH "PTSL SDK directory (empty when the SDK is not available)")

set(PTSLGUI_SDK_PROTO "")
set(PTSLGUI_SDK_FOUND OFF)
if(PTSL_SDK_DIR AND EXISTS "${PTSL_SDK_DIR}/Source/PTSL.proto")
    set(PTSLGUI_SDK_PROTO "${PTSL_SDK_DIR}/Source/PTSL.proto")
    set(PTSLGUI_SDK_FRAMEWORK_ROOT "${PTSL_SDK_DIR}/install/arm64/Release/PTSLC_CPP")
    find_package(PTSLC_CPP CONFIG QUIET PATHS "${PTSLGUI_SDK_FRAMEWORK_ROOT}" NO_DEFAULT_PATH)
    if(PTSLC_CPP_FOUND)
        set(PTSLGUI_SDK_FOUND ON)
        message(STATUS "PTSL SDK: ${PTSL_SDK_DIR} (client framework found)")
    else()
        message(STATUS "PTSL SDK: ${PTSL_SDK_DIR} (client framework not built; run tools/build_sdk.py)")
    endif()
else()
    message(STATUS "PTSL SDK: not found, SDK-dependent parts disabled")
endif()

function(ptslgui_generate_catalog proto output)
    add_custom_command(
        OUTPUT "${output}"
        COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/tools/gen_catalog.py" --proto "${proto}" --output "${output}" --quiet
        DEPENDS "${proto}" "${PROJECT_SOURCE_DIR}/tools/gen_catalog.py"
        COMMENT "Generating command catalog ${output}"
        VERBATIM)
endfunction()
