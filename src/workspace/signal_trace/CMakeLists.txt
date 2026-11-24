if(NOT (TARGET vd_ffi AND TARGET KF6::TextEditor AND TARGET vedit_workspace_project AND TARGET vedit_workspace_jump))
  return()
endif()

add_library(vedit_workspace_signal_trace STATIC signal_trace_integration.cpp trace_dialog.cpp)
target_link_libraries(vedit_workspace_signal_trace PRIVATE
  vd_ffi KF6::TextEditor Qt6::Widgets vedit_app_shell vedit_workspace_project vedit_workspace_jump)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(vedit_workspace_signal_trace PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
