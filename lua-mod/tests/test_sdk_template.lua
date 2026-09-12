local registered = nil
local messages = {}
local traces = {}
local keysDown = {}
local menuOpen = false

sm = {
    gui = {
        displayAlertText = function(message, seconds)
            assert(seconds == 2)
            messages[#messages + 1] = message
        end
    },
    keybind = {
        trace = function(message)
            traces[#traces + 1] = message
        end,
        isGameMenuOpen = function()
            return menuOpen
        end,
        actionIsDown = function(actionId)
            return keysDown[actionId] == true
        end
    }
}

KeybindBridgeRuntime = {
    registerExtension = function(extension)
        registered = extension
        return true
    end
}

assert(loadfile(
    "examples/sdk-template-mod/Scripts/KeybindBridgeExtension.lua"))()
assert(registered ~= nil)
assert(registered.id == "example.keybindbridge.extension")
assert(#registered.actions == 2)

registered:onCreate()
assert(messages[#messages] == "KeybindBridge SDK example loaded")

registered:onActionPressed("example.keybindbridge.press")
assert(messages[#messages] == "Press action received")

keysDown["example.keybindbridge.hold"] = true
registered:onUpdate()
assert(messages[#messages] == "Hold started")

menuOpen = true
registered:onUpdate()
assert(messages[#messages] == "Hold released")

registered:onUpdate()
assert(messages[#messages] == "Hold released",
    "unchanged hold state emitted duplicate messages")
assert(#traces == #messages)

print("SDK template tests passed")
