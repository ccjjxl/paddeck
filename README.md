# paddeck

**English** · [简体中文](README.zh-CN.md)

A keyboard-driven **scratchpad switcher** for the
[Umbriel](https://github.com/noctalia-dev/umbriel) Wayland compositor, written in
C++20 with GTK4. The overlay is a `wlr-layer-shell` surface; the protocol is
implemented directly in this project, so there is no `gtk4-layer-shell`
dependency.

It opens a full-screen overlay that lists every scratchpad together with the
windows inside it, showing **live thumbnails** of those windows. Everything is
driven from the keyboard: move the selection, show/hide a whole scratchpad,
cycle through its members, or jump straight to a window.

> End-user documentation: **[`docs/index.html`](docs/index.html)** (English) ·
> **[`docs/zh.html`](docs/zh.html)** (Chinese). Self-contained offline HTML —
> open it directly in a browser. Once the repository is published with GitHub
> Pages (Settings → Pages → source: `/docs`), the manual is also served at
> <https://ccjjxl.github.io/paddeck/> and
> <https://ccjjxl.github.io/paddeck/zh.html>.
> This README is aimed at developers and packagers.
> See [`CHANGELOG.md`](CHANGELOG.md) for release notes.

The UI does exactly one thing: **a selection plus actions**. Each scratchpad's
windows are laid out as a thumbnail grid (captured from the real windows), not a
text list:

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

Every outline — panel, cards, tiles — stays a 1px hairline and reuses the
theme's `colors.border.unfocused` (which equals the panel background in
Umbriel's default palette, so it is almost invisible on purpose: these borders
should not draw attention). **Only the selected scratchpad card** gets the
theme's accent green (`accent_secondary`), and its name is tinted to match:
green border = current selection. The border width is 1px in both states, so
moving the selection never makes the thumbnails jump.

## Keyboard

Every key acts on the **current selection**, and the key legend is always
visible at the bottom of the panel.

| Key | Action | Umbriel action |
| --- | --- | --- |
| `j` / `k`, `↓` / `↑`, `Tab` / `Shift+Tab` | Move the selection (wraps around) | — |
| `Space` | Show / hide the selected scratchpad | `scratchpad-toggle:<name>` |
| `h` / `l`, `←` / `→` | Show only the previous / next member window (the tiles never reorder) | `scratchpad-window-show-previous\|next:<name>` |
| `Enter` | Focus a window in the selected scratchpad and close | `window-focus:<id>` |
| `1` … `9` | Jump to the n-th scratchpad and toggle it immediately | `scratchpad-toggle:<name>` |
| `Home` / `End` | Jump to the first / last scratchpad | — |
| `Esc` | Close the panel | — |

The mouse still works: click a card to select it, click a thumbnail to focus
that window and close, click outside the panel to dismiss.

## Features

**Switching (the only core feature, kept in sync over Umbriel IPC)**

- Lists every scratchpad: name, window count, a `visible` / `showing n of m` /
  `hidden` / `empty` status badge, and an optional `spawn_when_empty` hint.
- Switch a whole scratchpad, step through its members, or focus a specific
  window (see the table above).
- Every member window is rendered as a **thumbnail card** using its real
  content:
  - Members keep a fixed order that paddeck remembers, so a tile never jumps to
    another slot when it is shown or hidden: `h` / `l` simply walks the
    highlight across the grid. (Umbriel's window list is most-recently-focused
    first, so it cannot be used directly; new members are appended at the end.)
  - While a scratchpad shows one member at a time, the parked members are
    dimmed so the shown one stays obvious.
  - Clicking a card sends `window-focus:<id>` and closes the panel.
  - A status badge in the top-left corner (`focused` / `visible` / `urgent`)
    and the tile border colour reflect the window state; `focused` is the
    window that holds keyboard focus right now.
  - When no thumbnail is available, the tile shows the first letters of the app
    id (e.g. `GC`, `FO`) instead of placeholder text.
- The selection is scrolled into view automatically; disconnects and failed
  actions are reported at the bottom of the panel.

**Window previews (thumbnails)**

- Captures each window through the Wayland `ext-image-copy-capture-v1` and
  `ext-foreign-toplevel-list-v1` protocols, so **windows hidden inside a
  scratchpad still produce a real image** — exactly where this is most useful.
  The identifier handed out by `ext_foreign_toplevel_handle_v1` matches the
  window id reported by Umbriel IPC, so the two sides pair up precisely without
  guessing from titles.
