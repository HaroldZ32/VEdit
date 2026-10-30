// Probe for slang's column semantics: byte offset, character offset, or UTF-16.
module unicode_probe;
  initial begin
    /* 你好 */ undefined_cjk = 1;
    /* 😀 */ undefined_emoji = 2;
  end
endmodule
