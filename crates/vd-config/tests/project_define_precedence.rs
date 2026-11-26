use std::fs;

use vd_config::project::{expand_with_project_entries, Variables};

#[test]
fn authored_macro_replaces_lower_layers_and_removal_restores_them(
) -> Result<(), Box<dyn std::error::Error>> {
    let temp = tempfile::tempdir()?;
    let root = temp.path();
    let filelist = root.join("sources.f");
    fs::write(
        root.join("vedit.toml"),
        "[project]\ndefines = [\"BOARD_KIND=11\"]\n",
    )?;
    fs::write(
        &filelist,
        "+define+BOARD_KIND=2+BOARD_WIDTH=2+FILELIST_ONLY\na.sv\n",
    )?;
    fs::write(root.join("a.sv"), "module a; endmodule\n")?;
    let tool_defines = ["BOARD_WIDTH=9".to_string(), "TOOL_ONLY".to_string()];
    let variables = Variables::from_pairs(&[]);

    for (authored, expected_kind, expected_width) in [
        (&[][..], Some("11"), Some("2")),
        (&["BOARD_KIND=1", "BOARD_WIDTH=3"][..], Some("1"), Some("3")),
        (&["BOARD_WIDTH=3", "BOARD_WIDTH"][..], Some("11"), None),
        (&[][..], Some("11"), Some("2")),
    ] {
        let authored = authored.iter().map(ToString::to_string).collect::<Vec<_>>();
        let expansion = expand_with_project_entries(
            root,
            std::slice::from_ref(&filelist),
            &[],
            &tool_defines,
            &[],
            &authored,
            &variables,
        )?;
        let values = |name| {
            expansion
                .defines
                .iter()
                .filter(|define| define.name == name)
                .map(|define| define.value.as_deref())
                .collect::<Vec<_>>()
        };
        assert_eq!(
            values("BOARD_KIND"),
            [expected_kind],
            "authored={authored:?}"
        );
        assert_eq!(
            values("BOARD_WIDTH"),
            [expected_width],
            "authored={authored:?}"
        );
        assert_eq!(values("FILELIST_ONLY"), [None]);
        assert_eq!(values("TOOL_ONLY"), [None]);
    }
    Ok(())
}
