local keybind = sm and sm.keybind or nil
local nativeLoaded = keybind ~= nil
print("[KeybindBridge] sm.keybind = " .. tostring(keybind))

KeybindLogic = class(nil)
-- One parent is the driver's seat and the optional second parent is a Mode
-- Controller. Both connections remain outside the vanilla seat hotbar.
KeybindLogic.maxParentCount = 2
KeybindLogic.maxChildCount = -1
-- A seat can still be wired to this input, but using power + logic instead of
-- seated keeps the block out of the vanilla seat hotbar.
KeybindLogic.connectionInput = sm.interactable.connectionType.power + sm.interactable.connectionType.logic
KeybindLogic.connectionOutput = sm.interactable.connectionType.logic
    + sm.interactable.connectionType.power
    + sm.interactable.connectionType.bearing
KeybindLogic.colorNormal = sm.color.new(0x2f8ee5ff)
KeybindLogic.colorHighlight = sm.color.new(0x66b5ffff)
KeybindLogic.poseWeightCount = 1

local DEFAULT_KEY = 0x46 -- F
local MODE_CONTROLLER_UUID = sm.uuid.new("9a2a8f43-6f74-4fc3-b25c-3e139b710003")
local CAPTURE_TIMEOUT_TICKS = 400
local VALID_MODES = {
    hold = true,
    toggle = true,
    pulse = true
}

local function validKey(value)
    return type(value) == "number" and value >= 1 and value <= 254 and value == math.floor(value)
end

local function validMode(value)
    return type(value) == "string" and VALID_MODES[value] == true
end

local function connectedSeat(interactable)
    for _, parent in ipairs(interactable:getParents()) do
        if parent ~= nil and sm.exists(parent) and parent:hasSeat() then
            return parent
        end
    end
    return nil
end

local function modeFromController(interactable)
    for _, parent in ipairs(interactable:getParents()) do
        if parent ~= nil and sm.exists(parent) and parent.shape ~= nil
            and parent.shape.uuid == MODE_CONTROLLER_UUID then
            local value = parent.power or 0
            if value <= -0.5 then return "hold" end
            if value >= 0.5 then return "pulse" end
            return "toggle"
        end
    end
    return nil
end

local function playerInConnectedSeat(interactable, player)
    local character = player and player.character or nil
    if character == nil or not sm.exists(character) then
        return nil
    end

    local seat = connectedSeat(interactable)
    if seat == nil then
        return nil
    end

    if character:getLockingInteractable() ~= seat then
        return nil
    end

    return seat
end

function KeybindLogic.server_onCreate(self)
    local saved = self.storage:load() or {}
    self.sv = {
        key = validKey(saved.key) and saved.key or DEFAULT_KEY,
        mode = validMode(saved.mode) and saved.mode or "hold",
        active = false,
        driver = nil
    }

    self.interactable:setActive(false)
    self.interactable:setPower(0)
    self:sv_syncConfiguration()
end

function KeybindLogic.server_onFixedUpdate(self)
    local controlledMode = modeFromController(self.interactable)
    if controlledMode ~= nil and controlledMode ~= self.sv.mode then
        self.sv.mode = controlledMode
        self:sv_applyState(false, nil)
        self:sv_syncConfiguration()
    end

    if not self.sv.active then
        return
    end

    if playerInConnectedSeat(self.interactable, self.sv.driver) == nil then
        self:sv_applyState(false, nil)
    end
end

function KeybindLogic.sv_syncConfiguration(self)
    self.storage:save({ key = self.sv.key, mode = self.sv.mode })
    self.network:setClientData({ key = self.sv.key, mode = self.sv.mode })
end

function KeybindLogic.sv_setBinding(self, data, player)
    if type(data) ~= "table" or not validKey(data.key) then
        return
    end

    local character = player and player.character or nil
    if character == nil or not sm.exists(character) then
        return
    end

    if (character.worldPosition - self.shape.worldPosition):length() > 5.0 then
        return
    end

    self.sv.key = data.key
    if validMode(data.mode) then
        self.sv.mode = data.mode
    end
    self:sv_applyState(false, nil)
    self:sv_syncConfiguration()
end

function KeybindLogic.sv_setMode(self, mode, player)
    self:sv_setBinding({ key = self.sv.key, mode = mode }, player)
end

function KeybindLogic.sv_requestState(self, requestedState, player)
    if type(requestedState) ~= "boolean" then
        return
    end

    if playerInConnectedSeat(self.interactable, player) == nil then
        return
    end

    self:sv_applyState(requestedState, requestedState and player or nil)
end

function KeybindLogic.sv_applyState(self, state, driver)
    if self.sv.active == state and self.sv.driver == driver then
        return
    end

    self.sv.active = state
    self.sv.driver = driver
    self.interactable:setActive(state)
    self.interactable:setPower(state and 1 or 0)
end

function KeybindLogic.client_onCreate(self)
    self.cl = {
        key = DEFAULT_KEY,
        mode = "hold",
        capturing = false,
        captureToken = nil,
        captureTicks = 0,
        lastPressSerial = keybind and keybind.pressSerial(DEFAULT_KEY) or 0,
        lastSentState = false,
        toggled = false,
        pulseTicks = 0,
        wasSeated = false,
        waitingForRelease = false,
        showedMissingDll = false
    }
end

function KeybindLogic.client_onDestroy(self)
    if keybind and self.cl.captureToken then
        keybind.cancelCapture(self.cl.captureToken)
    end
