# UI classes as a library so widget/model tests can link them.
add_library(mbui STATIC
  MainWindow.h MainWindow.cpp
  AppController.h AppController.cpp
  ConnectionBar.h ConnectionBar.cpp
  TagTableModel.h TagTableModel.cpp
  TagTableWidget.h TagTableWidget.cpp
  WriteController.h WriteController.cpp
  TrendPlot.h TrendPlot.cpp
  PlotCursors.h PlotCursors.cpp
  CursorPanel.h CursorPanel.cpp
  ExportImageDialog.h ExportImageDialog.cpp
  ExportCsvDialog.h ExportCsvDialog.cpp
  AnalysisView.h AnalysisView.cpp
  ClipboardCopy.h ClipboardCopy.cpp
  QuickWritePanel.h QuickWritePanel.cpp
)
target_include_directories(mbui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(mbui PUBLIC mbcore Qt6::Widgets Qt6::Svg Qt6::PrintSupport Qwt::Qwt
                           PRIVATE mc_warnings)

add_executable(modbus_control WIN32 main.cpp)
target_link_libraries(modbus_control PRIVATE mbui mc_warnings)

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

# Install: the GUI executable goes to bin/. Qt and the other runtime DLLs are added by cmake/Deploy.cmake.
install(TARGETS modbus_control RUNTIME DESTINATION bin)