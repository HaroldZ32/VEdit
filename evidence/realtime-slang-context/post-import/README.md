# Project macro changes after filelist import

The baseline application was built from `b614a4af1d57c6e4ce292a25e539722320189a4b` and preserved as `target/qa/baseline-wrapper/vedit-app`. Its SHA-256 is `5a46dcdd052ab1250f332dca6863438025551637f1cfdba2f24a9cbf1970f980`. The real Qt application and pinned tools ran in `vedit-qt6-build:local` with networking disabled and a fresh state directory. Xvfb and the application shared an X11 socket inside the container. The run used the `xcb` platform and injected pointer and keyboard events through XTest.

The project filelist contained `rtl/top.sv`, `rtl/child.sv` and `+define+PROJECT_WIDTH=2`. The application opened the filelist, accepted the visible Import confirmation, and activated `top.sv`. The Module Tree panel displayed `top` and its `child` instance before editing the macro. The current file contained these diagnostics controls.

```systemverilog
module top;
  logic probe;
  child u_child(.sample(probe));
  genuinely_missing u_missing();
  `ifdef PROJECT_WIDTH
    logic [`PROJECT_WIDTH-1:0] bits;
    assign probe = bits[8];
  `else
    assign probe = only_width_undefined;
  `endif
endmodule
```

## Baseline

The project sidebar received the following actions after the tree was ready. The Outline row at `top.sv:7:25` remained `cannot refer to element 8 of 'logic[1:0]'` in every baseline state. The `genuinely_missing` diagnostic at `top.sv:4:3` also remained visible. Nothing started Project Lint automatically.

| Sidebar action | Values sent by the baseline project model | Expected out-of-range diagnostic |
| --- | --- | --- |
| Import confirmed | `2` | `logic[1:0]` |
| Add `PROJECT_WIDTH=10` | `2`, `10` | Clears because bit 8 fits `logic[9:0]`. |
| Add `PROJECT_WIDTH=4` | `2`, `10`, `4` | `logic[3:0]` |
| Remove the `4` row on the baseline | `2`, `10` | Removing the current sidebar override should restore the filelist value and `logic[1:0]`. |
| Remove the obsolete `10` row on the baseline | `2` | `logic[1:0]` |

The pinned Slang CLI run with `--lint-only -DPROJECT_WIDTH=<value>` confirmed an `index-oob` diagnostic for `[1:0]` at width 2 and for `[3:0]` at width 4. Width 10 exited 0. This control used the identical bit-select expression with the missing-module fixture omitted.

The local launch and gesture commands are `bash target/qa/post-import-launch.sh target/qa/baseline-wrapper/vedit-app 2` and `bash target/qa/post-import-act.sh`. The launcher opens `vedit-app --no-session-restore --state-dir <state> --project <project> <project>/sources.f <project>/rtl/top.sv`. The screen captures, application log and fixture hashes from the baseline session are in `target/qa/post-import-markers.W3q74i/`. Tool pins and container identities are in the [toolchain evidence](../toolchain/README.md).
