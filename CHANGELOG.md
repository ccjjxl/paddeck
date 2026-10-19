# 更新记录

本项目遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [0.1.0] — 2026-10-19

首个版本。

### 界面

- 全屏 `overlay` 层的 layer-shell 面板，`namespace = "scratchpad-manager"`，
  键盘交互 `exclusive`、`exclusive_zone = 0`（不挤占其它窗口）。
- 按同一快捷键或 `Esc` 关闭；点击面板外的暗色区域也可关闭。
- 面板内容自适应高度，配色与圆角跟随 Umbriel 主题（订阅 `theme` 事件动态生成 CSS）。

### 窗口缩略图

- 每个窗口以**真实画面缩略图**呈现，通过 Wayland 的
  `ext-image-copy-capture-v1` + `ext-foreign-toplevel-list-v1` 逐窗口抓帧。
- **隐藏在 scratchpad 里的窗口同样能预览**——`ext_foreign_toplevel_handle_v1`
  的 identifier 与 Umbriel IPC 的 window id 完全一致，两边精确对应。
- 抓帧在独立线程（自带一条 Wayland 连接）进行，覆盖层可见时每 4 秒刷新，
  隐藏即停止；失败退避 20 秒。
- 缩略图统一缩放到 330×186 画布，网格固定两列，尺寸整齐。
- 合成器不支持相关协议时自动退化为文字列表，其余功能不受影响。
- `▦` 可关闭预览（卡片变为紧凑条目并停止抓帧）；`⟳` 重载配置并强制重抓。

### 运行时管理

- 列出每个 scratchpad：名称、窗口数量、`visible` / `hidden` / `empty` 状态。
- 显示 / 隐藏整个 scratchpad、逐个切换成员（`◂` `▸`）、聚焦窗口、
  归还到保存的工作区、关闭窗口。
- `⋮` 菜单：聚焦下一个窗口、存入或移入当前窗口、在配置文件中定位、删除（危险色）。
- 按标题 / app id 实时过滤窗口；折叠状态与断线重连均有提示与自动重连。

### 配置管理

- 新建 scratchpad 写入独立的 `scratchpads-managed.toml`，支持可选的 `spawn_when_empty`。
- 需要向主配置注入 `[include] files` 时先询问，注入前备份为
  `config.toml.scratchpad-manager.bak`，且只做最小文本插入，保留注释与格式。
- 只允许删除托管文件中的条目，用户手写的 scratchpad 只读。
- 每次变更后自动发送 `config-reload`。

### 命令行

- `--dump` 以纯文本打印 scratchpad 及其窗口，便于排查。
- `--add` / `--spawn` / `--remove` 直接管理托管文件。
- `--config` / `--socket` 覆盖配置与 IPC 路径，`--window` 使用普通窗口。

### 文档

- `docs/manual.html`：面向普通用户的离线中文使用手册
  （自包含样式，无外部资源与脚本，支持深色 / 浅色与打印）。
- `README.md`：构建、安装、架构与注意事项。

### 已知限制

- 抓帧必须由合成器提供 `ext-image-copy-capture-v1`；协议不支持让合成器
  直接缩放到缩略图尺寸，因此每次抓取都是原分辨率读回后再本地缩放。
- 面板打开期间每 4 秒会重新抓取一次可见的窗口，长时间开着会有持续的开销；
  关掉预览开关可完全消除。

[0.1.0]: https://github.com/ccjjxl/scratchpads-umbriel/releases/tag/v0.1.0
