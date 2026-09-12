# Current Blocks and Parts mod structure

Every local mod needs a version 2 `description.json`:

```json
{
  "allow_add_mods": true,
  "custom_icons": true,
  "name": "Mod name",
  "description": "Mod description",
  "type": "Blocks and Parts",
  "version": 2,
  "localId": "mod UUID"
}
```

Base database structure for a mod with parts:

```text
Objects/
└── Database/
    ├── rotationsets.rotationset
    ├── shapesets.shapedb
    └── ShapeSets/
        └── your_shapes.shapeset
```

A blockless autonomous mod does not need `ShapeSets`. Keep the required
database files empty:

```json
// Objects/Database/shapesets.shapedb
{
  "shapeSetList": []
}
```

```json
// Objects/Database/rotationsets.rotationset
{
  "rotationSet": []
}
```

To let KeybindBridge start the mod automatically, add this beside
`description.json`:

```json
// keybindbridge.json
{
  "version": 1,
  "script": "Scripts/KeybindBridgeExtension.lua"
}
```

Restart the game after adding the manifest: the DLL discovers extensions at
process startup. See `examples/autonomous-flashlight-mod` for a working mod.

Example `shapesets.shapedb` for a mod with blocks:

```json
{
  "shapeSetList": [
    "$CONTENT_DATA/Objects/Database/ShapeSets/your_shapes.shapeset"
  ]
}
```

`rotationsets.rotationset`:

```json
{
  "rotationSet": []
}
```

Additional checks before testing:

- a ShapeSet uses the `.shapeset` extension, not `.json`;
- with `custom_icons: true`, every part UUID must exist in `Gui/IconMap.xml`
  and its texture file must be present;
- a `PoseAnim...` material requires at least `pose0` in the corresponding LOD;
- the number of `subMeshList` entries must match the mesh being used;
- the autonomous flashlight defines a custom narrow `spotLight`, but its
  `Effects` database lives in `KeybindBridge Core` because extensions execute
  in the core sandbox. A first installation therefore needs both folders;
  updating only the flashlight script requires replacing only
  `KeybindBridge-Flashlight`.
