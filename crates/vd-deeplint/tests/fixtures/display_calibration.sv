module display_calibration;
  initial begin
    /* plain */ undeclared_plain = 1;
    /* tab->	 */ undeclared_tab = 1;
    /* 你 */ undeclared_cjk1 = 1;
    /* 你好 */ undeclared_cjk2 = 1;
    /* 你好呀 */ undeclared_cjk3 = 1;
    /* 😀 */ undeclared_e1 = 1;
    /* 😀😀 */ undeclared_e2 = 1;
    /* 😀😀😀 */ undeclared_e3 = 1;
    /* 中😀 */ undeclared_ce = 1;
    /* e<comb> */ undeclared_comb = 1;
  end
endmodule
