# Evidence harness for T9 (diagnostics-presentation).
#
# It compiles the SAME product sources the lane builds - the panel + outline
# model + controller + toolchain + the shared jump registry, and the editor's
# diagnostics_annotations - and drives them headless (offscreen) with fake
# backends. Nothing here is product code: it is the reproducible evidence
# toolchain for AC-5.1..5.4, AC-6.1..6.4, AC-7.1/7.2 and AC-10.3.3.
#
#   cmake -S evidence/d-diagnostics/harness -B target/d-diagnostics-harness -G Ninja
#   cmake --build target/d-diagnostics-harness
#   QT_QPA_PLATFORM=offscreen target/d-diagnostics-harness/panel-probe evidence/d-diagnostics
#   QT_QPA_PLATFORM=offscreen target/d-diagnostics-harness/annotations-probe evidence/d-diagnostics
cmake_minimum_required(VERSION 3.25)
project(vedit_d_diagnostics_harness LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

# Workspace root: the directory that contains src/. Overridable for a checkout
# that is not this evidence tree's grandparent.
set(VEDIT_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." CACHE PATH "workspace root")
get_filename_component(VEDIT_SOURCE_ROOT "${VEDIT_SOURCE_ROOT}" ABSOLUTE)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(Qt6 REQUIRED COMPONENTS Test)
find_package(KF6TextEditor REQUIRED)

# The panel lane sources under test (compiled here with the same semantics as the
# product: the editor lane is present, so the shared severity palette is used).
add_library(harness_diagnostics STATIC
  "${VEDIT_SOURCE_ROOT}/src/workspace/diagnostics/diagnostics_controller.cpp"
  "${VEDIT_SOURCE_ROOT}/src/workspace/diagnostics/diagnostics_outline_model.cpp"
  "${VEDIT_SOURCE_ROOT}/src/workspace/diagnostics/diagnostics_panel.cpp"
  "${VEDIT_SOURCE_ROOT}/src/workspace/diagnostics/toolchain.cpp"
  "${VEDIT_SOURCE_ROOT}/src/workspace/jump/jump_registry.cpp"
)
target_compile_definitions(harness_diagnostics PUBLIC VEDIT_DIAGNOSTICS_HAVE_ANNOTATIONS=1)
target_include_directories(harness_diagnostics PUBLIC
  "${VEDIT_SOURCE_ROOT}/src/workspace/diagnostics"
  "${VEDIT_SOURCE_ROOT}/src/workspace"
  "${VEDIT_SOURCE_ROOT}/src/editor/kernel"
)
target_link_libraries(harness_diagnostics PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(harness_diagnostics PRIVATE -Wall -Wextra)
endif()

add_executable(panel-probe panel_probe.cpp)
target_link_libraries(panel-probe PRIVATE harness_diagnostics Qt6::Test)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(panel-probe PRIVATE -Wall -Wextra)
endif()

# The inline squiggle lives in the editor kernel; compile that ONE product source
# (the same file the product build compiles) against the real KTextEditor.
add_executable(annotations-probe
  annotations_probe.cpp
  "${VEDIT_SOURCE_ROOT}/src/editor/kernel/diagnostics_annotations.cpp"
)
target_include_directories(annotations-probe PRIVATE
  "${VEDIT_SOURCE_ROOT}/src/editor/kernel")
target_link_libraries(annotations-probe PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets KF6::TextEditor)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(annotations-probe PRIVATE -Wall -Wextra)
endif()
