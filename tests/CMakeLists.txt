add_executable(tst_smoke tst_smoke.cpp)
target_link_libraries(tst_smoke PRIVATE mbcore Qt6::Test mc_warnings)
set_target_properties(tst_smoke PROPERTIES AUTOMOC ON)
# Qt6Test is not pulled in by the app's deployment, so deploy it for the test binary.
if(WINDEPLOYQT_EXECUTABLE)
  add_custom_command(TARGET tst_smoke POST_BUILD
    COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            $<IF:$<CONFIG:Debug>,--debug,--release>
            --no-translations --no-compiler-runtime --no-opengl-sw
            --dir "$<TARGET_FILE_DIR:tst_smoke>" "$<TARGET_FILE:tst_smoke>"
    VERBATIM)
endif()
add_test(NAME tst_smoke COMMAND tst_smoke)

add_test(NAME app_smoke COMMAND modbus_control --smoke)
set_tests_properties(app_smoke PROPERTIES TIMEOUT 60)

add_test(NAME server_placeholder COMMAND modbus_test_server)

# Qt DLLs are deployed next to the executables; make sure test processes find them too.
set_tests_properties(tst_smoke app_smoke server_placeholder PROPERTIES
  ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")