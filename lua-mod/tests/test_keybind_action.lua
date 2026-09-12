local state = {
    key = 70,
    down = false,
    press = 0,
    release = 0,
    registrations = 0,
    menuOpen = false
}

sm = {
    keybind = {
        registerAction = function(id, label, defaultKey)
            assert(id == "test.flashlight")
            assert(label == "Flashlight")
            assert(defaultKey == "F")
            state.registrations = state.registrations + 1
            return state.key
        end,
        actionKey = function(id) return state.key end,
        actionKeyName = function(id) return "F" end,
        actionIsDown = function(id) return state.down end,
        actionPressSerial = function(id) return state.press end,
        actionReleaseSerial = function(id) return state.release end,
        isGameMenuOpen = function() return state.menuOpen end
    }
}

assert(loadfile("lua-mod/Scripts/KeybindAction.lua"))()
local action = KeybindAction.create("test.flashlight", "Flashlight", "F")
assert(state.registrations == 1)
assert(action:isAvailable())
assert(action:getKey() == 70)
assert(action:getKeyName() == "F")
assert(action:isDown() == false)
assert(action:wasPressed() == false)

state.down = true
state.press = 1
assert(action:isDown() == true)
assert(action:wasPressed() == true)
assert(action:wasPressed() == false)

state.down = false
state.release = 1
assert(action:wasReleased() == true)
assert(action:wasReleased() == false)

state.menuOpen = true
state.down = true
state.press = 2
assert(action:isDown() == false)
assert(action:wasPressed() == false)
state.down = false
state.release = 2
assert(action:wasReleased() == false)
state.menuOpen = false
assert(action:wasPressed() == false,
    "menu press replayed after the bindings menu closed")
assert(action:wasReleased() == false,
    "menu release replayed after the bindings menu closed")

sm.keybind = nil
local unavailable = KeybindAction.create("test.missing", "Missing", "B")
assert(unavailable:isAvailable() == false)
assert(unavailable:isDown() == false)
assert(unavailable:wasPressed() == false)
print("Lua action helper tests passed")
