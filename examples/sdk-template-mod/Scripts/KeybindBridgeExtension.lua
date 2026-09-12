-- Ready-to-copy blockless KeybindBridge extension.
-- Before publishing, replace every "example.keybindbridge" prefix with a
-- globally unique prefix belonging to your own mod.

local PRESS_ACTION = "example.keybindbridge.press"
local HOLD_ACTION = "example.keybindbridge.hold"

local extension = {
    id = "example.keybindbridge.extension",
    updateIntervalMs = 16,
    actions = {
        {
            id = PRESS_ACTION,
            label = "SDK example: press",
            defaultKey = "K"
        },
        {
            id = HOLD_ACTION,
            label = "SDK example: hold",
            defaultKey = "H"
        }
    },
    holdWasDown = false
}

local function showMessage(message)
    if sm.gui ~= nil and sm.gui.displayAlertText ~= nil then
        pcall(sm.gui.displayAlertText, message, 2)
    end
    if sm.keybind.trace ~= nil then
        sm.keybind.trace("SDK example: " .. message)
    end
end

function extension:onCreate()
    self.holdWasDown = false
    showMessage("KeybindBridge SDK example loaded")
end

function extension:onActionPressed(actionId)
    if actionId == PRESS_ACTION then
        showMessage("Press action received")
    end
end

function extension:onUpdate()
    -- onActionPressed is automatically suppressed by the runtime while the
    -- bindings menu is open. A manually polled HOLD action must also respect
    -- the shared menu flag.
    local menuOpen = sm.keybind.isGameMenuOpen ~= nil
        and sm.keybind.isGameMenuOpen()
    local isDown = not menuOpen and sm.keybind.actionIsDown(HOLD_ACTION)
    if isDown == self.holdWasDown then
        return
    end

    self.holdWasDown = isDown
    showMessage(isDown and "Hold started" or "Hold released")
end

KeybindBridgeRuntime.registerExtension(extension)
