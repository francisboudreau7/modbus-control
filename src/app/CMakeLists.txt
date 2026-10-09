add_executable(modbus_control WIN32 main.cpp)
target_link_libraries(modbus_control PRIVATE mbcore Qt6::Widgets Qwt::Qwt mc_warnings)

# Make the exe runnable in place: deploy Qt DLLs and plugins next to it.
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_ROOT}/bin" "${Qt6_DIR}/../../../bin")
if(WINDEPLOYQT_EXECUTABLE)
  add_custom_command(TARGET modbus_control POST_BUILD
    COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            $<IF:$<CONFIG:Debug>,--debug,--release>
            --no-translations --no-compiler-runtime --no-opengl-sw
            --dir "$<TARGET_FILE_DIR:modbus_control>"
            "$<TARGET_FILE:modbus_control>" "$<TARGET_FILE:Qwt::Qwt>"
    COMMENT "Deploying Qt runtime with windeployqt"
    VERBATIM)
endif()