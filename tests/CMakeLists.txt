# mc_add_qtest(<name> LIBS <libs...>): QtTest executable from <name>.cpp, registered with CTest.
function(mc_add_qtest name)
  cmake_parse_arguments(ARG "" "" "LIBS" ${ARGN})
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE Qt6::Test mc_warnings ${ARG_LIBS})
  add_test(NAME ${name} COMMAND ${name})
  # Qt DLLs are deployed next to the executables; make sure test processes find them.
  set_tests_properties(${name} PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    TIMEOUT 120)
endfunction()

mc_add_qtest(tst_smoke LIBS mbcore)
# Qt6Test is not pulled in by the app's deployment, so deploy it for the test binaries.
if(WINDEPLOYQT_EXECUTABLE)
  add_custom_command(TARGET tst_smoke POST_BUILD
    COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            $<IF:$<CONFIG:Debug>,--debug,--release>
            --no-translations --no-compiler-runtime --no-opengl-sw
            --dir "$<TARGET_FILE_DIR:tst_smoke>" "$<TARGET_FILE:tst_smoke>"
    VERBATIM)
endif()

# Core unit tests
mc_add_qtest(tst_tagcodec LIBS mbcore)
mc_add_qtest(tst_blockplanner LIBS mbcore)
mc_add_qtest(tst_recorder LIBS mbcore)
mc_add_qtest(tst_projectio LIBS mbcore)
mc_add_qtest(tst_csvexporter LIBS mbcore)

# Test server unit tests
mc_add_qtest(tst_signalgen LIBS mbsim)
mc_add_qtest(tst_serverconfig LIBS mbsim)

# Integration: starts modbus_test_server and talks to it through mbcore.
mc_add_qtest(tst_integration LIBS mbcore)
target_compile_definitions(tst_integration PRIVATE
  MC_TEST_SERVER_PATH="$<TARGET_FILE:modbus_test_server>")
add_dependencies(tst_integration modbus_test_server)

add_test(NAME app_smoke COMMAND modbus_control --smoke)
add_test(NAME server_placeholder COMMAND modbus_test_server)
set_tests_properties(app_smoke server_placeholder PROPERTIES
  ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
  TIMEOUT 60)
