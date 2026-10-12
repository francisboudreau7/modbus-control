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
  AnalysisCsv.h AnalysisCsv.cpp
  SessionIO.h SessionIO.cpp
  EncodingDetect.h EncodingDetect.cpp
)
target_include_directories(mbcore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(mbcore PRIVATE MBCORE_VERSION="${PROJECT_VERSION}")
target_link_libraries(mbcore PUBLIC Qt6::Core modbus PRIVATE mc_warnings)
