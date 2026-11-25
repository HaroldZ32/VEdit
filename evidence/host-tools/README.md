# Host tools and HDL acceptance evidence

Independent QA recorded **PASS** as `rec-20261125T023940-97017b` for loop `loop-20261125T015408-415bd8`. The product source is `8643809224affc9fc2c32be1fd73315b57075ab4` and the packaged launcher includes `33a857224011daa1aadbb5b534a8d28eaa4a338c`. The verified AppImage SHA-256 is `1944cb913bab1e1938b9e1641d9543dfb37e2318a093d5aea0d9a72233e22993`. [Independent review and exact commands](review/README.md)

- Preferences, saved shell selection and real PTYs. [Terminal acceptance](terminal/README.md) · [Final settings screenshot](review/settings-host-shell.png)
- Native Git and SVN reads and writes through installed shells, with real local repositories and path and permission checks. [VCS acceptance](vcs/README.md) · [Final Qt panel checks](review/final-qt-host.log)
- Realtime diagnostics and Ctrl navigation against the imported filelist, the module tree, competing definitions and legitimate includes. [Original regression](filelist/README.md) · [Compiler settings and include repairs](filelist/repair/README.md) · [Final independent X11 run](review/final-hdl-x11.log)
- AppDir and AppImage contents, host-tool absence, legacy candidate refusal and language restart. [Packaging acceptance](packaging/README.md) · [Final image receipt](review/final-appimage.log) · [Packaged terminal](review/package-terminal.png) · [Git status](review/package-terminal-vcs.png) · [Missing Git](review/package-terminal-missing.png)
- Pinned Rust and Qt gates, generated header and old and new C ABI entry points. [Rust and ABI run](review/final-rust-abi.log) · [Commit and ABI receipt](review/commit-abi-receipt.log) · [Launcher, docs and translation gates](review/branch-packaging-gates.log)

The shell preference applies to new terminal sessions and subsequent VCS jobs. Very large project-index startup and refresh times remain unmeasured. A Ctrl jump during a document transition may need to be retried when the new diagnostics session is ready. The documented Qt build expects the pinned Slang CLI to be staged first. [Transition evidence](filelist/repair/ready-transition.log) · [Build prerequisite check](review/slang-clean-checkout-control.log)
