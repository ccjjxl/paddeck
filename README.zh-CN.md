# paddeck

[English](README.md) · **简体中文**

Umbriel 的 **scratchpad（暂存区）切换器**，用 C++20 + GTK4 实现，
以全屏覆盖层（overlay）的形式列出所有 scratchpad 及其窗口，
**全部用键盘操作**：移动选中项、显示 / 隐藏整个暂存区、逐个成员窗口切换、跳转到窗口。
覆盖层是 `wlr-layer-shell` 表面，该协议由本项目直接实现，
因此**不依赖** `gtk4-layer-shell`。

> 面向普通用户的使用手册：[`docs/zh.html`](docs/zh.html)（中文）·
> [`docs/index.html`](docs/index.html)（英文，默认语言）。自包含的离线 HTML，
> 直接用浏览器打开即可，无需网络。仓库用 GitHub Pages
> （Settings → Pages → 源：`/docs`）发布后，手册也会发布在
> <https://ccjjxl.github.io/paddeck/>（英文）与
> <https://ccjjxl.github.io/paddeck/zh.html>（中文）。
> 本 README 面向开发者与维护者。版本变更见 [`CHANGELOG.md`](CHANGELOG.md)。

界面只有一件事：**选中项 + 操作**。每个 scratchpad 的窗口以缩略图网格呈现
（直接抓取窗口画面），而不是文字列表：

```
┌───────────────────────────────────────────────────────────────────┐
│  scratchpads                                          ● connected │
│  ┌─────────────────────────────────────────────────────────────┐  │
│  │ work                                             [visible]  │  │
│  │ ┌───────────────┐ ┌───────────────┐ ┌───────────────┐       │  │
│  │ │ live preview  │ │ foot          │ │ foot          │       │  │
│  │ └───────────────┘ └───────────────┘ └───────────────┘       │  │
│  └─────────────────────────────────────────────────────────────┘  │
│  ┌─────────────────────────────────────────────────────────────┐  │
│  │ media                                              [empty]  │  │
│  │ Empty — toggling it runs: spotify                           │  │
│  └─────────────────────────────────────────────────────────────┘  │
│  j/k select · space toggle · ←/→ member · enter focus · esc close │
└───────────────────────────────────────────────────────────────────┘
```

面板、卡片、缩略图的轮廓都保持 1px 细线，面板与缩略图沿用主题的
`colors.border.unfocused`（在 Umbriel 默认配色里它等于面板底色，因此几乎不可见，
这是刻意的：它们不该抢眼）。**只有选中的 scratchpad 卡片**用主题的强调绿
（`accent_secondary`）描边，并把它自己的名字也染成绿色——绿框 = 当前选中。
选中与未选中的边框宽度一致，所以移动选中项时缩略图不会抖动。

## 键盘操作

所有按键都作用于**当前选中项**，按键提示常驻在面板底部。

| 按键 | 动作 | Umbriel 动作 |
| --- | --- | --- |
| `j` / `k`、`↓` / `↑`、`Tab` / `Shift+Tab` | 上下移动选中项（首尾循环） | — |
| `Space` | 显示 / 隐藏选中的暂存区 | `scratchpad-toggle:<name>` |
| `h` / `l`、`←` / `→` | 只显示上一个 / 下一个成员窗口（缩略图位置固定不变） | `scratchpad-window-show-previous\|next:<name>` |
| `Enter` | 跳转到选中暂存区的窗口并关闭面板 | `window-focus:<id>` |
| `1` … `9` | 跳到第 n 个暂存区并立即切换 | `scratchpad-toggle:<name>` |
| `Home` / `End` | 跳到第一个 / 最后一个 | — |
| `Esc` | 关闭面板 | — |

鼠标仍然可用：点击卡片 = 选中；点击缩略图 = 跳转并关闭；点击面板外 = 关闭。

## 功能

**切换（唯一的核心功能，通过 Umbriel IPC 实时同步）**

