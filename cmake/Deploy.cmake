# Install rules for the runnable folder:
#   cmake --install build/<preset> --prefix <dir>
#     <dir>/bin                     executables, Qt DLLs and plugins, Qwt, libmodbus, MSVC runtime
#     <dir>/share/modbus-control/   server.example.json (rule in server/CMakeLists.txt)
#     <dir>/licenses/               third-party license texts
# The executables and the Qwt DLL have their own install rules in src/app, server and cmake/Qwt.cmake.
# Test executables and static libraries (mbcore, mbui, mbsim) and headers are not installed.

# MSVC runtime DLLs (vcruntime140.dll, msvcp140.dll, ...) so the folder runs on a clean machine.
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION bin)
include(InstallRequiredSystemLibraries)

# vcpkg prefix (<installed>/<triplet>), derived from the libmodbus import library. The libmodbus config
# imports the .lib, not the DLL, so the DLL is installed explicitly per configuration.
get_target_property(_mc_modbus_implib modbus IMPORTED_LOCATION_RELEASE)
get_filename_component(_mc_vcpkg_lib_dir "${_mc_modbus_implib}" DIRECTORY)
get_filename_component(_mc_vcpkg_prefix "${_mc_vcpkg_lib_dir}" DIRECTORY)

# libmodbus DLL: vcpkg bin/ for Release, debug/bin/ for Debug.
install(FILES "${_mc_vcpkg_prefix}/bin/modbus-5.dll" DESTINATION bin CONFIGURATIONS Release)
install(FILES "${_mc_vcpkg_prefix}/debug/bin/modbus-5.dll" DESTINATION bin CONFIGURATIONS Debug)

# Qt runtime: the official Qt deploy API runs windeployqt on the installed executables at install time.
# The MSVC runtime comes from InstallRequiredSystemLibraries above, so NO_COMPILER_RUNTIME is set.
# Plugins go to bin/ (bin/platforms, bin/styles, ...) next to the executables, not to <prefix>/plugins.
# QT_DEPLOY_PLUGINS_DIR is read when the install script runs, so a small wrapper sets it before including
# the generated script.
function(mc_install_qt_runtime target)
  qt_generate_deploy_app_script(TARGET ${target}
    OUTPUT_SCRIPT _generated_script
    NO_TRANSLATIONS NO_COMPILER_RUNTIME
    DEPLOY_TOOL_OPTIONS --no-opengl-sw)
  set(_wrapper "${CMAKE_CURRENT_BINARY_DIR}/qt_deploy_${target}_install.cmake")
  file(WRITE "${_wrapper}"
    "set(QT_DEPLOY_PLUGINS_DIR \"bin\")\ninclude(\"${_generated_script}\")\n")
  install(SCRIPT "${_wrapper}")
endfunction()

mc_install_qt_runtime(modbus_control)
mc_install_qt_runtime(modbus_test_server)
mc_install_qt_runtime(modbus_loop_sim)

# Third-party licenses. libmodbus (LGPL 2.1) ships its license text in the vcpkg copyright file.
install(FILES "${_mc_vcpkg_prefix}/share/libmodbus/copyright" RENAME COPYING-libmodbus.txt DESTINATION licenses)
