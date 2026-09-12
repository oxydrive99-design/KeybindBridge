-- Small optional wrapper for autonomous KeybindBridge actions.
-- Copy this file into a mod and load it once with dofile(...).

KeybindAction = KeybindAction or {}

function KeybindAction.create(id, label, defaultKey)
    local bridge = sm and sm.keybind or nil
    local action = {
        id = id,
        bridge = bridge,
        lastPressSerial = 0,
        lastReleaseSerial = 0
    }

    -- Scrap Mechanic removes setmetatable from the mod sandbox, so the
    -- methods live directly on the returned object instead of using __index.
    action.isAvailable = KeybindAction.isAvailable
    action.isDown = KeybindAction.isDown
    action.wasPressed = KeybindAction.wasPressed
    action.wasReleased = KeybindAction.wasReleased
    action.getKey = KeybindAction.getKey
    action.getKeyName = KeybindAction.getKeyName

    if bridge and bridge.registerAction then
        bridge.registerAction(id, label, defaultKey)
        action.lastPressSerial = bridge.actionPressSerial(id)
        action.lastReleaseSerial = bridge.actionReleaseSerial(id)
    else
        action.bridge = nil
    end
    return action
end

function KeybindAction:isAvailable()
    return self.bridge ~= nil
end

local function inputSuppressed(action)
    return action.bridge ~= nil
        and action.bridge.isGameMenuOpen ~= nil
        and action.bridge.isGameMenuOpen()
end

function KeybindAction:isDown()
    return self.bridge and not inputSuppressed(self)
        and self.bridge.actionIsDown(self.id) or false
end

function KeybindAction:wasPressed()
    if not self.bridge then
        return false
    end

    local serial = self.bridge.actionPressSerial(self.id)
    if inputSuppressed(self) then
        -- Consume input produced while the shared bindings menu is open so it
        -- cannot replay as a gameplay action after the menu closes.
        self.lastPressSerial = serial
        return false
    end
    if serial == self.lastPressSerial then
        return false
    end
    self.lastPressSerial = serial
    return true
end

function KeybindAction:wasReleased()
    if not self.bridge then
        return false
    end

    local serial = self.bridge.actionReleaseSerial(self.id)
    if inputSuppressed(self) then
        self.lastReleaseSerial = serial
        return false
    end
    if serial == self.lastReleaseSerial then
        return false
    end
    self.lastReleaseSerial = serial
    return true
end

function KeybindAction:getKey()
    return self.bridge and self.bridge.actionKey(self.id) or nil
end

function KeybindAction:getKeyName()
    return self.bridge and self.bridge.actionKeyName(self.id) or "Unavailable"
end