- 列出每个 scratchpad：名称、窗口数量、`visible` / `showing n of m` /
  `hidden` / `empty` 状态标签，以及可选的 `spawn_when_empty` 提示。
- 键盘切换整个暂存区、逐个成员步进、跳转到具体窗口（见上表）。
- 每个成员窗口显示为**缩略图卡片**，使用真实窗口画面：
  - 成员顺序由 paddeck 记住并保持固定（新成员追加到末尾），显示 / 隐藏不会让任何
    缩略图换位置：`h` / `l` 只是让高亮在网格上左右移动。Umbriel 的窗口列表是
    “最近聚焦优先”，直接用它会让每次步进都重排缩略图。
  - 当暂存区一次只显示一个成员时，其余成员会变暗，让当前显示的那一个保持醒目。
  - 点击卡片 → `window-focus:<id>`，然后关闭面板。
  - 卡片左上角的状态标签（`focused` / `visible` / `urgent`）与边框颜色反映窗口状态；
    `focused` 表示此刻真正持有键盘焦点的窗口。
  - 缩略图缺失时显示 app id 首字母（如 `GC`、`FO`），不会显示占位文案。
- 选中项进入视口时自动滚动；断线重连、操作失败均在底部给出提示。

**窗口预览（缩略图）**

- 通过 Wayland 的 `ext-image-copy-capture-v1` + `ext-foreign-toplevel-list-v1`
  协议逐窗口抓帧，因此**隐藏在 scratchpad 里的窗口也能取到真实画面**——这正是
  scratchpad 场景最有用的地方。`ext_foreign_toplevel_handle_v1` 给出的 identifier
  与 Umbriel IPC 的 window id 完全一致，两边可以精确对应，无需靠标题猜测。
- 抓帧在独立的工作线程里进行（自带一条 Wayland 连接），成品通过 `Glib::Dispatcher`
  交回 GTK 主线程；缩略图统一缩放到 220×124 的画布（16:9，与每行 3 张的网格匹配），
  保证网格整齐。
- 覆盖层可见时每 4 秒刷新一次；隐藏时立刻停止抓帧。抓帧失败会退避 20 秒再试。
- 合成器不支持这些协议时，覆盖层照常工作（只显示首字母），并在底部提示
  `Window previews are unavailable on this compositor.`。

**只读配置**

- 每次打开面板重新读取一次 Umbriel 配置，包括递归的 `[include]` /
  `[include.optional]` 与 `~`、`$VAR` 展开。
- **不写任何文件**：没有托管文件、没有 include 注入、没有备份、没有确认弹窗。
  增删 scratchpad 由用户直接编辑配置文件完成。

**主题跟随**：订阅 Umbriel 的 `theme` 事件，用当前配色与圆角动态生成 CSS。

## 依赖

| 依赖 | 版本 | 用途 |
| --- | --- | --- |
| gtkmm-4.0 | ≥ 4.10（构建环境 4.22） | GTK4 的 C++ 绑定 |
| nlohmann_json | 任意 | IPC JSON |
| tomlplusplus | ≥ 3.0 | 读取配置 |
| wayland-client | 任意 | 覆盖层与窗口抓帧的底层连接 |
| wayland-protocols | ≥ 1.36 | 生成 `xdg-shell`、`ext-image-copy-capture-v1` 等协议代码 |
| meson + ninja | — | 构建 |

`wlr-layer-shell-unstable-v1` 的协议 XML 已随源码放在 `protocols/` 下并直接编译进
程序，构建和运行都不需要额外的 layer-shell 库。

运行时需要正在运行的 Umbriel 会话（通过 `$UMBRIEL_SOCKET`，否则回退到
`$XDG_RUNTIME_DIR/umbriel-$WAYLAND_DISPLAY.sock`）。

## 构建与安装

```sh
meson setup build
ninja -C build
sudo ninja -C build install      # 安装到 /usr/local/bin/paddeck
```

