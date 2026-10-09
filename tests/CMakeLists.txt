# mc_add_qtest(<name> LIBS <libs...>): QtTest executable from <name>.cpp, registered with CTest.
function(mc_add_qtest name)
  cmake_parse_arguments(ARG "" "" "LIBS" ${ARGN})
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE Qt6::Test mc_warnings ${ARG_LIBS})
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES
    ENVIRONMENT_MODIFICATION "${MC_TEST_PATH}"
    TIMEOUT 120)
endfunction()

# Test processes find our DLLs in bin/ and Qt's DLLs in the Qt install, even when only a
# single test target was built (windeployqt has not run).
set(MC_TEST_PATH
  "PATH=path_list_prepend:${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}"
  "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>bin"
  "PATH=path_list_prepend:${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

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
add_test(NAME server_version COMMAND modbus_test_server --version)
set_tests_properties(app_smoke server_version PROPERTIES
  ENVIRONMENT_MODIFICATION "${MC_TEST_PATH}"
  TIMEOUT 60)
