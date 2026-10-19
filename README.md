# scratchpads-umbriel

Umbriel 的 **scratchpad（暂存区）图形管理器**。用 C++20 + GTK4 + `gtk4-layer-shell`
实现，以全屏覆盖层（overlay）的形式列出所有 scratchpad 及其窗口，并提供显示 / 隐藏、
切换窗口、聚焦、归还窗口、以及创建 / 删除 scratchpad 的能力。

> 面向普通用户的使用手册见 **[`docs/manual.html`](docs/manual.html)**（离线 HTML，
> 直接用浏览器打开即可，无需网络）。本 README 面向开发者与维护者。

每个 scratchpad 里的窗口以**缩略图网格**呈现（直接抓取窗口画面），而不是文字列表：

```
┌─────────────────────────────────────────────────────────────────────┐
│  Scratchpads                          ● connected     ▦      ⟳   ✕  │
│  显示、隐藏、聚焦并重新整理 Umbriel scratchpad 中的窗口。           │
│  [ 按标题或 app id 过滤…                                          ] │
│ ┌─────────────────────────────────────────────────────────────────┐ │
│ │ ▾ default        implicit   [hidden]   4 windows                │ │
│ │                                        [Show]  ◂  ▸      ⋮      │ │
│ │ ┌───────────────────────────┐ ┌───────────────────────────┐    │ │
│ │ │  ╭──────╮                 │ │        ╭──────╮           │    │ │
│ │ │  │ 窗口 │  ← 真实画面缩略图 │ │        │ 窗口 │           │    │ │
│ │ │  ╰──────╯                 │ │        ╰──────╯           │    │ │
│ │ │ OC | 用 C++ GTK4 …     ↩ ✕│ │ foot                  ↩ ✕ │    │ │
│ │ └───────────────────────────┘ └───────────────────────────┘    │ │
│ │ ┌───────────────────────────┐ ┌───────────────────────────┐    │ │
│ │ │   （隐藏窗口同样能取到画面） │ │                           │    │ │
│ │ │ foot                  ↩ ✕ │ │ foot                  ↩ ✕ │    │ │
│ │ └───────────────────────────┘ └───────────────────────────┘    │ │
│ └─────────────────────────────────────────────────────────────────┘ │
│ [＋ New scratchpad]  Reload config  Open config                     │
└─────────────────────────────────────────────────────────────────────┘
```

## 功能

**运行时管理（通过 Umbriel IPC，实时同步）**

- 列出每个 scratchpad：名称、窗口数量、`visible` / `hidden` / `empty` 状态。
- `Show` / `Hide` → `scratchpad-toggle:<name>`。
- `◂` / `▸` → `scratchpad-window-show-previous|next:<name>`，一次只显示一个成员。
- `⋮` 菜单（浮出式 popover，随主题配色；删除项为危险色并带分隔线）：
  - Focus next window → `scratchpad-focus-next:<name>`
  - Store focused window here → `window-toggle-scratchpad:<name>`
  - Move focused window here → `window-move-to-scratchpad:<name>`
  - Reveal in configuration → 用默认编辑器打开定义该 scratchpad 的文件
  - Delete scratchpad… （仅对由本工具创建、位于托管文件中的条目可见）
- 每个成员窗口显示为**缩略图卡片**，使用真实窗口画面：
  - **点击缩略图** → `window-focus:<id>`；卡片右下角 `↩` → 先聚焦再
    `window-restore-from-scratchpad:<name>`；`✕` → `window-close:<id>`。
  - 卡片左上角的状态标签（`focused` / `visible` / `urgent`）与边框颜色反映窗口状态。
  - 缩略图缺失时显示 app id 首字母（如 `GC`、`FO`）与 `capturing…` / `no preview`。
- 顶部 `▦` 开关缩略图模式：关闭后卡片变成紧凑的整行文字条目，同时停止抓帧（省电）。
- 顶部 `⟳` 重新读取配置、清空缩略图缓存并强制重新抓帧。
- 顶部过滤框按窗口标题 / app id 实时筛选；折叠状态、断线重连均有提示。

