# Qt requires versionless OpenSSL library filenames on Android. Only ELF dynamic
# names change in the staging copies; the Conan package remains untouched.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
string(TOUPPER "${CMAKE_BUILD_TYPE}" android_dependency_configuration)
list(GET openssl_LIB_DIRS_${android_dependency_configuration} 0 android_openssl_directory)
set(android_tls_directory "${PROJECT_BINARY_DIR}/android-tls")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/android/tools/stage_tls.py"
        "${android_openssl_directory}" "${android_tls_directory}"
    COMMAND_ERROR_IS_FATAL ANY
)
foreach(library IN ITEMS crypto ssl)
    foreach(component IN ITEMS openssl openssl_OpenSSL_Crypto openssl_OpenSSL_SSL)
        set(tls_target "CONAN_LIB::${component}_${library}_${android_dependency_configuration}")
        if(TARGET "${tls_target}")
            set_target_properties("${tls_target}" PROPERTIES
                IMPORTED_LOCATION_${android_dependency_configuration} "${android_tls_directory}/lib${library}_3.so"
                IMPORTED_SONAME_${android_dependency_configuration} "lib${library}_3.so"
            )
        endif()
    endforeach()
endforeach()
set(android_tls_libraries "${android_tls_directory}/libcrypto_3.so;${android_tls_directory}/libssl_3.so")
