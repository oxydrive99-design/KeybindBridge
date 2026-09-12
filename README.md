# KeybindBridge 1.0.0

KeybindBridge gives Scrap Mechanic Lua mods a shared API for additional
keyboard and mouse bindings. A mod declares an action with a stable ID, a
display name and a default key; the player can rebind it from the in-game menu
opened with `End`.

The project consists of independent parts:

- `keybind_bridge.dll` reads keyboard and mouse input, stores assignments and
  publishes `sm.keybind` to Lua;
- `KeybindBridge Core` starts autonomous extensions and contains the bindings
  menu, the Keybind Logic block and the Mode Controller;
- `KeybindBridge Flashlight` is an optional real-world example of a blockless
  autonomous mod;
- the SDK contains a ready-to-copy template, short examples and API reference.

Russian documentation: [README_RU.md](README_RU.md).

## Features in 1.0.0

- `A-Z`, number keys, navigation keys, `F1-F24` and five mouse buttons;
- one `End` menu for autonomous actions from all participating mods;
- assignments persist across worlds and game restarts;
- multiple actions may share one key and multiple listeners may observe an
  action;
- press, release and held-state APIs;
- automatic discovery of `keybindbridge.json` in local and Workshop mods;
- autonomous mods work without placing an activation block;
- actions are suppressed while configuring bindings and do not replay after
  the menu closes;
- seat-specific Keybind Logic does not occupy the seat hotbar;
- `HOLD`, `TOGGLE` and `PULSE` block modes;
- separate Mode Controller operated by a bound key, `U`, or a regular logic
  signal;
- server-side verification of the specifically connected seat;
- no hard-coded function offsets into `ScrapMechanic.exe`.

## Requirements

- Windows x64;
- Scrap Mechanic;
- a compatible DLL injector that loads modules from `Release/DLLModules`;
- every player who uses additional local keys must install the DLL.

## Installation for players

1. From the Windows archive, copy `keybind_bridge.dll` and
   `KeybindBridge.diagnostics.ini` to:

   `Steam/steamapps/common/Scrap Mechanic/Release/DLLModules`

2. From the Mods archive, copy `KeybindBridge-Core` to your local mods folder:

   `%APPDATA%/Axolot Games/Scrap Mechanic/User/User_<id>/Mods`

3. Optionally copy `KeybindBridge-Flashlight` to the same folder.
4. Enable the mods when creating or editing the world.
5. Fully restart Scrap Mechanic after adding or removing an autonomous mod.

`FeatureLevel=8` is the normal release setting. Levels `0-7` are diagnostic
fallbacks only.

## Bindings menu

Press `End` while in a world:

- `Up/Down` selects an action;
- `Enter` starts key capture;
- `Delete` restores the default key;
- `Esc` or `End` closes the menu.

Assignments are saved immediately to
`Release/DLLModules/KeybindBridge.bindings.ini`. Only autonomous mod actions
appear in this menu. Per-block bindings are intentionally kept separate.

## Blocks

### Keybind Logic

1. Connect a specific seat to the blue block.
2. Interact with the block using `E`, then press the desired key.
3. Press `U` to cycle `HOLD -> TOGGLE -> PULSE`.
4. Connect the output to logic, lights, controllers, bearings or another
   compatible consumer.

The block responds only to the player occupying the connected seat. A passenger
needs a separate Keybind Logic connected to their own seat. Keys pressed outside
the seat are consumed and are not replayed after sitting down.

### Mode Controller

The orange block changes the mode of connected Keybind Logic blocks. It can be
triggered by:

- the rising edge of a regular button, gate or sensor;
- `U` while interacting with it;
- an assigned key from a connected seat.

One Mode Controller can drive several Keybind Logic blocks.

## Smallest autonomous mod

Place `keybindbridge.json` beside the mod's `description.json`:

```json
{
  "version": 1,
  "script": "Scripts/KeybindBridgeExtension.lua"
}
```

Create `Scripts/KeybindBridgeExtension.lua`:

```lua
local ACTION = "yourname.yourmod.open_map"

local extension = {
    id = "yourname.yourmod.map_extension",
    actions = {
        {
            id = ACTION,
            label = "Open map",
            defaultKey = "M"
        }
    }
}

function extension:onActionPressed(actionId)
    if actionId == ACTION then
        self:openMap()
    end
end

KeybindBridgeRuntime.registerExtension(extension)
```

Extension and action IDs must remain stable and globally unique. Prefix them
with the author and mod name. Changing an ID resets its saved assignment and
creates a new entry in the menu.

The complete folder structure is in `examples/sdk-template-mod`. See
[docs/MODDERS_GUIDE.md](docs/MODDERS_GUIDE.md) for press, hold and safe network
examples, and [docs/API.md](docs/API.md) for the complete Lua API.

## Multiplayer safety

A key press is local user input, not authority to change server state. Send a
request through `self.network:sendToServer(...)`, then validate the player,
distance, ownership, equipment and operation on the server. See
`examples/snippets/03_existing_script_with_helper.lua`.

KeybindBridge provides input only. Inventory persistence, death bags, ownership
and other game systems remain the responsibility of the mod using it.

## Building the DLL

Visual Studio 2022 with the Desktop development with C++ workload is required.

1. Open `native/KeybindBridge.sln`.
2. Select `Release | x64`.
3. Build the solution.
4. The DLL is written to `native/x64/Release/keybind_bridge.dll`.

LuaJIT and vcpkg do not need to be installed separately. MinHook is bundled and
retains its own license. Pushing a `v*` tag runs GitHub Actions, builds the DLL
and creates the separated release archives.

## Logs and diagnostics

DLL log: `Release/DLLModules/KeybindBridge.log`.

A normal startup contains:

```text
OK: discovered ... autonomous extension manifest(s)
OK: core KeybindRuntime.lua source loaded from disk
OK: KeybindBridge 1.0.0 initialized; FeatureLevel=8
OK: KeybindBridge client runtime started
```

If the game crashes, follow [docs/DIAGNOSTICS.md](docs/DIAGNOSTICS.md) and keep a
separate `KeybindBridge.log` and game `game-*.log` for every test.

## License

KeybindBridge is licensed under MIT. Bundled MinHook retains its BSD license in
`native/vendor/minhook/LICENSE.txt`.
