# Native SVN working tree and Project History

The `vedit-svn-workbench-selftest` opens the Qt application shell on a local `svnadmin` repository. It sends mouse events to the actual `QListWidget` and `QTreeWidget` viewports at their row coordinates. It then reads the application’s two real KTextEditor comparison documents and checks their contents and editability.

| Check | Expected result |
| --- | --- |
| Working-tree single click | The changed row is selected and the editor remains on the current tab. The inline Diff column is hidden. |
| Working-tree double click | A modified path shows pristine BASE on the left and working content on the right. A scheduled or unversioned addition has an empty left side. A scheduled deletion has an empty right side. |
| Project History | The third tab is a sibling of Working tree and History. Its revision list excludes a newer commit outside the opened project directory. Selecting the mixed revision shows four changed files, excluding its changes outside the project. |
| Historical file double click | A modification shows its pre-commit and post-commit contents, an addition has an absent left side, a deletion has an absent right side, and a copied file compares its source snapshot with the new content. Both sides are read-only. |
| Delayed SVN reads | Changing the working selection or leaving and returning to its tab prevents an old BASE result from opening. Explicit comparisons can still open when started with the dock hidden. An old historical snapshot or detail loses to the newly selected revision. Leaving Project History discards its pending log, and switching working copies displays the new project's log. |
| Simplified Chinese | The sibling tab is labeled `项目历史`. A populated file history clears the initial working-tree prompt. |

The fixture includes `新增 @ file.sv` to cover spaces, non-ASCII characters, and an SVN peg delimiter in a local and historical path. The test sets `VEDIT_SVN_UI_CAPTURE` to an existing directory to record screenshots from the native Qt shell and comparison widget. `vedit-workspace-vcs-selftest` covers the working-tree panel’s retained Git diff behavior, and `vedit-file-compare-selftest` covers the shared comparison and existing SVN file history flows.
