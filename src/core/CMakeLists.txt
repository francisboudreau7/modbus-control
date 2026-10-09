add_library(mbcore STATIC
  Version.h
  Version.cpp
)
target_include_directories(mbcore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(mbcore PRIVATE MBCORE_VERSION="${PROJECT_VERSION}")
target_link_libraries(mbcore PUBLIC Qt6::Core modbus PRIVATE mc_warnings)