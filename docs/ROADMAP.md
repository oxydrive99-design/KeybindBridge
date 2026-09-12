# KeybindBridge status

## Implemented

- in-game MyGUI opened with `End`; the external Win32 UI does not open at
  FeatureLevel 8;
- blockless extension loading through `keybindbridge.json` without placing an
  activation block;
- shared autonomous action list, rebinding and persistence;
- simple `registerAction` API and `KeybindAction` helper for modders;
- private Keybind Logic bindings absent from the shared menu and seat hotbar;
- exact seat validation, passenger isolation and stale input suppression;
- separate Mode Controller for HOLD/TOGGLE/PULSE;
- runtime recovery after leaving and re-entering a world;
- autonomous flashlight mod with a narrow engine-hosted `spotLight` on the
  character's head bone;
- complete English and Russian player/modder documentation.

## Stable 1.0.0 foundation

- custom directional `spotLight` without the ambient haze of
  `FluorescentLight`;
- engine hosting on `jnt_head`, avoiding per-frame world-transform updates;
- accurate direction from `sm.camera.getDirection()` through refreshed local
  offsets, including pitch and fast yaw;
- first-person offsets 0.005/0.07 and third-person offsets 0.10/0.07;
- first-person emitter stays on `jnt_head` to preserve dynamic shadows;
- crouching changes only the vertical eye-height correction instead of moving
  the light to the camera;
- MyGUI and runtime close together on `Esc` without losing the assignment;
- effect set resides in the core sandbox, with a one-time built-in `HeadLight`
  fallback if the custom name is unavailable;
- flashlight automatically turns off and remains blocked in seats and beds;
- autonomous actions are suppressed while the bindings menu is open;
- Mode Controller uses valid power values in the `-1..1` range;
- exact vanilla `Button - On` / `Button - Off` effects play at the character
  without a top-screen status message;
- audio requests queue and retry after callbacks that temporarily lack world
  context.

## Possible future versions

- bring the menu visuals closer to the vanilla Scrap Mechanic settings while
  preserving input blocking;
- add localized action names and menu hints;
- optionally add mouse-driven menu controls while keeping keyboard navigation;
- build separate mods on the API: maps, backpacks and other autonomous actions
  with their own network logic;
- later add an equipment system with dedicated slots/menu, where flashlight,
  armor, ammo belts and other modules register actions only while owned or
  equipped.
