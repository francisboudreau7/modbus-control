add_library(mbcore STATIC
  Version.h Version.cpp
  Tag.h Tag.cpp
  TagCodec.h TagCodec.cpp
  SampleBatch.h
  MetaTypes.h MetaTypes.cpp
  ModbusConnection.h ModbusConnection.cpp
  BlockPlanner.h BlockPlanner.cpp
  Recorder.h Recorder.cpp
  PollWorker.h PollWorker.cpp
  ProjectIO.h ProjectIO.cpp
  CsvExporter.h CsvExporter.cpp
  StepAnalysis.h StepAnalysis.cpp
  Tuning.h Tuning.cpp
  AnalysisCsv.h AnalysisCsv.cpp
  SessionIO.h SessionIO.cpp
  EncodingDetect.h EncodingDetect.cpp
  WriteLog.h WriteLog.cpp
  NumberText.h NumberText.cpp
  Language.h Language.cpp
  PidForm.h PidForm.cpp
  ClosedLoopAnalysis.h ClosedLoopAnalysis.cpp
  ClosedLoopSim.h ClosedLoopSim.cpp
)
target_include_directories(mbcore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(mbcore PRIVATE MBCORE_VERSION="${PROJECT_VERSION}")
target_link_libraries(mbcore PUBLIC Qt6::Core modbus PRIVATE mc_warnings)

# v1.3.4 interface translations (see the top-level CMakeLists for update_translations): our
# catalogs plus Qt's own French catalog (standard buttons), embedded as ":/i18n/<name>.qm".
qt_add_lrelease(
  TS_FILES "${PROJECT_SOURCE_DIR}/translations/modbus_control_fr.ts"
           "${PROJECT_SOURCE_DIR}/translations/modbus_control_en.ts"
  QM_FILES_OUTPUT_VARIABLE mc_qm_files
  NO_GLOBAL_TARGET)
set(mc_qtbase_fr "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}/qtbase_fr.qm")
if(NOT EXISTS "${mc_qtbase_fr}")
  message(FATAL_ERROR "Qt's French catalog not found: ${mc_qtbase_fr}")
endif()
foreach(qm IN LISTS mc_qm_files ITEMS "${mc_qtbase_fr}")
  get_filename_component(qm_name "${qm}" NAME)
  set_source_files_properties("${qm}" PROPERTIES QT_RESOURCE_ALIAS "${qm_name}")
endforeach()
qt_add_resources(mbcore "i18n" PREFIX "/i18n" FILES ${mc_qm_files} "${mc_qtbase_fr}")
