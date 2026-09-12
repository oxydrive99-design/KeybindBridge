# KeybindBridge
Features:

- `sm.keybind` API for press, release and held state;
- in-game bindings menu on `End`, persisted across worlds;
- blockless extension discovery through `keybindbridge.json`;
- input suppression while the bindings menu is open;
- seat-specific Keybind Logic with Hold, Toggle and Pulse modes;
- separate logic-driven Mode Controller;
- no hard-coded offsets into `ScrapMechanic.exe`;
- ready-to-copy SDK template and multiplayer-safe examples.

Release packages are intentionally separated:

- `KeybindBridge-1.0.0-Windows-x64.zip` — DLL and diagnostics configuration;
- `KeybindBridge-1.0.0-Mods.zip` — core mod and optional flashlight;
- `KeybindBridge-1.0.0-SDK.zip` — examples, helper and API documentation.

The DLL is required on every client that uses extra keys. The core mod owns the
Lua runtime and binding UI. `FeatureLevel=8` is the normal release setting;
levels 0–7 are troubleshooting fallbacks.

See [README_RU.md](README_RU.md) for installation and gameplay instructions,
[docs/MODDERS_GUIDE_RU.md](docs/MODDERS_GUIDE_RU.md) for the modder guide, and
[docs/API.md](docs/API.md) for the complete Lua API.

Build `native/KeybindBridge.sln` as `Release | x64` with Visual Studio 2022.
Pushing a `v*` tag runs the release workflow, builds the DLL and publishes the
three separated archives.

Licensed under MIT. Bundled MinHook retains its BSD license.
