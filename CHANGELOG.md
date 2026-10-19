# Changelog

All notable changes to this project are documented in this file. This project
follows [Semantic Versioning](https://semver.org/).

## [0.1.0] — 2026-10-19

First public release.

`paddeck` is a keyboard-driven scratchpad switcher for the
[Umbriel](https://github.com/noctalia-dev/umbriel) Wayland compositor. It shows
every scratchpad with live thumbnails of the windows inside it, and lets you
switch scratchpads without touching the mouse.

### Interface

- Full-screen layer-shell panel on the `overlay` layer with
  `namespace = "paddeck"`, keyboard interactivity `exclusive` and
  `exclusive_zone = 0` (it reserves no space from other windows).
- One-line key legend permanently visible at the bottom of the panel.
- The selected scratchpad card is outlined with the theme's accent green
  (`accent_secondary`) and its name is tinted to match. Every other outline
  keeps the theme's `colors.border.unfocused` at 1px, so the panel stays quiet
  and moving the selection never shifts the layout.
- Panel height adapts to its content; colours and corner radius follow the
  Umbriel theme (CSS is generated on every `theme` event).
- The selection scrolls into view automatically, and the panel closes on `Esc`,
  on a second press of the launch shortcut, or on a click outside it.

### Switching

- Lists every scratchpad with its name, window count, a `visible` /
  `partly visible` / `hidden` / `empty` badge, and an optional
  `spawn_when_empty` hint.
- Keyboard map: `j` / `k` (or `↑` `↓`, `Tab` / `Shift+Tab`) moves the selection,
  `Space` shows / hides the whole scratchpad, `h` / `l` (or `←` `→`) cycles
  member windows, `Enter` focuses a window and closes the panel, `1`–`9` jumps
  to the n-th scratchpad and toggles it, `Home` / `End` jump to the ends, and
  `Esc` closes.
- Mouse equivalents: click a card to select it, click a thumbnail to focus that
  window and close, click outside the panel to dismiss.
- Disconnects and failed actions are reported at the bottom of the panel;
  reconnection is automatic.

### Window previews

- Each window is shown as a thumbnail captured through the Wayland
  `ext-image-copy-capture-v1` and `ext-foreign-toplevel-list-v1` protocols.
- **Windows hidden inside a scratchpad can be previewed too** — the
  `ext_foreign_toplevel_handle_v1` identifier matches Umbriel's IPC window id
  exactly, so both sides pair up without guessing from titles.
- Capture runs on a dedicated thread with its own Wayland connection; frames are
  handed back to the GTK main thread through `Glib::Dispatcher`.
- Thumbnails are scaled onto a 220×124 canvas and laid out three per row.
- Refreshes every 4 seconds while the overlay is visible and stops immediately
  when hidden; failed captures back off for 20 seconds.
- Tiles fall back to the app id's initials when no preview is available (for
  example when the compositor lacks the protocols); switching and focusing keep
  working in that case.

### Configuration

- The Umbriel configuration is re-read every time the panel opens, including
  recursive `[include]` / `[include.optional]` and `~` / `$VAR` expansion.
- **Read-only**: no managed file, no include injection, no backup, no
  confirmation dialogs. Scratchpads are added or removed by editing the
  configuration directly. The process writes no files at all.

### Command line

- `--dump` prints scratchpads and their windows as plain text for
  troubleshooting.
- `--config` / `--socket` override the config and IPC paths; `--window` opens a
  normal window instead of a layer-shell surface.

### Documentation

- `docs/index.html`: offline English user manual (self-contained styles, no
  external assets or scripts, dark / light and print support).
- `docs/zh.html`: the same manual in Chinese.
- `README.md` (English) and `README.zh-CN.md` (Chinese): build, install,
  configuration, architecture and caveats.
- Documents the recommended `[animation.scratchpad] scale = 0.9` setting, and
  why `match.is_scratchpad` combined with `default_floating*` cannot replace it.

### Known limitations

- Capture requires the compositor to provide `ext-image-copy-capture-v1`. The
  protocol cannot scale on the compositor side, so every frame is read back at
  native resolution and scaled locally.
- While the panel is open, visible windows are re-captured every 4 seconds;
  leaving it open for a long time has a continuous cost.
- The panel only lists windows that currently live in a scratchpad. Ordinary
  desktop windows never appear.

[0.1.0]: https://github.com/ccjjxl/paddeck/releases/tag/v0.1.0
