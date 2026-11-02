# The M0 build-base smoke target (task T10).
#
# It is deliberately tiny: Qt6 Widgets window + KTextEditor document +
# KSyntaxHighlighting definitions/theme + tree-sitter runtime, each exercised
# once, with the versions it was built against reported at runtime. It proves
# the pinned image and the CMake skeleton work end to end; it is NOT the
# application (src/app is the app-shell lane's deliverable, T13).
add_executable(vedit-m0-smoke qt6_window_smoke.cpp)

target_link_libraries(vedit-m0-smoke PRIVATE
  Qt6::Widgets
  KF6::TextEditor
  KF6::SyntaxHighlighting
  PkgConfig::TREE_SITTER
)

target_compile_definitions(vedit-m0-smoke PRIVATE
  VEDIT_M0_QT_VERSION="${Qt6_VERSION}"
  VEDIT_M0_KF6_VERSION="${KF6TextEditor_VERSION}"
  VEDIT_M0_KSYNTAXHIGHLIGHTING_VERSION="${KF6SyntaxHighlighting_VERSION}"
  VEDIT_M0_TREE_SITTER_VERSION="${VEDIT_TREE_SITTER_VERSION}"
  VEDIT_M0_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata"
)

# This is our own code, so warnings are errors: it is the C++ static check the
# `typecheck` gate runs (the pinned image carries no clang-tidy by design --
# fewer moving parts, and -Werror catches the same class of defect here).
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-m0-smoke PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME vedit-m0-smoke COMMAND vedit-m0-smoke --selfcheck)
set_tests_properties(vedit-m0-smoke PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/xdg-runtime"
  TIMEOUT 120
)

# T61 diagnostic: which syntax definitions can a repository see, and where do
# they come from? Used to prove/refute host-definition isolation of the chosen
# injection form (F1 NO_STANDARD_PATHS=ON vs F2 + XDG_DATA_DIRS). Test-only.
add_executable(vedit-ksh-env-probe ksh_env_probe.cpp)
target_link_libraries(vedit-ksh-env-probe PRIVATE
  Qt6::Widgets
  KF6::TextEditor
  KF6::SyntaxHighlighting
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit-ksh-env-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
