; Keywords of the Verilog subset implemented by grammar.js
[
  "module"
  "endmodule"
  "wire"
  "reg"
  "assign"
  "always"
  "begin"
  "end"
  "input"
  "output"
  "inout"
  "posedge"
  "negedge"
] @keyword

(module_declaration name: (identifier) @type)
(identifier) @variable
(number) @number
(line_comment) @comment

["&" "|" "^" "+" "-" "*" "/" "==" "!="] @operator
