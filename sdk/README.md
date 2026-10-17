# OnAir plugin SDK

- `include/onair_plugin.h` the stable C ABI (public domain)
- `cmake/OnAirPlugin.cmake` `onair_add_plugin()`: builds a plugin and copies its manifest
- `template/` a minimal IQ plugin to copy
- `examples/` tone-detector, burst-counter, log-to-file (built by the repository's CMake into `<build>/plugins/<id>/`)
- `rust/onair-plugin` Rust bindings; `rust/examples/level-meter` a Rust plugin

Documentation: `docs/plugins.md`.
