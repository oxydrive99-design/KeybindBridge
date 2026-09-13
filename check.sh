#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")" && pwd)"

find "$project_dir/lua-mod" "$project_dir/examples" \
  \( -name '*.json' -o -name '*.shapedb' -o -name '*.shapeset' -o -name '*.rotationset' \) \
  ! -path '*/CraftingRecipes/*' \
  -print0 |
  while IFS= read -r -d '' json_file; do
    jq empty "$json_file"
  done

jq -e '
  .["KeybindBridge - Character Flashlight"].effectList[0].type == "spotLight"
  and .["KeybindBridge - Character Flashlight"].parameterList.ambientIntensityScale == 0.0
' "$project_dir/lua-mod/Effects/Database/EffectSets/keybindbridge_effects.json" \
  >/dev/null

if rg -q ':set(Position|Rotation)\(|FluorescentLight' \
  "$project_dir/examples/autonomous-flashlight-mod/Scripts/KeybindBridgeExtension.lua"; then
  echo "Hosted flashlight regressed to manual world transforms" >&2
  exit 1
fi

if rg -q 'NoteTerminal-Interact|sm\.audio\.play' \
  "$project_dir/examples/autonomous-flashlight-mod/Scripts/KeybindBridgeExtension.lua"; then
  echo "Flashlight regressed to an incorrect toggle sound" >&2
  exit 1
fi

if ! rg -q 'TOGGLE_ON_EFFECT = "Button - On"' \
  "$project_dir/examples/autonomous-flashlight-mod/Scripts/KeybindBridgeExtension.lua"; then
  echo "Flashlight vanilla button effect is missing" >&2
  exit 1
fi

if ! rg -q 'tryPendingToggleSound|MAX_TOGGLE_SOUND_ATTEMPTS' \
  "$project_dir/examples/autonomous-flashlight-mod/Scripts/KeybindBridgeExtension.lua"; then
  echo "Flashlight sound retry queue is missing" >&2
  exit 1
fi

if command -v xmllint >/dev/null 2>&1; then
  find "$project_dir/lua-mod" "$project_dir/examples" \
    \( -name '*.xml' -o -name '*.layout' \) -print0 |
    xargs -0 --no-run-if-empty xmllint --noout
fi

if [[ -f "$project_dir/lua-mod/tests/check_syntax.lua" ]] \
  && command -v lua >/dev/null 2>&1; then
  (
    cd "$project_dir"
    while IFS= read -r -d '' lua_file; do
      lua lua-mod/tests/check_syntax.lua "$lua_file"
    done < <(find lua-mod examples -name '*.lua' -print0)
    lua lua-mod/tests/test_keybind_logic.lua
    lua lua-mod/tests/test_mode_controller.lua
    lua lua-mod/tests/test_keybind_action.lua
    lua lua-mod/tests/test_sdk_template.lua
    lua lua-mod/tests/test_autonomous_runtime.lua
    lua lua-mod/tests/test_autonomous_runtime_fallback.lua
    lua lua-mod/tests/test_runtime_single_owner.lua
    lua lua-mod/tests/test_runtime_server_guard.lua
    lua lua-mod/tests/test_runtime_diagnostic_stages.lua
  )
elif [[ -f "$project_dir/lua-mod/tests/check_syntax.lua" ]] \
  && command -v texlua >/dev/null 2>&1; then
  (
    cd "$project_dir"
    while IFS= read -r -d '' lua_file; do
      texlua lua-mod/tests/check_syntax.lua "$lua_file"
    done < <(find lua-mod examples -name '*.lua' -print0)
    texlua lua-mod/tests/test_keybind_logic.lua
    texlua lua-mod/tests/test_mode_controller.lua
    texlua lua-mod/tests/test_keybind_action.lua
    texlua lua-mod/tests/test_sdk_template.lua
    texlua lua-mod/tests/test_autonomous_runtime.lua
    texlua lua-mod/tests/test_autonomous_runtime_fallback.lua
    texlua lua-mod/tests/test_runtime_single_owner.lua
    texlua lua-mod/tests/test_runtime_server_guard.lua
    texlua lua-mod/tests/test_runtime_diagnostic_stages.lua
  )
elif command -v luac >/dev/null 2>&1; then
  luac -p "$project_dir/lua-mod/Scripts/KeybindLogic.lua"
fi

if ! rg -q 'kVersion = "1\.0\.0"' \
  "$project_dir/native/src/keybind_bridge.cpp"; then
  echo "Native release version is not 1.0.0" >&2
  exit 1
fi

if ! rg -q 'g_featureLevel\{8\}' \
  "$project_dir/native/src/keybind_bridge.cpp" || \
  ! rg -q 'GetPrivateProfileIntW\(L"Diagnostics", L"FeatureLevel", 8, path\)' \
  "$project_dir/native/src/keybind_bridge.cpp"; then
  echo "Missing diagnostics file must default FeatureLevel to 8" >&2
  exit 1
fi

if ! rg -q 'CREATE_ALWAYS' "$project_dir/native/src/keybind_bridge.cpp"; then
  echo "DLL log is not reset for each process launch" >&2
  exit 1
fi

if rg -q 'Copy-Item .*KeybindBridge\.diagnostics\.ini' \
  "$project_dir/scripts/package-release.ps1"; then
  echo "Windows release still requires the optional diagnostics file" >&2
  exit 1
fi

g++ -std=c++20 -pthread -Wall -Wextra -Wpedantic -Werror -fsyntax-only \
  -D_WIN32 \
  -I"$project_dir/native/tests/stubs" \
  -I"$project_dir/native/src" \
  -I"$project_dir/native/vendor/minhook/include" \
  "$project_dir/native/src/keybind_bridge.cpp"

echo "KeybindBridge checks passed"