**窗口预览（缩略图）**

- 通过 Wayland 的 `ext-image-copy-capture-v1` + `ext-foreign-toplevel-list-v1`
  协议逐窗口抓帧，因此**隐藏在 scratchpad 里的窗口也能取到真实画面**——这正是
  scratchpad 场景最有用的地方。`ext_foreign_toplevel_handle_v1` 给出的 identifier
  与 Umbriel IPC 的 window id 完全一致，两边可以精确对应，无需靠标题猜测。
- 抓帧在独立的工作线程里进行（自带一条 Wayland 连接），成品通过 `Glib::Dispatcher`
  交回 GTK 主线程；缩略图统一缩放到 330×186 的画布，保证网格整齐。
- 覆盖层可见时每 4 秒刷新一次；隐藏时立刻停止抓帧。抓帧失败会退避 20 秒再试。
- 合成器不支持这些协议时，覆盖层自动退化为纯文字列表（显示 `no preview available`），
  功能不受影响。

**配置管理（可选，写入独立的托管文件）**

- 新建 scratchpad 时写入 `scratchpads-managed.toml`（与本工具所在配置目录同级），
  支持可选的 `spawn_when_empty`。
- 如果主配置的 `[include] files` 尚未包含该文件，会在**首次写入前询问**，然后做一次
  **保留注释与格式**的文本插入，并先备份为 `config.toml.scratchpad-manager.bak`。
- 只允许删除托管文件里的条目；用户自己写的 scratchpad 只读，不会被动修改。
- 每次变更后自动触发 `umbriel msg config-reload`。

**主题跟随**：订阅 Umbriel 的 `theme` 事件，用当前配色与圆角动态生成 CSS。

## 依赖

| 依赖 | 版本 | 用途 |
| --- | --- | --- |
| gtkmm-4.0 | ≥ 4.10（构建环境 4.22） | GTK4 的 C++ 绑定 |
| gtk4-layer-shell-0 | ≥ 1.0（构建环境 1.3） | layer-shell 覆盖层 |
| nlohmann_json | 任意 | IPC JSON |
| tomlplusplus | ≥ 3.0 | 读取 / 写入配置 |
| wayland-client | 任意 | 窗口抓帧的底层连接 |
| wayland-protocols | ≥ 1.36 | 生成 `ext-image-copy-capture-v1` 等协议代码 |
| meson + ninja | — | 构建 |

运行时需要正在运行的 Umbriel 会话（通过 `$UMBRIEL_SOCKET`，否则回退到
`$XDG_RUNTIME_DIR/umbriel-$WAYLAND_DISPLAY.sock`）。

## 构建与安装

```sh
meson setup build
ninja -C build
sudo ninja -C build install      # 安装到 /usr/local/bin/scratchpads-umbriel
```

## 使用

绑定一个快捷键来开关覆盖层。把它加到 `~/.config/umbriel/binds.toml`（或 `config.toml`）：

```toml
[keybinds]
"Mod+S" = "spawn:scratchpads-umbriel"
```

再次按下同一个快捷键时，`Gtk::Application` 会复用已有实例，因此覆盖层会在显示
与隐藏之间切换。

- `Esc` 或点击卡片外的暗色区域关闭；点击卡片内部不会关闭。
- 覆盖层是 `overlay` 层的 layer-shell 表面，`namespace = "scratchpad-manager"`，
  键盘交互为 `exclusive`，`exclusive_zone = 0`（不挤占其他窗口）。
  如需给它设置专属外观，可在配置里用 `[[layer_rule]]` 匹配该 namespace。

### 命令行

