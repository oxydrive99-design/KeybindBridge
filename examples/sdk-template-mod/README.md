# Ready-to-copy autonomous mod template

KeybindBridge starts this mod automatically without requiring an activation
block. Enabling it adds two entries to the `End` menu:

- `SDK example: press` - a single `K` press;
- `SDK example: hold` - holding and releasing `H`.

Before creating your own mod:

1. Generate a new UUID and replace `localId` in `description.json`.
2. Rename the mod in `description.json`.
3. Replace every `example.keybindbridge` prefix in
   `Scripts/KeybindBridgeExtension.lua` with a globally unique author/mod
   prefix.
4. Change the action labels, default keys and callback bodies.
5. Keep the path in `keybindbridge.json` unchanged unless you rename the Lua
   file.

Keep top-level Lua code limited to declarations and
`KeybindBridgeRuntime.registerExtension(extension)`. Make game API calls from
`onCreate`, `onActionPressed` or `onUpdate`.

This is sufficient for UI, flashlight and other client-only features. To alter
authoritative inventory, containers or world state, use the action from a
normal scripted-object/player/tool script and send a request through
`self.network`, with mandatory server-side validation.
