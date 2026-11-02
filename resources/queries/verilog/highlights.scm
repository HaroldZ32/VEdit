; Verilog (.v/.vh) highlights for the vendored tree-sitter-verilog grammar
; (identity/provenance: third_party/tree-sitter/PROVENANCE.md).
; Capture names are the provider's logical highlight classes; colours and C16
; single-owner arbitration live in the overlay layer, never here.

(comment) @comment
(integral_number) @number
(real_number) @number
(string_literal) @string
(system_tf_identifier) @function.builtin

[
  "module" "endmodule" "primitive" "endprimitive" "macromodule"
  "input" "output" "inout" "wire" "reg" "integer" "real" "time" "genvar"
  "parameter" "localparam" "assign" "always" "initial"
  "function" "endfunction" "task" "endtask"
  "begin" "end" "generate" "endgenerate"
  "for" "if" "else" "case" "casex" "casez" "endcase" "default"
  "posedge" "negedge" "or" "and" "not"
  "fork" "join" "repeat" "while" "wait" "disable"
] @keyword

[
  "+" "-" "*" "/" "%" "**" "==" "!=" "===" "!==" "<" "<=" ">" ">="
  "&&" "||" "!" "&" "|" "^" "~" "~&" "~|" "~^" "<<" ">>" "<<<" ">>>"
  "++" "--" "=" "+=" "-=" "*=" "/="
] @operator

(module_header (simple_identifier) @type)
(module_instantiation (simple_identifier) @type)
