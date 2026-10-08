function(ptslgui_configure_target target)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast
        -Wnon-virtual-dtor -Woverloaded-virtual -Wcast-align -Wnull-dereference -Wdouble-promotion
        -Wimplicit-fallthrough -Wformat=2)
    if(PTSLGUI_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE -Werror)
    endif()

    if(PTSLGUI_SANITIZE)
        string(REPLACE "," ";" sanitizers "${PTSLGUI_SANITIZE}")
        list(JOIN sanitizers "," joined)
        target_compile_options(${target} PRIVATE -fsanitize=${joined} -fno-omit-frame-pointer -fno-sanitize-recover=all)
        target_link_options(${target} PRIVATE -fsanitize=${joined})
    endif()
endfunction()
