# Vendored Wayland protocols

## `wlr-layer-shell-unstable-v1.xml`

The layer-shell protocol that Umbriel (wlroots) implements. Unlike the
protocols in `wayland-protocols`, it is not shipped by anyone else, so the XML
is vendored here and generated at build time like the rest.

- Source: <https://gitlab.freedesktop.org/wlroots/wlroots>
  (`protocol/wlr-layer-shell-unstable-v1.xml`, taken from the 0.18 release)
- Licence: MIT (see the copyright header inside the XML)
- Local change: the `set_namespace` argument is named `name_space` instead of
  `namespace`, because `wayland-scanner` copies argument names into the
  generated C header and `namespace` is a keyword in C++. The wire protocol is
  unaffected by argument names.

The other protocols (`xdg-shell`, `ext-image-copy-capture-v1`, ...) come from
`wayland-protocols` and are looked up through `pkg-config` at configure time.
