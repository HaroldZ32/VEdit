; SystemVerilog (.sv/.svh) highlights for the vendored tree-sitter-verilog
; grammar (the upstream grammar covers .v/.vh/.sv/.svh; see PROVENANCE.md).
; Verilog captures are repeated here on purpose: the two query sets are
; independent and a .sv file must not depend on the .v file being loaded.

(comment) @comment
(integral_number) @number
(real_number) @number
(string_literal) @string
(system_tf_identifier) @function.builtin

[
  "module" "endmodule" "primitive" "endprimitive" "macromodule"
  "input" "output" "inout" "wire" "reg" "integer" "real" "time" "genvar"
  "parameter" "localparam" "assign" "always" "always_ff" "always_comb" "always_latch"
  "initial" "final" "function" "endfunction" "task" "endtask"
  "begin" "end" "generate" "endgenerate"
  "for" "if" "else" "case" "casex" "casez" "endcase" "default"
  "posedge" "negedge" "or" "and" "not"
  "fork" "join" "repeat" "while" "wait" "disable"
] @keyword

[
  "interface" "endinterface" "package" "endpackage" "class" "endclass"
  "program" "endprogram" "typedef" "enum" "struct" "union"
  "logic" "bit" "byte" "shortint" "int" "longint"
  "import" "export" "extends" "virtual" "static" "automatic"
  "modport" "clocking" "endclocking" "property" "endproperty"
  "sequence" "endsequence" "assert" "covergroup" "constraint"
  "rand" "randc" "new" "this" "super" "null" "return" "break" "continue"
  "void" "signed" "unsigned" "packed" "const" "ref" "var" "local" "protected" "pure" "extern"
] @keyword

[
  "+" "-" "*" "/" "%" "**" "==" "!=" "===" "!==" "<" "<=" ">" ">="
  "&&" "||" "!" "&" "|" "^" "~" "~&" "~|" "~^" "<<" ">>" "<<<" ">>>"
  "++" "--" "=" "+=" "-=" "*=" "/="
] @operator

(module_header (simple_identifier) @type)
(module_instantiation (simple_identifier) @type)
(interface_identifier) @type
(class_identifier) @type
(package_identifier) @type
