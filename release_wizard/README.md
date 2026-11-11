# release_wizard（1928 仓库管理工具）

图形化的仓库管理工具：状态查看、更新、构建、同步、子仓发布、主仓发布。
主仓发布、更新、构建、同步都调 `engineering\scripts\` 下的脚本；子仓发布例外——
子仓没有专用脚本，`VERSION.txt` 由 UI 写入并提交，tag 由 UI 直接执行 `git tag/push`。

## 启动

日常只用根目录的入口：

```bat
1928仓库管理工具.bat              源码比 exe 新才编译，然后启动
1928仓库管理工具.bat -Rebuild     强制重编，然后启动
```

`release_wizard\build_ui.bat` / `run_ui.bat` 是底层两步，需要单独编译或单独启动时才用。

- Qt：5.12.12 msvc2017_64；VS 2019；CMake 3.28
- 源码：`src/`；构建产物在 `build/`，不要提交
- 脚本目录：`engineering\scripts\`；版本文件：`engineering\version\`

## 约束

- 不要把 `build/`、exe、Qt DLL、截图、日志提交进 git；
- 不要在 UI 里重写脚本逻辑（UI 只负责调用脚本）；唯一例外是子仓打 tag：
  子仓没有脚本，UI 直接执行 `git tag -a` / `git push`。
- 发布 / 打 tag 必须在界面上人工确认后才执行。

## 文档

- `docs\交接-UI现状与发布页.md`：当前实现与发布页现状
- `docs\主仓发布.md`：主仓发布流程
- `docs\版本管理.md`：版本与 tag 规则
