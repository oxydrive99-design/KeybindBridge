# KeybindBridge 1.0.0

The first stable release of a shared additional-key bridge for Scrap Mechanic.

## For players

- shared bindings menu opened with `End`;
- assignments persist across worlds and restarts;
- keyboard and five mouse buttons are supported;
- Keybind Logic drives compatible outputs from one specific seat;
- a separate Mode Controller cycles Hold/Toggle/Pulse;
- optional autonomous flashlight works without an activation block.

## For modders

- register an action with `registerAction(id, label, defaultKey)`;
- automatically loaded extensions through `keybindbridge.json`;
- press, release and held-state API;
- `KeybindAction.lua` helper for existing scripted-object scripts;
- installable starter mod and multiplayer validation examples.

## Installation packages

The archives are intentionally separated:

- `Windows-x64` contains the DLL and installation instructions; the optional
  diagnostic configuration is no longer required for normal play;
- `Mods` contains the core mod and optional flashlight;
- `SDK` contains documentation, templates and examples.

See `INSTALL.txt` or `README.md`.

Without `KeybindBridge.diagnostics.ini`, the DLL enables the full
`FeatureLevel=8`. `KeybindBridge.log` is recreated for every game launch.

## Known limitations

- Windows x64 only;
- requires a third-party DLL injector;
- the bindings menu is keyboard-controlled;
- every player using extra keys must install the DLL locally;
- KeybindBridge transports input, while authoritative inventory and other game
  systems must be implemented and synchronized by the consuming mod.