end

function KeybindLogic.client_onClientDataUpdate(self, data)
    if type(data) ~= "table" then
        return
    end

    if validKey(data.key) then
        self.cl.key = data.key
        self.cl.lastPressSerial = keybind and keybind.pressSerial(data.key) or 0
    end
    if validMode(data.mode) then
        self.cl.mode = data.mode
    end

    self.cl.toggled = false
    self.cl.pulseTicks = 0
    self.cl.wasSeated = false
    self.cl.waitingForRelease = false
    self:cl_sendState(false)
end

function KeybindLogic.client_canInteract(self, character)
    if keybind then
        local modeText = self.cl.mode == "hold" and "HOLD" or string.upper(self.cl.mode)
        sm.gui.setInteractionText("", sm.gui.getKeyBinding("Use"), "Bind: " .. keybind.keyName(self.cl.key) .. " [" .. modeText .. "]")
    else
        sm.gui.setInteractionText("", sm.gui.getKeyBinding("Use"), "KeybindBridge DLL is missing")
    end
    return true
end

function KeybindLogic.client_onInteract(self, character, state)
    if not state then
        return
    end

    if not keybind then
        sm.gui.displayAlertText("KeybindBridge: install keybind_bridge.dll into Release/DLLModules", 4)
        self.cl.showedMissingDll = true
        return
    end

    self.cl.captureToken = keybind.beginCapture()
    self.cl.capturing = true
    self.cl.captureTicks = 0
    self:cl_sendState(false)
    sm.gui.displayAlertText("Press a key (Esc cancels)", 4)
end

function KeybindLogic.client_onTinker(self, character, state)
    if not state then
        return
    end

    local nextMode = "hold"
    if self.cl.mode == "hold" then
        nextMode = "toggle"
    elseif self.cl.mode == "toggle" then
        nextMode = "pulse"
    end
    self.network:sendToServer("sv_setMode", nextMode)
end

function KeybindLogic.client_canTinker(self, character)
    sm.gui.setInteractionText("", sm.gui.getKeyBinding("Tinker"), "Mode [U]: " .. string.upper(self.cl.mode))
    return true
end

function KeybindLogic.client_onFixedUpdate(self)
    self.interactable:setPoseWeight(0, self.interactable.active and 1 or 0)

    if not keybind then
        return
    end

    if self.cl.capturing then
        self.cl.captureTicks = self.cl.captureTicks + 1
        local captured = keybind.captureNext(self.cl.captureToken)
        if captured ~= nil then
            self.cl.capturing = false
            self.cl.captureToken = nil
            self.cl.captureTicks = 0

            if captured == keybind.VK.ESCAPE then
                sm.gui.displayAlertText("Key binding cancelled", 2)
            else
                self.network:sendToServer("sv_setBinding", { key = captured, mode = self.cl.mode })
                sm.gui.displayAlertText("Bound to " .. keybind.keyName(captured), 2)
            end
        elseif self.cl.captureTicks >= CAPTURE_TIMEOUT_TICKS then
            keybind.cancelCapture(self.cl.captureToken)
            self.cl.capturing = false
            self.cl.captureToken = nil
            self.cl.captureTicks = 0
            sm.gui.displayAlertText("Key binding timed out", 2)
        end
        return
    end

    local pressSerial = keybind.pressSerial(self.cl.key)
    local localPlayer = sm.localPlayer.getPlayer()
    if playerInConnectedSeat(self.interactable, localPlayer) == nil then
        -- Discard every edge produced while the player is outside the connected
        -- driver's seat. Otherwise toggle/pulse replays it after entering.
        self.cl.lastPressSerial = pressSerial
        self.cl.toggled = false
        self.cl.pulseTicks = 0
        self.cl.wasSeated = false
        self.cl.waitingForRelease = false
        self:cl_sendState(false)
        return
    end

    if not self.cl.wasSeated then
        self.cl.wasSeated = true
        self.cl.lastPressSerial = pressSerial
        self.cl.waitingForRelease = keybind.isDown(self.cl.key)
        self.cl.toggled = false
        self.cl.pulseTicks = 0
        self:cl_sendState(false)
        return
    end

    if self.cl.waitingForRelease then
        self.cl.lastPressSerial = pressSerial
        if not keybind.isDown(self.cl.key) then
            self.cl.waitingForRelease = false
        end
        self:cl_sendState(false)
        return
    end

    local pressed = pressSerial ~= self.cl.lastPressSerial
    self.cl.lastPressSerial = pressSerial

    if self.cl.mode == "hold" then
        self:cl_sendState(keybind.isDown(self.cl.key))
    elseif self.cl.mode == "toggle" then
        if pressed then
            self.cl.toggled = not self.cl.toggled
            self:cl_sendState(self.cl.toggled)
        end
    elseif self.cl.mode == "pulse" then
        if pressed then
            self.cl.pulseTicks = 2
            self:cl_sendState(true)
        elseif self.cl.pulseTicks > 0 then
            self.cl.pulseTicks = self.cl.pulseTicks - 1
            if self.cl.pulseTicks == 0 then
                self:cl_sendState(false)
            end
        end
    end
end

function KeybindLogic.cl_sendState(self, state)
    if self.cl.lastSentState == state then
        return
    end

    self.cl.lastSentState = state
    self.network:sendToServer("sv_requestState", state)
end