- Capture runs on a dedicated worker thread with its own Wayland connection;
  finished frames are handed back to the GTK main thread through
  `Glib::Dispatcher`. Thumbnails are scaled onto a 220×124 canvas (16:9, matching
  the three-per-row grid) so the grid stays tidy.
- Refreshes every 4 seconds while the overlay is visible and stops immediately
  when hidden. A failed capture backs off for 20 seconds.
- If the compositor lacks these protocols the overlay still works (initials
  only) and shows `Window previews are unavailable on this compositor.` at the
  bottom.

**Read-only configuration**

- The Umbriel configuration is re-read every time the panel opens, including
  recursive `[include]` / `[include.optional]` and `~` / `$VAR` expansion.
- **It never writes any file**: no managed file, no include injection, no
  backup, no confirmation dialogs. Scratchpads are added or removed by editing
  the configuration directly.

**Theme aware**: subscribes to Umbriel's `theme` event and generates CSS from
the current palette and corner radius.

## Requirements

| Dependency | Version | Purpose |
| --- | --- | --- |
| gtkmm-4.0 | ≥ 4.10 (built against 4.22) | C++ bindings for GTK4 |
| nlohmann_json | any | IPC JSON |
| tomlplusplus | ≥ 3.0 | reading the configuration |
| wayland-client | any | low-level connection for the overlay and window capture |
| wayland-protocols | ≥ 1.36 | generates `xdg-shell` and `ext-image-copy-capture-v1` and friends |
| meson + ninja | — | build system |

`wlr-layer-shell-unstable-v1` is vendored under `protocols/` and compiled in,
so no layer-shell library is needed at build or run time.

At runtime a running Umbriel session is required (via `$UMBRIEL_SOCKET`,
falling back to `$XDG_RUNTIME_DIR/umbriel-$WAYLAND_DISPLAY.sock`).

## Build & install

```sh
meson setup build
ninja -C build
sudo ninja -C build install      # installs to /usr/local/bin/paddeck
```

## Usage

Bind a shortcut that launches the overlay. Add it to
`~/.config/umbriel/binds.toml` (or `config.toml`):

```toml
[keybinds]
"Mod+S" = "spawn:paddeck"
```

Pressing the same shortcut again makes `Gtk::Application` reuse the existing
instance, so the overlay toggles between shown and hidden.

- `Esc`, clicking outside the card, or pressing the shortcut again closes it.
- The overlay is a layer-shell surface on the `overlay` layer with
  `namespace = "paddeck"`, keyboard interactivity `exclusive` and
  `exclusive_zone = 0` (it does not reserve space from other windows). Use a
  `[[layer_rule]]` matching that namespace if you want to style it.
  The surface is created by `src/wayland/layer_shell.cpp`, which speaks
  `zwlr_layer_shell_v1` directly.

### Recommended Umbriel configuration

With this snippet, **any** window that enters a scratchpad is resized to 90% ×
90% of the output's usable area and centred, matching what you see in the panel
— no per-application rules required:

```toml
# ~/.config/umbriel/config.toml
[animation.scratchpad]
scale = 0.9        # 0.1–1.0; 0 keeps the window's original size and position
```

- Windows entering a scratchpad **are made floating automatically**, so
  `default_floating = true` is not needed.
- Precedence is `fullscreen` > `maximize` > `scale`; leave the first two at
  `false` (their default) for this to apply.
- There is a 100px floor, and the value is computed once when a window
  **enters** the scratchpad. After changing the config, windows already in a
  scratchpad must be taken out and put back in to pick up the new size.

**Do not** try to achieve the same with a window rule — the following does
**not** work:

```toml
# ✗ Invalid: open-time settings are resolved when the window opens, before it
#   is in a scratchpad
[[window_rule]]
match.is_scratchpad = true
default_floating = true
default_floating_size = { width = 0.9, height = 0.9 }
```

`match.is_scratchpad` only applies to **runtime** properties such as `opacity`,
`blur` and `border_color_focused`:

```toml
# ✓ Valid: every window in a scratchpad becomes slightly translucent
[[window_rule]]
match.is_scratchpad = true
opacity = 0.9
```

### Command line

```
paddeck [options]

  -c, --config <path>   use the given Umbriel config instead of the XDG search
      --socket <path>   override the IPC socket (default: $UMBRIEL_SOCKET)
      --window          use a normal window instead of a layer-shell surface
      --dump            print scratchpads and their windows as text, then exit
  -h, --help            show help
  -v, --version         show version
```