## 使用

绑定一个快捷键来开关覆盖层。把它加到 `~/.config/umbriel/binds.toml`（或 `config.toml`）：

```toml
[keybinds]
"Mod+S" = "spawn:paddeck"
```

再次按下同一个快捷键时，`Gtk::Application` 会复用已有实例，因此覆盖层会在显示
与隐藏之间切换。

- `Esc`、点击卡片外的暗色区域、或再次按快捷键关闭。
- 覆盖层是 `overlay` 层的 layer-shell 表面，`namespace = "paddeck"`，
  键盘交互为 `exclusive`，`exclusive_zone = 0`（不挤占其他窗口）。
  如需给它设置专属外观，可在配置里用 `[[layer_rule]]` 匹配该 namespace。
  该表面由 `src/wayland/layer_shell.cpp` 直接使用 `zwlr_layer_shell_v1` 创建。

### 推荐的 Umbriel 配置

加上这一段，**任何**窗口进入暂存区时都会统一变成输出可用区域的 90% × 90% 并居中，
与面板里看到的效果一致，也不需要按应用名逐个写规则：

```toml
# ~/.config/umbriel/config.toml
[animation.scratchpad]
scale = 0.9        # 0.1–1.0；0 表示保持窗口原有尺寸和位置
```

- 窗口进入暂存区时**本来就会自动浮动**，不必再写 `default_floating = true`。
- 优先级 `fullscreen` > `maximize` > `scale`，后两者保持 `false`（默认值）即可。
- 尺寸有 100px 下限；该值只在窗口**进入**暂存区时计算一次，改配置后已在暂存区里的
  旧窗口需要取出来再放回去才会套用新尺寸。

**不要**用窗口规则做同样的事，这样写**不会生效**：

```toml
# ✗ 无效：开窗设置只在窗口打开那一刻结算，那时它还不在暂存区里
[[window_rule]]
match.is_scratchpad = true
default_floating = true
default_floating_size = { width = 0.9, height = 0.9 }
```

`match.is_scratchpad` 只对**运行时状态**类字段有效，例如 `opacity`、`blur`、
`border_color_focused`：

```toml
# ✓ 有效：暂存区里的窗口统一半透明
[[window_rule]]
match.is_scratchpad = true
opacity = 0.9
```

### 命令行

```
paddeck [options]

  -c, --config <path>   使用指定的 Umbriel 配置，而不是按 XDG 顺序查找
      --socket <path>   覆盖 IPC socket（默认 $UMBRIEL_SOCKET）
      --window          使用普通窗口而非 layer-shell 表面
      --dump            以文本打印 scratchpad 及其窗口后退出
  -h, --help            帮助
  -v, --version         版本
```

`--dump` 便于排查问题，例如：

```console
$ paddeck --dump
config: /home/me/.config/umbriel/config.toml
loaded: 6 file(s)

[default] (implicit)  1 window(s), 0 visible
    hidden   kitty                ~

[work]  2 window(s), 2 visible
    focused  kitty                editor
    visible  foot                 logs
```

## 实现说明

```
src/
├── main.cpp                    程序入口、命令行解析、应用单实例切换
├── model/
│   ├── types.*                 WindowInfo / Theme / ScratchpadDef 与 JSON 解析
│   ├── scratchpad.*            把配置定义与实时窗口合并为 Scratchpad 列表
│   └── config_store.*          只读的配置发现与递归 include 解析
├── umbriel/
│   ├── ipc.*                   Unix socket 客户端：订阅事件 + 动作队列
│   └── capture.*               独立线程的逐窗口抓帧客户端（ext-image-copy-capture-v1）
├── wayland/
│   ├── layer_shell.*           覆盖层的 wlr-layer-shell 表面
│   └── wl_shim.*               拦截 xdg_surface 角色，使上面的做法成为可能
└── ui/
    ├── theme.*                 Umbriel 配色 → GTK CSS（#rrggbbaa → rgba()）
    └── overlay_window.*        layer-shell 覆盖层、选中项状态与全部键盘处理
```

