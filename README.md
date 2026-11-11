# 1927Project

上位机（bonding 设备）多子工程统一构建/部署仓库。

## 目录结构

```
1927project/
  1928仓库管理工具.bat  唯一入口：按需编译 + 启动图形化仓库管理工具
  release_wizard/     图形化工具源码（Qt）+ build_ui.bat / run_ui.bat
  engineering/       一切工程侧资产
    scripts/         顶层脚本（七个入口 + private_scripts\ 内部件）
    cmake/           5 个子工程共用的 CMake 模块
    config/          现场配置（独立一层，更新程序时不覆盖）
    version/         主仓版本记录（CHANGELOG.md / release-map.html + private_tools/）
    runtime/         exe\shared 的仓内源头（Qt 机器件 + VC 运行时，见 runtime\README.md）
    exe_template/    exe\ 的骨架（run / start_all / stop_all）
  exe\               装配产物：四源纯拷贝拼成，可整体拷到目标机
  module/io          子工程：io 模块        ┐
  module/motion      子工程：motion 模块    │ 每个子工程都能独立
  module/re          子工程：处方界面模块   │ 编译、独立运行、独立调试
  mctrl              子工程：主控           │（Qt Creator / VSCode 打开
  gui                子工程：上位机界面     ┘  子工程的 CMakeLists.txt 即可）
  platform_output/   平台产物子模块（exe_output / rpc_output / basiclibrary_output）
  simulator/         子模块：28 模型仿真（28module\）+ 仿真中转层（simbridge\）
```

## 脚本族（七个入口，都在 engineering\scripts\，都带 -h）

