# Changelog

## 1.0.0

- Stable `sm.keybind` API for keyboard and five mouse buttons.
- Persistent shared action registry and in-game `End` bindings menu.
- Blockless `keybindbridge.json` extension discovery for local and Workshop mods.
- Runtime ownership and world re-entry recovery across Lua sandboxes.
- Client callback guard and delayed world-dependent extension work.
- Menu-open state exposed to helpers; menu input is consumed without replay.
- Seat-specific Keybind Logic with Hold, Toggle and Pulse modes.
- Separate Mode Controller driven by logic, `U`, or a seat-bound key.
- Server-side connected-seat validation and stale edge suppression.
- Character-hosted autonomous flashlight with camera aim, crouch compensation,
  first/third-person offsets, dynamic shadows and exact vanilla switch sounds.
- Release defaults to full `FeatureLevel=8`; lower levels remain diagnostics.
- Diagnostic INI is optional: a missing file selects `FeatureLevel=8` and the
  normal Windows package no longer includes it.
- `KeybindBridge.log` is truncated once per game process before current-run
  entries are appended.
- Added installable SDK template, snippets and Russian modder guide.

## 0.8.0–0.9.23

Experimental development series used to validate Lua sandbox injection,
runtime lifecycle, MyGUI, autonomous discovery, seat input and the flashlight.
The public API introduced during that series is consolidated in 1.0.0.
