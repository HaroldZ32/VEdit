# Probes for T14 (edit-core). Standalone only; NOT part of the product build.
cmake_minimum_required(VERSION 3.20)
project(vedit_editor_probes LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
# Ubuntu ships per-framework configs, no umbrella KF6Config.cmake
find_package(KF6TextEditor REQUIRED)
find_package(KF6CoreAddons REQUIRED)

foreach(p tx_probe tx_steps_probe tx_steps_probe2 tx_steps_probe3 tx_steps_probe4 tx_steps_probe5 replace_probe replace_probe2 fsync_probe)
  add_executable(${p} ${p}.cpp)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  target_link_libraries(${p} PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test KF6::TextEditor KF6::CoreAddons)
  target_compile_options(${p} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()
