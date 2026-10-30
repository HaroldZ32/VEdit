#!/usr/bin/env bash
# Gate `rows`: MPD-bundle ledger/row discipline. This product has no ledger
# rows, so there is nothing to check; honestly RED instead of a fake pass.
source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

no_gate "[rows] 本项目无此门禁：rows 是 MPD bundle 的 ledger 门禁，本产品没有对应实体；此门禁不伪装通过。"
