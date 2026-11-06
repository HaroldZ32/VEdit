# 验收环境快照

- 采集时刻：2026-11-03T01:31:05+08:00（Asia/Shanghai）
- HEAD：4e2d0a57ca50db9568db5a5b040cc66f04c9a05f（4e2d0a5）
- 分支：main
- 契约 sha256：06b62f281ed849f0b9c010286e6f2ef4b498dc019047196e2b58b7c85e636a42
- 本轮 T25 owner：lead-integ（b75e0010-0cac-4c7b-9c86-d03a03b737ef）

## docker ps（起跑前，确认无 G7 构建容器）
```
intelligent_merkle	03a38da52dfb	Up 9 hours
optimistic_elbakyan	cf36128d847c	Up 9 hours
xs-net	vedit-hostmatrix-xside:local	Up 2 days
vedit-qt6-probe	docker.m.daocloud.io/library/ubuntu:26.04	Up 2 days
wizardly_hopper	vedit-tarball-cleanroom:local	Up 3 days
ui-ui-1	mpd-ui-view:latest	Up 7 days
```

## 脏路径口径（两套，禁止相减）
- `git status --porcelain=v1 | wc -l` = 241（与开工基线同口径：157 条，上轮遗留）
- `git status --porcelain=v1 -uall | wc -l` = 841（文件粒度探测值，本窗口另采）
