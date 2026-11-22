option(ENABLE_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)

function(enable_sanitizers target)
    if(ENABLE_SANITIZERS AND NOT MSVC)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    endif()
endfunction()

