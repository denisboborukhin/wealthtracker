function(enable_strict_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(
            ${target}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wsign-conversion
                -Wshadow
                -Wnon-virtual-dtor
        )
    endif()
endfunction()

function(enable_catch2_compatibility target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # Catch2 uses __COUNTER__, which recent Clang versions diagnose as a
        # future-language extension when expanded at each test declaration.
        target_compile_options(
            ${target}
            PRIVATE
                -Wno-c2y-extensions
                -Wno-missing-designated-field-initializers
        )
    endif()
endfunction()
