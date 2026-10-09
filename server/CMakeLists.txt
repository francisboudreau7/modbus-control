# Server logic as a library so unit tests can link it.
add_library(mbsim STATIC
  SignalGen.h SignalGen.cpp
  ServerConfig.h ServerConfig.cpp
  ServerLoop.h ServerLoop.cpp
)
target_include_directories(mbsim PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(mbsim PUBLIC Qt6::Core modbus PRIVATE mc_warnings)
if(WIN32)
  target_link_libraries(mbsim PUBLIC ws2_32)
endif()

add_executable(modbus_test_server main.cpp)
target_link_libraries(modbus_test_server PRIVATE mbsim mc_warnings)