`--dump` is handy for troubleshooting:

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

## Implementation notes

```
src/
├── main.cpp                    entry point, CLI parsing, single-instance toggle
├── model/
│   ├── types.*                 WindowInfo / Theme / ScratchpadDef and JSON parsing
│   ├── scratchpad.*            merges config definitions with live windows
│   └── config_store.*          read-only config discovery and recursive include parsing
├── umbriel/
│   ├── ipc.*                   Unix socket client: event subscription + action queue
│   └── capture.*               per-window capture client on its own thread
├── wayland/
│   ├── layer_shell.*           wlr-layer-shell surface for the overlay
│   └── wl_shim.*               xdg_surface role interception that makes it possible
└── ui/
    ├── theme.*                 Umbriel palette → GTK CSS (#rrggbbaa → rgba())
    └── overlay_window.*        layer-shell overlay, selection state, all key handling
```

A user-facing offline HTML manual is provided in two languages:
`docs/index.html` (English) and `docs/zh.html` (Chinese). Both are
self-contained, use no external assets or scripts, and support dark / light
colour schemes and printing.

meson generates the `xdg-shell`, `ext-foreign-toplevel-list-v1` /
`ext-image-capture-source-v1` / `ext-image-copy-capture-v1` bindings with
`wayland-scanner` at build time, compiles them into a static library and links
that into the binary. The vendored `wlr-layer-shell-unstable-v1.xml` is
processed the same way. The version string is produced by `configure_file` from
`project version` into `paddeck_config.hpp`, so neither `--version` nor the
source hard-codes it.

- **IPC**: `{"cmd":"subscribe","events":["windows","theme"]}` delivers a full
  snapshot (Umbriel's payload is a snapshot, not a delta). It runs on a separate
  thread and posts results back to the GTK main thread with
  `Glib::Dispatcher`; actions are sent sequentially as
  `{"cmd":"msg","arg":"..."}` from another worker thread. Reconnects every
  second after a disconnect.
- **Keyboard**: a `Gtk::EventControllerKey` is attached in the `CAPTURE` phase,
  so the list never consumes keys for its own focus navigation; `Tab` and the
  arrow keys are consumed explicitly.
- **Selection and scrolling**: the card holding the selection is focusable, and
  `grab_focus()` makes `GtkScrolledWindow` scroll it into view without manual
  coordinate math.
- **Config parsing**: starts from the main config and recursively processes
  `[include]` / `[include.optional]`, expanding `~` and `$VAR`. With no
  `[[scratchpad]]` at all, Umbriel's implicit `default` scratchpad is assumed.
  Parsing is strictly read-only.
- **Colours**: `GdkRGBA` does not accept `#rrggbbaa`, so Umbriel's 8-digit hex
  colours are converted to `rgba(r,g,b,a)` before being handed to CSS.
- **Previews**: the capture thread owns its own `wl_display` and only works
  while the overlay is visible. Each frame first creates an shm buffer honouring
  the `buffer_size` / `shm_format` constraints, then reads the pixels back,
  scales them and centres them on a fixed canvas. The card's own `size_request`
  defines its size, so the card does not collapse onto its header while capture
  is pending (or failing for good).

## Caveats

- After you "create" a named scratchpad, Umbriel removes the implicit `default`
  scratchpad and any windows parked in it return to their saved workspaces.
  That happens on Umbriel's side; paddeck only displays it.
- The panel only lists **windows that live in a scratchpad**. Ordinary windows
  on the desktop do not appear — that is the point of a scratchpad switcher.
- The configuration is re-read every time the panel opens, so changes need
  neither a restart nor a "reload" button.
- `--dump` is read-only: it never sends `config-reload` and never touches your
  files.

## Version & license

- Current version: **0.3.0** (see [`CHANGELOG.md`](CHANGELOG.md)).
- License: [MIT](LICENSE). Every source file carries an
  `SPDX-License-Identifier: MIT` header.
- Source tarball:

  ```sh
  meson dist                      # writes build/meson-dist/paddeck-<version>.tar.xz
  ```

- Releases are built by GitHub Actions: pushing a `v*` tag produces the source
  tarball plus a prebuilt x86_64 binary archive and publishes both to the
  GitHub release. CI also builds every push to `main` and every pull request.

- Runtime requirement: an Umbriel (wlroots) session. Window previews
  additionally need the compositor to provide `ext-image-copy-capture-v1` and
  `ext-foreign-toplevel-list-v1`; otherwise tiles show initials.
