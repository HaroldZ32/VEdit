; Structure/outline entries for SystemVerilog (.sv/.svh). Anchor = the whole
; declaration (so breadcrumbs work anywhere inside it), @name = identifier text.
(module_declaration (module_header (simple_identifier) @name)) @module
(interface_declaration
  [(interface_ansi_header (interface_identifier) @name)
   (interface_nonansi_header (interface_identifier) @name)]) @interface
(package_declaration (package_identifier) @name) @package
(class_declaration (class_identifier) @name) @class
(program_declaration
  [(program_ansi_header (program_identifier) @name)
   (program_nonansi_header (program_identifier) @name)]) @program
(function_declaration (function_body_declaration (function_identifier) @name)) @function
(task_declaration (task_body_declaration (task_identifier) @name)) @task
; class constructors are their own node kind upstream (no function_identifier),
; so they are listed as a function entry without a name capture.
(class_declaration (class_item (class_method (class_constructor_declaration) @function)))
