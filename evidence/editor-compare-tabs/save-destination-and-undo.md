# Save As destination and mixed line-ending undo

Save As checks whether an existing destination resolves to a regular file before writing or opening it. Comparison-backed Save As checks both the read permission bits and effective readability. The preflight uses filesystem metadata and does not open the destination for reading.

Before a comparison hunk removes multiple lines, the provider records the original line separators against the exact text in the shared buffer. If native Undo restores that text, the provider restores its separators even when an earlier edit is still dirty. The provider groups separators for co-located native joins and bounds the retained metadata. If the metadata budget is exhausted and a subsequent structural edit makes the format uncertain, the provider refuses the save until it can establish the format again.

## Reproduction before the fix

The pinned Qt runner built `vedit-undo-view-lifetime-tests` and ran the new cases against the previous provider. CTest exited 8 with two failures.

- The FIFO fixture and source file remained intact. The subprocess reached `FIFO-SAVE-AS begin` and the parent stopped it after the five-second watchdog expired.
- A 420-line mixed LF/CRLF removal was undone. Native Undo restored the buffer text and kept the independent dirty edit. Saving wrote 3,675 bytes with zero CRLF separators. The expected result was 3,955 bytes with 280 CRLF separators and no final newline. [Reproduction log](../../target/editor-compare-qa/t13-red.log)

## Verification after the fix

In the pinned Qt runner, `cmake --build target/qt6-build --parallel 4 --target vedit-undo-view-lifetime-tests` compiled the provider and test. QA verified that both source hashes still matched the frozen input. `env QT_QPA_PLATFORM=offscreen ctest --test-dir target/qt6-build -V -R '^vedit-undo-view-lifetime-tests$'` passed all 76 checks with exit 0. The test artifact SHA-256 was `ead212ce88b7ef53d9bee8b0ba128d510fdf0ffa29794e9541202b2186ced281`. [Build and test log](../../target/editor-compare-qa/t13-green-recheck.log)

```text
CHECK save-as.fifo-returned-before-watchdog PASS
CHECK save-as.fifo-and-source-intact PASS
CHECK mixed.large-hunk-replaced PASS
CHECK mixed.large-hunk-undo-retains-dirty-edit PASS
CHECK mixed.large-hunk-undo-restores-disk-bytes PASS
SUMMARY checks=76 failures=0
```
