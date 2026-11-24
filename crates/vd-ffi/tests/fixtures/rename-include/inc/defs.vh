// T72 fixture: the macro below sets the width of the signal that gets renamed,
// and `HAS_SHARED` decides whether that signal is declared AT ALL. This file
// lives in `inc/`, NOT next to the including files, so the project can only be
// elaborated when the caller passes `-I inc`.
`define CNT_W 8
`define HAS_SHARED 1
