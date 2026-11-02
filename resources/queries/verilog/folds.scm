; Foldable regions of Verilog (.v/.vh). The fold view folds whole lines
; [start_line, end_line]; single-line constructs are dropped by the provider.
(module_declaration) @fold
(function_declaration) @fold
(task_declaration) @fold
(generate_region) @fold
(generate_block) @fold
(seq_block) @fold
(par_block) @fold
(case_statement) @fold
