# KeybindBridge Lua API 1.0.0

The injected DLL publishes the API before a mod script runs:

```lua
local keybind = sm.keybind
if not keybind then
    -- Show an installation hint; never crash a world because a client lacks DLL.
end
```

Scrap Mechanic hides the standard `package`/`package.cpath` loader from game
scripts, so `require("keybind_bridge")` is not used. The DLL must be loaded by the
DLL injector from `Release/DLLModules`.

## Autonomous actions (recommended for mods)

An autonomous action belongs to a mod feature such as opening a map, toggling a
flashlight, or opening a backpack. Register it once from a client-side script:

```lua
local ACTION = "yourname.backpack.open"
sm.keybind.registerAction(ACTION, "Open backpack", "B")
```

The arguments are:

1. A stable, globally unique ID. Prefix it with the author/mod name.
2. The user-facing name shown in the shared binding menu.
3. A default key name (`"A"`, `"F12"`, `"Mouse4"`, `"Tab"`, etc.) or a
   Windows virtual-key number.

Pressing `End` opens the in-game MyGUI binding menu. Escape and End both close
the menu through the runtime; assignments are committed immediately. Only registered autonomous
actions appear there. Rebindings are saved to
`Release/DLLModules/KeybindBridge.bindings.ini` and therefore apply across
worlds. `End` is reserved and cannot itself be assigned. Multiple actions may
intentionally share a key.

`onActionPressed` callbacks registered through `KeybindBridgeRuntime` are
suppressed while this menu is open. Helpers also consume press/release serials
created by menu interaction, so they are not replayed after the menu closes.

The easiest event handling uses the included `Scripts/KeybindAction.lua`:

```lua
dofile("$CONTENT_DATA/Scripts/KeybindAction.lua")

function Player.client_onCreate(self)
    self.openBackpack = KeybindAction.create(
        "yourname.backpack.open",
        "Open backpack",
        "B")
end

function Player.client_onUpdate(self, dt)
    if self.openBackpack:wasPressed() then
        self:openBackpackGui()
    end
end
```

`KeybindAction` also provides `isDown()`, `wasReleased()`, `getKey()`,
`getKeyName()`, and `isAvailable()`. It safely stays inactive if a client does
not have the DLL.

### Autonomous action functions

| Function | Result | Notes |
|---|---|---|
| `registerAction(id, label, defaultKey)` | number | Registers or refreshes an action and returns its current assigned VK code. |
| `actionKey(id)` | number or nil | Current assigned VK code. |
| `actionKeyName(id)` | string or nil | Display name of the current key. |
| `actionIsDown(id)` | boolean | Current held state of the action's assigned key. |
| `actionPressSerial(id)` | number | Independent press counter that continues correctly after rebinding. |
| `actionReleaseSerial(id)` | number | Independent release counter that continues correctly after rebinding. |
| `actionCount()` | number | Number of autonomous actions currently registered. |
| `actionId(index)` | string or nil | Stable ID at the one-based menu index. |
| `actionLabel(index)` | string or nil | Display label at the one-based menu index. |
| `setActionKey(id, key)` | boolean | Saves a new key for a registered action. |
| `resetAction(id)` | boolean | Restores the action's declared default key. |
| `isGameMenuOpen()` | boolean | True while the shared in-game bindings menu is open. Check this when polling held state manually. |

The DLL deduplicates registrations by ID. It is safe for several instances of
one scripted object to register the same action, although a player/game script
is normally the right owner for a truly global feature.

The injected runtime elects one client Lua sandbox as its owner. This prevents
one physical key press from creating duplicate autonomous effects when the DLL
observes several sandbox environments in the same process.

Runtime callbacks are dispatched only while Scrap Mechanic is executing a
confirmed client callback. This is required for APIs such as `sm.gui` and
`sm.effect`; invoking them from a server callback causes a sandbox violation.
The native `lua_close` lifecycle hook releases ownership when a world closes,
so a later world can create its own runtime in the same game process.

### Blockless extension discovery

For a feature that must start automatically, place `keybindbridge.json` beside
the extension mod's `description.json`:

```json
{
  "version": 1,
  "script": "Scripts/KeybindBridgeExtension.lua"
}
```