另有一份面向普通用户的离线 HTML 使用手册，提供两种语言：
`docs/index.html`（英文）与 `docs/zh.html`（中文）。两者都是自包含样式，
无外部资源与脚本，支持深色 / 浅色与打印。

meson 会在构建时用 `wayland-scanner` 生成 `xdg-shell`、
`ext-foreign-toplevel-list-v1` / `ext-image-capture-source-v1` /
`ext-image-copy-capture-v1` 的绑定代码，编译成静态库后链接进主程序；
随源码提供的 `wlr-layer-shell-unstable-v1.xml` 也走同一流程。
版本号由 `configure_file` 从 `project version` 生成 `paddeck_config.hpp`，
`--version` 与代码里都不再硬编码。

- **IPC**：`{"cmd":"subscribe","events":["windows","theme"]}` 提供全量快照
  （Umbriel 的 payload 是快照而非增量），运行在独立线程，用 `Glib::Dispatcher`
  把结果投递回 GTK 主线程；动作在另一个工作线程里顺序发送
  `{"cmd":"msg","arg":"..."}`。断线后每 1 秒自动重连。
- **键盘**：`Gtk::EventControllerKey` 挂在窗口上并使用 `CAPTURE` 阶段，
  因此列表永远不会把按键拿去做自己的焦点导航；`Tab` / 方向键被显式消费。
- **选中与滚动**：选中项所在的卡片设置了 `can_focus`，靠 `grab_focus()`
  让 `GtkScrolledWindow` 自动把它滚进视口，不需要自己算坐标。
- **配置解析**：从主配置出发递归处理 `[include]` / `[include.optional]`，
  支持 `~`、`$VAR` 展开；若完全没有 `[[scratchpad]]`，则按 Umbriel 语义视为
  隐式的 `default`。解析过程只读，不产生任何副作用。
- **颜色**：`GdkRGBA` 不接受 `#rrggbbaa`，所以把 Umbriel 的 8 位十六进制颜色
  转成 `rgba(r,g,b,a)` 再交给 CSS。
- **窗口预览**：抓帧线程自己持有 `wl_display`，只在覆盖层可见时工作；每帧先按
  `buffer_size` / `shm_format` 约束创建 shm buffer，读回像素后再缩放并居中到固定画布。
  卡片本身的 `size_request` 才是尺寸来源，因此抓帧还没完成（或永远失败）时，
  卡片不会塌陷到标题栏上。

## 注意事项

- 「新建」一个具名 scratchpad 后，Umbriel 会移除隐式的 `default`，
  其中暂存的窗口会回到它们保存的工作区。这发生在 Umbriel 侧，本工具只做展示。
- 面板只列出**存在于暂存区里的窗口**；普通显示在桌面上的窗口不会出现。
- 面板每次打开都会重新读取配置，所以配置改动无需重启，也不必点「重新加载」。
- `--dump` 只读：它不会发送 `config-reload`，也不会修改你的任何文件。

## 版本与许可

- 当前版本：**0.3.0**（见 [`CHANGELOG.md`](CHANGELOG.md)）。
- 许可证：[MIT](LICENSE)。所有源文件均带 `SPDX-License-Identifier: MIT` 标识。
- 源码发布包：

  ```sh
  meson dist                      # 生成 build/meson-dist/paddeck-<版本>.tar.xz
  ```

- 发布由 GitHub Actions 完成：推送 `v*` 标签会构建源码包与 x86_64 预编译包，
  并自动发布到 GitHub Release；每次推送 `main` 与每个 PR 也会执行构建检查。

- 运行时要求：Umbriel（wlroots）会话；窗口预览额外需要合成器提供
  `ext-image-copy-capture-v1` 与 `ext-foreign-toplevel-list-v1`，否则只显示首字母。
