# KeybindBridge for modders

## Choose the right integration

| Goal | Recommended approach |
|---|---|
| Open a local map, GUI or flashlight | Autonomous extension with `keybindbridge.json` |
| Add a binding to an existing Tool/Part/Player script | `KeybindAction.lua` |
| Send a command to the server | `KeybindAction.lua` + `self.network` + server validation |
| Assign a private key to a block connected to a specific seat | Raw API; use Keybind Logic as the reference |

The DLL loads autonomous extensions automatically; no inventory object needs
to be placed. This is convenient for client-only features. A normal scripted
object receives `self.network`, `self.storage` and other game-instance
facilities, so it is the better owner of authoritative server state.

## Option 1: autonomous extension

Minimum folder structure:

```text
YourMod/
├── description.json
├── keybindbridge.json
├── Gui/
│   └── IconMap.xml
├── Objects/
│   └── Database/
│       ├── rotationsets.rotationset
│       └── shapesets.shapedb
└── Scripts/
    └── KeybindBridgeExtension.lua
```

For a mod without blocks, `shapesets.shapedb` must contain an empty
`shapeSetList` and `rotationsets.rotationset` an empty `rotationSet`. A complete
copy-ready project is available in `examples/sdk-template-mod`.

### Manifest

```json
{
  "version": 1,
  "script": "Scripts/KeybindBridgeExtension.lua"
}
```

The script path must be relative, remain inside the mod and use forward
slashes. Fully restart the game after adding, removing or renaming a manifest:
discovery runs when the DLL loads.

### Single press

```lua
local ACTION = "author.mod.open_map"

local extension = {
    id = "author.mod.map",
    actions = {
        { id = ACTION, label = "Open map", defaultKey = "M" }
    }
}

function extension:onActionPressed(actionId)
    if actionId == ACTION then
        -- Run the mod's local feature here.
    end
end

KeybindBridgeRuntime.registerExtension(extension)
```

The runtime suppresses `onActionPressed` while the bindings menu is open and
consumes the press so it is not replayed when the menu closes.

### Multiple actions

Add more entries to `actions`. The callback receives the ID of the action that
was pressed:

```lua
actions = {
    { id = "author.mod.map", label = "Open map", defaultKey = "M" },
    { id = "author.mod.backpack", label = "Open backpack", defaultKey = "B" }
}
```

Actions may intentionally share a key. Their IDs must remain unique.

### Hold and release

`onActionPressed` reports only the rising edge. Poll `actionIsDown` from
`onUpdate` for held state:

```lua
function extension:onUpdate()
    local menuOpen = sm.keybind.isGameMenuOpen()
    local down = not menuOpen and sm.keybind.actionIsDown(ACTION)

    if down and not self.wasDown then
        -- Hold started.
    elseif not down and self.wasDown then
        -- Hold released.
    end
    self.wasDown = down
end
```

Set `updateIntervalMs = 16` in the extension table to throttle `onUpdate`.
`onActionPressed` delivery is not delayed by this interval.

## Option 2: KeybindAction in an existing script

Copy `Scripts/KeybindAction.lua` from the SDK into your mod and load it:

```lua
dofile("$CONTENT_DATA/Scripts/KeybindAction.lua")
```

Create the action once in a client instance:

```lua
function Example.client_onCreate(self)
    self.backpackKey = KeybindAction.create(
        "author.mod.backpack",
        "Open backpack",
        "B")
end
```

Poll it from a client update callback:

```lua
function Example.client_onFixedUpdate(self)
    if self.backpackKey:wasPressed() then
        self:openBackpack()
    end
end
```

Helper methods:

- `isAvailable()` - whether the DLL API is present;
- `isDown()` - whether the assigned key is held;
- `wasPressed()` - one press event;
- `wasReleased()` - one release event;
- `getKey()` - current numeric Windows VK code;
- `getKeyName()` - current display name.

The helper consumes events generated while the bindings menu is open. If a
client has no DLL, it safely returns `false`/`nil` instead of crashing the
world.

Do not create a separate helper in hundreds of identical objects for one global
feature: every instance can independently observe the same event. A global
binding should have one logical owner.

## Multiplayer and security

A press is detected on the client. It does not prove that the player is allowed
to modify server state.

```lua
function Example.client_onFixedUpdate(self)
    if self.action:wasPressed() then
        self.network:sendToServer("sv_requestAction")
    end
end

function Example.sv_requestAction(self, _, player)
    local character = player and player.character or nil
    if character == nil or not sm.exists(character) then return end

    -- Validate distance, ownership, equipment, inventory and world state.
    if (character.worldPosition - self.shape.worldPosition):length() > 5 then
        return
    end

    -- Only now perform the authoritative server-side operation.
end
```

For a backpack, the server must own the inventory contents, verify the equipped
backpack type, transfer items to the normal death bag and synchronize only the
authorized view to clients. KeybindBridge solves input; it does not replace
that gameplay logic.

## Stable IDs

Recommended format:

```text
author.mod.feature.action
```

IDs may contain ASCII letters, digits, `.`, `_`, `-` and `:` and may be up to
96 bytes long. The player-facing label may change. Do not change a published
ID: saved assignments are keyed by it.

## Default keys

Pass either a key name (`"F"`, `"Tab"`, `"Mouse4"`, `"LeftShift"`, `"F12"`)
or a numeric Windows virtual-key code. `End` is reserved for the menu and cannot
be assigned.

Never assume that a default key is free. A player may intentionally keep a
conflict or rebind the action in the menu.

## World-dependent functions

Some client callbacks have a Lua environment but temporarily lack world
context. Calls such as `sm.effect` and `sm.audio` may fail there. Queue an
important one-shot request and retry it from a later `onUpdate`. The autonomous
flashlight demonstrates this pattern.

## Before publishing

- generate a new mod `localId`;
- use unique extension and action IDs;
- keep absolute paths and `..` out of `keybindbridge.json`;
- make the mod fail safely when the DLL is absent;
- revalidate every client request on the server;
- confirm menu input cannot trigger the gameplay action;
- test leaving and re-entering a world without restarting the game;
- test both host and connected player;
- exclude logs, Cache, `.pdb` and temporary files from the archive.