内部件全部收进 `engineering\scripts\private_scripts\`（`_common.bat` `_build_one.bat`
`_deploy_ops.bat` `_sync_ops.bat` `_sync_dir.ps1` `_sync_proto.ps1`）——
它们不是入口、别直接跑，各入口用 `%HERE%private_scripts\` 调用。各核心工程同理：
`<proj>\scripts\private_scripts\_lib.bat`（re 另含 `_build-platform.bat` +
`_platform_commit.ps1`）。

| 入口 | 干什么 |
|---|---|
| `init_submodules.bat` | 子仓还原到本 commit 记录的 SHA（不 pull） |
| `update_corecode.bat` | 业务代码仓 -> develop + pull（显式升级） |
| `update_platform.bat` | 平台层对齐版本 tag（显式升级） |
| `update_thirdparty.bat` | 第三方仓 -> develop + pull（显式升级） |
| `init_update_build.bat` | 一键：还原子仓版本 + 编译 + 装配 |
| `build_exe.bat` | 编译 + 生成 exe\（零 git） |
| `sync.bat` | 跨工程同步 |

### 两层同契约 / 归属地图

`update_platform.bat` 与 `update_thirdparty.bat` 在**顶层和各核心工程各有一份**：
同名、同参数文法、同退出行为，靠目录区分。

```bat
<proj>\scripts\update_platform.bat   [<tag>|latest|show|-h]      无参 = latest
<proj>\scripts\update_thirdparty.bat [latest|show|-h]            无参 = latest
```

**谁拥有，谁维护**：顶层只对自己拥有的仓库跑 git，其余逐个 `call` 给拥有它的
工程脚本（也不去预检别人的挂载点）。

| 归属 | 平台挂载点 | 第三方 | 执行者 |
|---|---|---|---|
| 顶层 | `platform_output\{basiclibrary,rpc,exe}_output` | `simulator` | `engineering\scripts\update_platform.bat` / `update_thirdparty.bat` |
| module\io | `basiclibrary_output` | —（空壳） | `module\io\scripts\*` |
| module\motion | `basiclibrary_output` | —（空壳） | `module\motion\scripts\*` |
| gui | `basiclibrary_output`、`platform_output` | —（空壳） | `gui\scripts\*` |
| mctrl | `basiclibrary_output`、`platform_output` | `third_party` | `mctrl\scripts\*` |
| module\re | `platform`(a) + `platform\basiclibrary_output`(b) | —（空壳） | `module\re\scripts\*` |

没有这类子仓的工程用空壳实现：打印
`[n/a] update_thirdparty: <proj> owns no third-party submodule`、返回 0，
一键链里照样会被真实调到。全仓非破坏性——没有 `checkout -f`、没有 `-B`、没有
`reset`；工作区脏或上游分叉就**非 0 退出，且你的改动原地不动**，改完重跑。

### 1. init_submodules.bat —— 还原到本 commit 记录的版本

```bat
engineering\scripts\init_submodules.bat            子仓全部对齐到本 commit 的 SHA，然后报告状态
```

执行 `git submodule sync --recursive` + `git submodule update --init --recursive`，
把所有层级的子模块放到**这个 1928 commit 里记的 SHA**——缺失的会拉，已有的会回到
记录的位置。它不 pull、不选版本，所以"一个 commit = 一套固定版本"在这里成立。

**不加 `-f`**：子仓有本地改动就直接失败退出，改动原地保留，不会替你丢弃。

版本前进是显式动作（`update_corecode` / `update_platform` / `update_thirdparty`），
跑完记得在 1928 里提交新的子仓指针。

### 2. update_corecode.bat —— 版本统一（代码仓）

```bat
engineering\scripts\update_corecode.bat                全部代码仓 -> develop + pull
engineering\scripts\update_corecode.bat io gui         只更新这几个
engineering\scripts\update_corecode.bat --except m re  除这几个外都更新
```

代码仓 = **io motion re mctrl gui**。**不碰任何子模块**：平台挂载点归
`update_platform.bat`，第三方仓（`simulator`、`mctrl\third_party`）归
`update_thirdparty.bat`——simulator 不再算"代码仓"，一键链里只被更新一次。

### 3. update_platform.bat —— 平台层版本

```bat
engineering\scripts\update_platform.bat            对齐到 origin/master 最高 tag（= latest，默认）
engineering\scripts\update_platform.bat 2.0.1      平台层全部对齐到 2.0.1（可回退）
engineering\scripts\update_platform.bat latest     同上（显式写法）
engineering\scripts\update_platform.bat show       只显示各挂载点当前版本，不动手
```

顶层做自己拥有的 3 个挂载点 + 扇出到 5 个工程脚本；`module\re\platform` 的
a/b 映射（按 b 的 tag 反查 a 的 commit，无匹配即报错、不猜）也归 re 的脚本。
业务仓零 git 命令——回退平台不会顺手动你的代码。对齐后 re 的子模块指针会变，
要固化就在 re 里提交。原子性**每层各自**全有或全无，失败的层在收尾块里点名。

### 4. update_thirdparty.bat —— 第三方仓

```bat
engineering\scripts\update_thirdparty.bat          拉所有第三方仓（= latest，默认）
engineering\scripts\update_thirdparty.bat show     只看当前版本，不动手
```

顶层自己拉 `simulator`（simulator.git，develop 分支），再扇出到 5 个工程脚本
（真实现只有 `mctrl\third_party`，其余四个是空壳）。第三方没有 tag 语义，只认
`latest` / `show`。走 `fetch` + `checkout develop` + `pull --ff-only`。

### 5. init_update_build.bat —— 一键（还原版本 + 编译）

```bat
engineering\scripts\init_update_build.bat              子仓回到本 commit 记录的版本，然后编译 + 装配
engineering\scripts\init_update_build.bat --no-build   只还原子仓版本
```

链只有两步：`init_submodules` -> `build_exe`，每步必须成功才走下一步。

**它不会把任何仓库拉到最新**：`init_submodules` 执行的是
`git submodule sync --recursive` + `git submodule update --init --recursive`，
即"回到这个 1928 commit 里钉住的 SHA"。所以同一个 commit 编出来的东西是可复现的。

要升级版本是**显式的手动动作**：`update_corecode` / `update_platform <tag>` /
`update_thirdparty`，跑完之后记得在 1928 里提交新的子仓指针——否则这个 commit
就不再描述你编出来的东西了。

`init_submodules.bat` 遇到子仓有本地改动会直接失败（不加 `-f`），不会替你丢弃任何东西。

### 6. build_exe.bat —— 编译 + 生成 exe\（零 git）

```bat
engineering\scripts\build_exe.bat                  release 全量：io→motion→simbridge→re→同步→mctrl→同步→gui→装配→verify
engineering\scripts\build_exe.bat mctrl            只编 mctrl，且只把 exe\apps\mctrl\ 换掉（config\、platform\、shared\、其它 app 都不动，跳过 verify）
engineering\scripts\build_exe.bat simbridge        只编仿真中转层 relay（release-only，产物落 simulator\simbridge\deploy\bin，不进 exe\）
engineering\scripts\build_exe.bat debug io         只编 io 的 Debug
engineering\scripts\build_exe.bat -c mctrl gui     先 clean 再编
engineering\scripts\build_exe.bat --no-sync        跳过跨工程同步（就地调试不弄脏下游）
engineering\scripts\build_exe.bat pack [ver]       exe\ -> dist\1927-exe-<ver>.zip
engineering\scripts\build_exe.bat status           exe\ 概览 + 平台 tag
```

两种部署模式：

- **五个工程全选**（默认）→ 全量装配：清空重建 `exe\apps\`、`platform\`、`shared\`，重铺 `config\`，然后 verify；
- **只选部分工程** → 只把选中的 app 目录镜像进 `exe\apps\<name>\`，其余一概不动。适合"现场包已生成，我只修了 mctrl，只想换 mctrl 的 exe"。这条路径会刷新 `exe\VERSIONS.txt`，但跳过 verify（此时 `exe\` 不是完整包）。

要不要拉代码完全取决于你跑不跑 update_corecode / update_platform——build_exe 里
没有任何 git 命令。同步的顺序是硬约束（消费方必须在同步后再编），内嵌在流程里。

### 7. sync.bat —— 跨工程同步

```bat
engineering\scripts\sync.bat                       全部边（A1~A8）
engineering\scripts\sync.bat check                 只读体检
engineering\scripts\sync.bat re mctrl              只推这几个源的出边
engineering\scripts\sync.bat --dry-run             只报告不动手
```

| 源 | 推什么 |
|---|---|
| io | A1 io 协议头 → mctrl |
| motion | A2 motion 协议头 → mctrl |
| re | A3 rm 协议头 → mctrl；A4/A5 ReData → mctrl + gui（镜像） |
| mctrl | A6 MC 契约表 → engineering\config\mc_config；A7 协议头 + A8 McData → gui（镜像） |
| gui | 无出边（纯消费方） |

内容相同不碰目标 mtime（不触发下游无谓重编）；目录边是镜像语义。
同步会让下游工作区变脏——快照入库是设计选择，同步完在下游提交。
子工程里也各有 `sync.bat` 转发壳（在哪个工程目录里跑就推/拉谁的）。

### 收尾与暂停（所有入口一致）

每个入口最后都打同一个收尾块：`RESULT : OK`，或 `RESULT : FAILED (exit code N)`
加一行 `Failed steps:`（失败步骤/层列表）。失败即停、整体退出码非 0，任何情况
都能看到执行情况。双击运行默认停在"请按任意键继续"；自动化/IDE 任务设
`_SCRIPT_NO_HOLD=1` 抑制（仅抑制**被调**脚本，入口自己的暂停不受影响）。

## exe\ 的形状与运行

```
exe\
  run.bat  start_all.bat  stop_all.bat
  apps\<io|motion|re|mctrl|gui>\   每程序一个文件夹（== 各自 output\exe 整拷）
  platform\                        == platform_output 子仓镜像
    basiclibrary_output\lib\       ISTAR*.dll + Qt5*.dll + platforms + styles
    rpc_output\libUI*\lib\         libUI* 模块库（含 libUIRm）
    exe_output\CTL_Platform\CTL_*\bin\Release\   外部控制程序（2.0.4+ 的布局）
  shared\                          == engineering\runtime\ 整拷（Qt 机器件 + VC 运行时）
    qt\  system\
  config\                          == engineering\config\（唯一配置根，PLATFORM_PATH）
    mw_config.yaml  appconfig.yaml
    eh_config\ mc_config\ rp_config\ ua_config\ st_config\   平台进程
    rm_config\ io_config\ motion_config\motion1..4\          我们自己的进程
    mctrl_config\ gui_config\
    Log\                           运行期日志根（<proc>/Trace|Event）
  VERSIONS.txt                     各程序 commit + 平台 tag
