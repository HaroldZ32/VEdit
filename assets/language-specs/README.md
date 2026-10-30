# Language definitions (data assets) — provenance and patch status

This directory documents the Verilog/SystemVerilog **data** assets that T11 relies on. No
`.lang` file is modified: the upstream definitions have no gap for the AC-05 constructs, so the
"minimal patch only when the definition really lacks something" clause of the contract resolves
to **no patch**.

## Where the definitions come from

GtkSourceView **4.6.1**, built from the pinned upstream tarball
(`gtksourceview-4.6.1.tar.xz`, sha256
`63f487a7ab59612ffa9e7be1883dbd274ab1016afcede3239b93b0fc384df874`, built by the skeleton task
into `/opt/gsv4`). The product bundles them from that install into
`usr/share/gtksourceview-4/{language-specs,styles}` (packaging tasks T23/T25).

Measured in the pinned CentOS 7 image (full output in `evidence/highlight/03-langspec-provenance.txt`):

```
166                                  # files in language-specs/
7670ddfd7b443f5f99c202bfa79de31834c1bd8968ed74c39474dca4f439d464  verilog.lang        (475 lines)
9d1f67411f2674d93f035620ce004da4beca5bbf469eb9955a83c332edb79b04  systemverilog.lang  (262 lines)
3c8997e4539e56a7bfde7e51e52dd6768fc41f818f30af6497a2a28a5e133b94  styles/cobalt.xml
```

The two Verilog/SystemVerilog files are **byte-identical to GtkSourceView 4.8.4** (sha256 above
matches the T5 report `evidence/language-spec-equality.txt`), so the 4.8 → 4.6.1 version
deviation costs nothing for this product.

A `.lang` file is **data, not code**: editing one is allowed by the contract (registering the
patch and its provenance here and in `THIRD_PARTY_NOTICES`), while writing a lexer/renderer in
Rust is forbidden (`requirements.md` §Non-goals).

## Why the two files are not vendored into the repository

They are consumed from the GtkSourceView install (and from the bundled copy at run time) through
`GtkSourceLanguageManager`'s search path. Copying them here would create a second, drifting copy
of an LGPL asset and would not be the file the editor actually loads. The sha256 values above
are the identity check; `evidence/highlight/01-probe.log` shows both languages resolving
(`LANGUAGES verilog=true systemverilog=true missing=[]`).

## Inheritance note (why `systemverilog.lang` looks incomplete)

`systemverilog.lang` declares only the SystemVerilog-specific items and then inherits everything
else from Verilog:

```
256:        <context ref="verilog:verilog"/>
```

`module` therefore appears **0 times** as a keyword in `systemverilog.lang` and **once** in
`verilog.lang`; the rendered probe confirms both are highlighted in a `systemverilog` buffer.
Any future validation of these assets must use the rendered result or the resolution of the
inheritance chain, never a string search.

## Licence

GtkSourceView is LGPL-2.1-or-later. The tarball URL, version and sha256 above are recorded for
the LGPL source-availability obligation; the notices themselves are written by T29
(`THIRD_PARTY_NOTICES`).