The DLL scans enabled/local mod locations at process startup, reads that Lua
file from disk and executes it through `luaL_loadbuffer` in the shared client
sandbox. The script exports itself by registering an extension:

```lua
KeybindBridgeRuntime.registerExtension({
    id = "yourname.backpack",
    -- Optional throttle for onUpdate; input callbacks are not delayed.
    updateIntervalMs = 16,
    actions = {
        {
            id = "yourname.backpack.open",
            label = "Open backpack",
            defaultKey = "B"
        }
    },
    onActionPressed = function(self, actionId)
        if actionId == "yourname.backpack.open" then
            -- Open the client UI here.
        end
    end,
    onUpdate = function(self)
        -- Optional per-frame client work.
    end
})
```

Use a globally unique extension `id` and action IDs. Keep the manifest path
relative, inside the mod, and use forward slashes. Restart the game after adding
or removing a manifest because discovery happens when the DLL initializes.
Keep top-level extension code limited to definitions and `registerExtension`;
gameplay work belongs in callbacks. `onCreate` is deferred until the local
player exists.

Blockless extensions are best suited to local GUI, camera, audio and visual
features. Code that changes authoritative inventory, containers or world state
must use an ordinary game script with `self.network`, send a request to the
server and validate the player and operation there.

`examples/autonomous-flashlight-mod` is the blockless reference implementation:
enabling the mod registers a directional flashlight action without placing an
inventory object. Its custom narrow `spotLight` is engine-hosted on the
character's `jnt_head` bone. Only the bone-local offset is refreshed from
`sm.camera.getDirection()` so pitch and fast yaw stay aligned with the
crosshair; no world `setPosition`/`setRotation` calls are used. The local-player
first-person query selects a 0.005 m forward offset, while third person uses
0.10 m; both use 0.07 m upward. The first-person emitter stays bone-hosted to
preserve dynamic shadows and applies only the vertical eye-height change while
crouching; it is never moved to the camera world position. Its effect-set
is stored in the core `lua-mod`, because the native
loader executes extensions in the core runtime sandbox. A failed custom-effect
probe falls back once to the built-in `HeadLight`. Manual toggles queue the
vanilla `Button - On` / `Button - Off` effect-set entries through
`sm.effect.playEffect` at the character position and retry after no-world
callbacks, without a top-screen ON/OFF alert.

## Private/raw bindings

Use the raw VK API below for bindings owned by individual world objects, such as
separate Keybind Logic blocks. They are deliberately absent from the `End` menu;
the object stores its key in Scrap Mechanic world storage.

## Functions

| Function | Result | Notes |
|---|---|---|
| `isDown(vk)` | boolean | True while the key is physically held and Scrap Mechanic is foreground. |
| `pressSerial(vk)` | number | Increases on each up-to-down edge. Non-consuming, so many mods can observe it. |
| `releaseSerial(vk)` | number | Increases on each down-to-up edge. |
| `beginCapture()` | number | Starts a capture and returns its token. Keys already held at that moment are ignored until released. |
| `captureNext(token)` | number or nil | Returns the next captured virtual-key code once. |
| `cancelCapture(token)` | nothing | Cancels capture only if the token still owns it. |
| `keyName(vk)` | string | Localized Windows key name where available. |
| `isForeground()` | boolean | True when a window owned by this process is foreground. |
| `monotonicMilliseconds()` | number | Monotonic DLL clock for throttling client work without relying on world state. |
| `version()` | string | Native module version. |

`keybind.VK` contains common constants (`TAB`, `SPACE`, `LSHIFT`, `MOUSE1`,
`F1` through `F24`, etc.). Letters use Windows virtual-key values, so `F` is
`0x46`/`70` and `B` is `0x42`/`66`.

## Correct edge handling

Do not use a globally consumed `wasPressed()` flag. Keep the last serial in each
consumer instead:

```lua
self.lastF = keybind.pressSerial(0x46)

function Example.client_onFixedUpdate(self)
    local now = keybind.pressSerial(0x46)
    if now ~= self.lastF then
        self.lastF = now
        -- F was pressed at least once since the previous update.
    end
end
```

The sampler runs outside Lua at 250 Hz. It never calls Lua from its worker thread.
