// Fixture filelist for the ABI smoke test.
// The expected expansion is: rtl_a.sv, rtl_b.sv, rtl_c.sv (nested -f last).
# Both comment styles are part of the parser contract.
rtl_a.sv
rtl_b.sv
+incdir+inc
+define+WIDTH=8
+define+SIM
-f nested.f