```

**exe\ 是四个源头的纯拷贝拼图，每个角落都有出处、都可整文件夹替换更新：**

| 想更新什么 | 替换哪里 |
|---|---|
| 某个程序 | 重编后 `build_exe.bat`（或直接换 `exe\apps\<x>\`） |
| 平台 | `update_platform.bat <tag>` + `build_exe.bat` |
| Qt/VC 机器件 | 更新 `engineering\runtime\`（见 engineering\runtime\README.md） |
| 现场配置 | `exe\config\` |

启动（编号或名字都认，`start_all.bat -h` 看编号表；`-s` 为仿真模式）：

```bat
exe\start_all.bat              默认整套
exe\start_all.bat 6 12         只拉 io 和 GUI（某程序秒退后单独补拉就靠它）
exe\start_all.bat -s mctrl gui 仿真模式下只拉这两个
exe\stop_all.bat               全停（杀不掉的内核僵尸会明确告警）
exe\run.bat apps\gui\GUI_WM.exe - GUI_WM   单进程启动器（第 2 个参数是实例名，- = 无）
```

程序必须经 run.bat / start_all 启动：它们设置 dll 搜索 PATH，并把
`PLATFORM_PATH` 指向 `exe\config\`、把该根设为工作目录。进程一律按
`PLATFORM_PATH` 拼配置绝对路径（见各工程的 `Common/ConfigRoot.h`），不再把
"当前目录"当配置身份；cwd 只是给不归我们控制的平台库做兜底。
多实例（motion1..4）只用同一个 exe，差别只有启动时给的实例名：
`CTL_MDL_Motion.exe motion1`，进程自己读 `<配置根>\motion_config\motion1\config.json`。
`update 一个程序 = 替换 exe\apps\<名字>\ 一个文件夹`。

## 版本管理与发布

- 子仓没有打 tag 的脚本：在子仓 `develop` 上人工打标签、人工推送。
  `VERSION.txt` 是子仓唯一的版本文件，边界写这次发布 tag 去掉前缀后的名字，下面写本次更新内容：
  普通月度版本写 `>>>>> 2.1.0`，带日期重测写 `>>>>> 2.1.0_Rel_2026113001`。
  主仓脚本按“去掉首尾空白后完全相等”匹配边界，不做前缀或模糊匹配。
- 一份子仓发布的最小操作（以 gui 为例）：

  ```bat
  git -C gui tag -a GUI-2.1.0_Rel_2026113001 -m "更新内容"
  git -C gui push origin develop refs/tags/GUI-2.1.0_Rel_2026113001
  ```

- tag 格式 `<PREFIX>-<X.Y.Z>_Rel_<YYYYMMDD><NN>`，`PREFIX` 与主仓脚本里的 `SubRepos` 配置一致
  （GUI / MCTRL / RE / IO / MOTION），`NN` 是同一版本的重测序号（01、02…）。
  **tag 必须 annotated、必须 push**：主仓生成版本图时以各子仓 origin 上的 tag 为准。
- 版本节奏：每月底一个次版本（`2.<月>.0`）；月内代码修复升 patch（`2.1.1`）；
  同一份代码重测时版本号不动，只递增 `NN`。各子仓只在有改动时升版本，不强制跟主仓同号。
- 主仓只维护 `engineering/version/RELEASE.md` 和标签，`engineering\scripts\tag_version.bat`
  只生成版本图，不打标签、不提交、不推送。一节 = 一个主仓 tag，标题写**去掉前缀的标签名**：

  ```markdown
  ## 主仓 2.1.0_Rel_2026113001
  更新内容：
  1 …
  ```

  脚本读该 tag 上的 5 个子仓 gitlink，再查子仓 origin 上这个提交有没有标签：
  有标签就用标签名和它 `VERSION.txt` 里对应版本的描述；没有就用提交哈希，描述写
  "版本迭代描述缺失"。产出 `CHANGELOG.md` 和 `release-map.html`，两份都是生成物，
  不需要手改，也没有中间文件。

## 设计约定

- **CMake 管工程内的编译/生成/配置/install**；**脚本管跨仓事务**（git、同步、装配、打包）。
- 子工程 `output\exe\` == `exe\apps\<工程>\`，**顶层对整拷目录零加工**。
- `output\` 按"出口"分层：`exe\`（给装配）+ `interface\<用途>\`（给下游/外部），
  均为整目录替换语义。
- 子工程 `build/<Config>/` = 自包含调试目录，两个 IDE 直接 F5。
- 编译路径零 git；版本选择（update_corecode / update_platform）与编译（build_exe）正交。
- 所有 `.bat` **纯 ASCII + CRLF**（cmd 按 GBK 解析，UTF-8 中文注释会吃掉行尾；
  LF 行尾会让 goto 标签扫描在括号块后错位——两个都实踩过）。
  尽量不用尾部 `:label` 段（大文件 + goto + 转义符叠加会让 cmd 解析带偏，实踩）。
- 共享编译选项、RPC 生成、Qt 探测、install 规则都在 `cmake/istar_*.cmake`。

## 文档

- `构建与部署重构方案.md` —— 完整规格与"细节保全对照表"
- `构建重构-交接*.md`、`构建重构-完成报告.md` —— 历轮交接（坑清单在里头）
- `平台子仓标签同步方案.md` —— 子模块取 tag 的修正记录
- `runtime/README.md` —— shared 层源头的升级方法

---

## Getting started

To make it easy for you to get started with GitLab, here's a list of recommended next steps.

Already a pro? Just edit this README.md and make it your own.

## Add your files

- [ ] [Create](https://docs.gitlab.com/ee/user/project/repository/web_editor.html#create-a-file) or [upload](https://docs.gitlab.com/ee/user/project/repository/web_editor.html#upload-a-file) a file
- [ ] [Add files using the command line](https://docs.gitlab.com/ee/gitlab-basics/add-file.html#add-a-file-using-the-command-line)

```
cd existing_repo
git remote add origin http://172.16.10.36:8085/bonder_series/icb3000/1927project.git
git branch -M develop
git push -uf origin develop
```

## Integrate with your tools

- [ ] Set up project integrations: http://172.16.10.36:8085/bonder_series/icb3000/1927project/-/settings/integrations

## Collaborate with your team

- [ ] [Invite team members and collaborators](https://docs.gitlab.com/ee/user/project/members/)
- [ ] [Create a new merge request](https://docs.gitlab.com/ee/user/project/merge_requests/creating_merge_requests.html)
- [ ] [Automatically close issues from merge request](https://docs.gitlab.com/ee/user/project/issues/managing_issues.html#closing-issues-automatically)
- [ ] [Set auto-merge](https://docs.gitlab.com/ee/user/project/merge_requests/merge_when_pipeline_succeeds.html)
- [ ] [Enable merge request approvals](https://docs.gitlab.com/ee/user/project/approvals/)
- [ ] [Set auto-merge](https://docs.gitlab.com/ee/user/project/merge_requests/merge_when_pipeline_succeeds.html)

## Test and Deploy

Use the built-in continuous integration in GitLab.

- [ ] Get started with GitLab CI/CD: https://docs.gitlab.com/ee/ci/quick_start/index.html
- [ ] Analyze your code for known vulnerabilities with Static Application Security Testing (SAST): https://docs.gitlab.com/ee/user/application_security/sast/

***

# Editing this README

Editor's choice: make it your own. This template favors markdown structure.
