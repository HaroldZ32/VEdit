// ADDENDUM A-9 fixture: variables, nesting, cycle, missing incdir, missing var.
+incdir+inc
+incdir+no_such_dir
$RTL/a.sv
${RTL}/b.sv
$(RTL)/c.sv
~/a9-home/d.sv
rtl/$MISSING_VAR.sv
-f lists/sub.f
-f lists/cycle_a.f
+define+WIDTH=$WIDTH_VALUE
+define+PRECEDENCE=$PRECEDENCE