```
scratchpads-umbriel [options]

  -c, --config <path>   使用指定的 Umbriel 配置，而不是按 XDG 顺序查找
      --socket <path>   覆盖 IPC socket（默认 $UMBRIEL_SOCKET）
      --window          使用普通窗口而非 layer-shell 表面
      --dump            以文本打印 scratchpad 及其窗口后退出
      --add <name>      在托管文件中新建 scratchpad
      --spawn <cmd>     给 --add 设置 spawn_when_empty
      --remove <name>   从托管文件中删除 scratchpad
  -h, --help            帮助
  -v, --version         版本
```

`--dump` 便于排查问题，例如：

```console
$ scratchpads-umbriel --dump
config:  /home/me/.config/umbriel/config.toml
managed: /home/me/.config/umbriel/scratchpads-managed.toml  (included)
loaded:  5 file(s)

[default] (implicit)  1 window(s), 1 visible
    visible  kitty                ~
```

## 实现说明

```
src/
├── main.cpp                    程序入口、命令行解析、应用单实例切换
├── model/
│   ├── types.*                 WindowInfo / Theme 与 JSON 解析
│   ├── scratchpad.*            把配置定义与实时窗口合并为 Scratchpad 列表
│   └── config_store.*          递归 include 解析、托管文件读写、include 注入
├── umbriel/
│   ├── ipc.*                   Unix socket 客户端：订阅事件 + 动作队列
│   └── capture.*               独立线程的逐窗口抓帧客户端（ext-image-copy-capture-v1）
└── ui/
    ├── theme.*                 Umbriel 配色 → GTK CSS（#rrggbbaa → rgba()）
    └── overlay_window.*        layer-shell 覆盖层与全部界面
```

另有一份面向普通用户的离线 HTML 使用手册：`docs/manual.html`（自包含样式，
无外部资源与脚本，支持深色 / 浅色与打印）。

meson 会在构建时用 `wayland-scanner` 生成
`ext-foreign-toplevel-list-v1` / `ext-image-capture-source-v1` /
`ext-image-copy-capture-v1` 的绑定代码，编译成静态库后链接进主程序。

- **IPC**：`{"cmd":"subscribe","events":["windows","theme"]}` 提供全量快照
  （Umbriel 的 payload 是快照而非增量），运行在独立线程，用 `Glib::Dispatcher`
  把结果投递回 GTK 主线程；动作在另一个工作线程里顺序发送
  `{"cmd":"msg","arg":"..."}`。断线后每 1 秒自动重连。
- **配置解析**：从主配置出发递归处理 `[include]` / `[include.optional]`，
  支持 `~`、`$VAR` 展开；若完全没有 `[[scratchpad]]`，则按 Umbriel 语义视为
  隐式的 `default`。
- **include 注入**：只对 `[include]` 段做最小的文本插入（定位 `files = [` 并写入一项），
  不重新序列化整个文件，因此注释和排版都会保留。
- **颜色**：`GdkRGBA` 不接受 `#rrggbbaa`，所以把 Umbriel 的 8 位十六进制颜色
  转成 `rgba(r,g,b,a)` 再交给 CSS。
- **窗口预览**：抓帧线程自己持有 `wl_display`，只在覆盖层可见时工作；每帧先按
  `buffer_size` / `shm_format` 约束创建 shm buffer，读回像素后再缩放到固定画布。
  `ext_foreign_toplevel_handle_v1` 的 identifier 就是 Umbriel 的 window id，
  两者是同一套字符串。

## 注意事项

- 「新建」一个具名 scratchpad 后，Umbriel 会移除隐式的 `default`，
  其中暂存的窗口会回到它们保存的工作区。界面在创建前会提示这一点。
- 删除只针对托管文件中的条目；`default` 名称被 Umbriel 保留，不允许创建。
- 配置目录必须可写，否则新建 / 删除会被拒绝并给出说明（例如主配置位于
  `/usr/share` 时需先建立用户配置）。
- `--dump` / `--add` / `--remove` 会在执行后发送 `config-reload`；
  只读取运行中合成的配置本身，不会修改你的文件。
