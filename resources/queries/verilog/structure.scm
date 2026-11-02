; Structure/outline entries for Verilog (.v/.vh): one match per declaration,
; anchor capture = the whole declaration, @name = its identifier text.
(module_declaration (module_header (simple_identifier) @name)) @module
(function_declaration (function_body_declaration (function_identifier) @name)) @function
(task_declaration (task_body_declaration (task_identifier) @name)) @task
