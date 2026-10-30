#!/usr/bin/env bash
# Gate `comments`: MPD-bundle comment checker. This product has no such gate:
# the checker is a bundle tool (MPD_DSH_COMMENT_CHECKER_BIN) and the product
# sources do not exist yet. It is honestly RED instead of silently green.
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

no_gate "[comments] 本项目无此门禁：本产品未接入 comment-checker（该检查属于 MPD bundle 自身）；此门禁不伪装通过。"
