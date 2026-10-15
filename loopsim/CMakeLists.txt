# Level loop 101 simulator: process model, Modbus server and scenario files (mbloop), the window
# (mbloopui) and the executable.
add_library(mbloop STATIC
  LoopModel.h LoopModel.cpp
  LoopServer.h LoopServer.cpp
  Scenario.h Scenario.cpp
  LoopController.h LoopController.cpp
)
target_include_directories(mbloop PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(mbloop PUBLIC Qt6::Core modbus PRIVATE mc_warnings)
if(WIN32)
  target_link_libraries(mbloop PUBLIC ws2_32)
endif()
set_target_properties(mbloop PROPERTIES AUTOMOC ON)

add_library(mbloopui STATIC
  LoopWindow.h LoopWindow.cpp
)
target_link_libraries(mbloopui PUBLIC mbloop Qt6::Widgets PRIVATE mc_warnings)
set_target_properties(mbloopui PROPERTIES AUTOMOC ON)

add_executable(modbus_loop_sim WIN32 main.cpp modbus_loop_sim.rc)
target_link_libraries(modbus_loop_sim PRIVATE mbloopui mc_warnings)
target_compile_definitions(modbus_loop_sim PRIVATE MBLOOP_VERSION="${PROJECT_VERSION}")

install(TARGETS modbus_loop_sim RUNTIME DESTINATION bin)
