# 来源清单（MANIFEST）

本仓是**外网开发复刻仓**，源出公司内网 GitLab 的 `1928project`。只保留源码；预编译依赖不进 git。

## 内网版本

| 项 | 值 |
|---|---|
| 内网仓库 | `git@172.16.10.36:bonder_series/icb3000/1928project.git` |
| 分支 | `develop` |
| 导出时 commit | `14a456adfa7b589a061280f503dbe2aefb7d8a92` |
| 导出时间 | 2026-11-12 |

## 被排除的预编译依赖

- `mctrl/`
- `gui/`
- `module/`
- `simulator/`
- `platform_output/`
- `exe/`
- `engineering\runtime/`
- `build-mctrl-Desktop_Qt_5_12_12_MSVC2017_64bit-Debug/`

这些目录原本是 git 子模块或打包好的运行时，含大体积二进制（单个文件可超过 GitHub 100 MB 上限），
因此被 `.gitignore` 排除。把对应的 deps 包解压到仓库根目录即可还原（见 Release 页）。

## 原来的 .gitmodules（已从本仓移除）

```ini
[submodule "module/io"]
	path = module/io
	url = git@172.16.10.36:bonder_series/icb3000/io_template.git
	branch = develop
[submodule "module/motion"]
	path = module/motion
	url = git@172.16.10.36:bonder_series/icb3000/motionstage_template.git
	branch = develop
[submodule "module/re"]
	path = module/re
	url = git@172.16.10.36:bonder_series/icb3000/re.git
	branch = develop
[submodule "mctrl"]
	path = mctrl
	url = git@172.16.10.36:bonder_series/icb3000/mctrl.git
	branch = develop
[submodule "gui"]
	path = gui
	url = git@172.16.10.36:bonder_series/icb3000/gui.git
	branch = develop
[submodule "platform_output/rpc_output"]
	path = platform_output/rpc_output
	url = git@172.16.10.36:2006/platform_v2_product/platform_output.git
	branch = master
[submodule "platform_output/exe_output"]
	path = platform_output/exe_output
	url = git@172.16.10.36:2006/platform_v2_product/tool_output.git
	branch = master
[submodule "platform_output/basiclibrary_output"]
	path = platform_output/basiclibrary_output
	url = git@172.16.10.36:2006/platform_v2_product/basiclibrary_output.git
	branch = master
[submodule "simulator"]
	path = simulator
	url = git@172.16.10.36:bonder_series/icb3000/simulator.git
	branch = develop
```

## 备注

`engineering/runtime` 是部署用的 Qt 运行时 DLL（含 78 MB 的 Qt5WebEngineCore.dll），与各子模块的二进制一样走 deps 包。
