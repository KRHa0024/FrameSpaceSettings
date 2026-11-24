cmake_minimum_required(VERSION 3.16)
project(frame-space-settings VERSION 0.1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE MinSizeRel)
endif()
add_compile_options(-Wall -Wextra -ffunction-sections -fdata-sections)
add_compile_definitions(FSS_VERSION="${PROJECT_VERSION}")
option(FSS_BUILD_APP "Build the SteamVR program (needs OpenVR)" ON)

# Pure logic: no OpenVR, tested without SteamVR.
add_library(fsscore STATIC
  src/core/math.cpp src/core/gestures.cpp
  src/core/config.cpp src/core/files.cpp src/core/log.cpp src/core/version.cpp)
target_include_directories(fsscore PUBLIC src)

# Panel drawing: FreeType with a system font, no OpenVR (also used by --screenshot).
find_package(PkgConfig REQUIRED)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
add_library(fssui STATIC src/ui/canvas.cpp src/ui/panel.cpp src/ui/svg.cpp)
target_include_directories(fssui PUBLIC src)
target_link_libraries(fssui PUBLIC fsscore PkgConfig::FREETYPE)

enable_testing()
add_executable(fss-tests tests/tests.cpp)
target_link_libraries(fss-tests PRIVATE fsscore)
add_test(NAME core COMMAND fss-tests)

if(FSS_BUILD_APP)
  set(OPENVR_HEADERS "${CMAKE_SOURCE_DIR}/third_party/openvr" CACHE PATH "openvr.h directory (2.15+: IVRIPCResourceManagerClient)")
  set(OPENVR_LIB "/opt/steamvr/bin/linuxarm64/libopenvr_api.so" CACHE FILEPATH "libopenvr_api.so")
  add_executable(frame-space-settings src/main.cpp src/app.cpp src/chaperone.cpp src/input.cpp src/overlay.cpp src/driver_link.cpp src/updater.cpp)
  target_include_directories(frame-space-settings PRIVATE src ${OPENVR_HEADERS})
  target_link_libraries(frame-space-settings PRIVATE fssui fsscore ${OPENVR_LIB})
  target_link_options(frame-space-settings PRIVATE -Wl,--gc-sections -Wl,--as-needed $<$<CONFIG:MinSizeRel>:-s>)
  get_filename_component(OPENVR_LIB_DIR "${OPENVR_LIB}" DIRECTORY)
  set_target_properties(frame-space-settings PROPERTIES BUILD_RPATH "${OPENVR_LIB_DIR}" INSTALL_RPATH "${OPENVR_LIB_DIR}")
  # The SteamVR driver that applies the move to device poses; loaded by vrserver (OpenVR driver header in third_party).
  set(OPENVR_DRIVER_HEADERS "${CMAKE_SOURCE_DIR}/third_party/openvr" CACHE PATH "openvr_driver.h directory")
  add_library(driver_framespacesettings SHARED src/driver/driver_main.cpp)
  target_include_directories(driver_framespacesettings PRIVATE src)
  target_include_directories(driver_framespacesettings SYSTEM PRIVATE ${OPENVR_DRIVER_HEADERS})
  set_target_properties(driver_framespacesettings PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden)
  target_link_options(driver_framespacesettings PRIVATE -Wl,--gc-sections $<$<CONFIG:MinSizeRel>:-s>)
  add_executable(fss-driver-probe tools/driver_probe.cpp)
  target_include_directories(fss-driver-probe PRIVATE src ${OPENVR_HEADERS})
  target_link_libraries(fss-driver-probe PRIVATE ${OPENVR_LIB})
  set_target_properties(fss-driver-probe PROPERTIES BUILD_RPATH "${OPENVR_LIB_DIR}")

  # The program finds its manifests next to itself in the build tree.
  add_custom_command(TARGET frame-space-settings POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:frame-space-settings>/data)
endif()
