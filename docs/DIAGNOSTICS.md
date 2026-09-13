# KeybindBridge 1.0.0 diagnostics

The normal release configuration is `FeatureLevel=8`. If
`KeybindBridge.diagnostics.ini` does not exist, the DLL selects level 8
automatically. MyGUI is created only after the player presses `End`, autonomous
extensions are executed through `luaL_loadbuffer`, and the runtime runs only
from confirmed client callbacks. Levels `0-7` disable subsystems one by one to
isolate a crash. Create `KeybindBridge.diagnostics.ini` beside
`keybind_bridge.dll` in `Release/DLLModules` only for these tests.

## Test procedure

Fully close the game before every run. `KeybindBridge.log` is cleared
automatically when the new game process writes its first entry.

### Run 1 - only if level 8 crashes

```ini
[Diagnostics]
FeatureLevel=0
```

This enables the input API and hook from the working 0.8 baseline, without the
manifest scanner or autonomous runtime. Load a test world and remain there for
15-20 seconds.

### Run 2 - only if level 0 did not crash

```ini
[Diagnostics]
FeatureLevel=1
```

Adds the extended action registry and `keybindbridge.json` discovery, but does
not execute any discovered Lua file.

### Run 3 - only if level 1 did not crash

```ini
[Diagnostics]
FeatureLevel=2
```

The DLL only retains a reference to the Lua environment. It does not call Lua
functions or start the runtime.

### Run 4 - only if level 2 did not crash

```ini
[Diagnostics]
FeatureLevel=3
```

After the original game call returns, the DLL checks `sm.isServerMode()` but
does not call `dofile`. The log should contain
`TRACE: client environment guard ...` entries.

### Run 5 - only if level 3 did not crash

```ini
[Diagnostics]
FeatureLevel=4
```

The DLL compiles and executes one safe Lua probe through `luaL_loadbuffer`. It
does not create the runtime or GUI. The log should contain
`TRACE: native Lua loader probe completed`.

### Run 6 - only if level 4 did not crash

```ini
[Diagnostics]
FeatureLevel=5
```

The real `KeybindRuntime.lua` executes, but creates only an empty runtime. Key
polling, MyGUI and autonomous extensions are still disabled. The log should
contain `OK: KeybindBridge client runtime started`.

### Run 7 - only if level 5 did not crash

```ini
[Diagnostics]
FeatureLevel=6
```

Enables the basic runtime and key polling, but not MyGUI or extensions.

### Run 8 - only if level 6 did not crash

```ini
[Diagnostics]
FeatureLevel=7
```

MyGUI is not created while the world loads. After the character appears, press
`End`; only then does the runtime attempt to create and open the menu. The
external Win32 window is completely disabled. Autonomous extensions are not
loaded yet.

### Run 9 - only if level 7 did not crash

```ini
[Diagnostics]
FeatureLevel=8
```

Full release configuration. The DLL executes discovered extensions through
`luaL_loadbuffer` in the same sandbox as the core. Their client callbacks wait
for a local player, while MyGUI waits for the first `End` press. Neither an
external window nor game-side `dofile` is used.

## What to include in a report

For every tested level, provide:

- the `FeatureLevel` number;
- whether the world loaded or the game crashed;
- a separate `KeybindBridge.log` and game log.

The startup line must contain the selected level, for example:

```text
OK: KeybindBridge 1.0.0 initialized; FeatureLevel=1
```

If it reports another level, the game read a different INI or loaded another
copy of the DLL.
