local keybind = sm and sm.keybind or nil

ModeController = class(nil)
ModeController.maxParentCount = -1
ModeController.maxChildCount = -1
ModeController.connectionInput = sm.interactable.connectionType.power
    + sm.interactable.connectionType.logic
ModeController.connectionOutput = sm.interactable.connectionType.power
    + sm.interactable.connectionType.logic
ModeController.colorNormal = sm.color.new(0xe58b2fff)
ModeController.colorHighlight = sm.color.new(0xffbd66ff)
ModeController.poseWeightCount = 1

local CAPTURE_TIMEOUT_TICKS = 400
local VALID_MODES = { hold = true, toggle = true, pulse = true }
-- Interactable power is clamped to [-1, 1]. Use three values inside that
-- documented range as the private command sent to Keybind Logic.
local MODE_POWER = { hold = -1, toggle = 0, pulse = 1 }

local function validKey(value)
    return value == nil or (type(value) == "number" and value >= 1
        and value <= 254 and value == math.floor(value))
end

local function validMode(value)
    return type(value) == "string" and VALID_MODES[value] == true
end

local function nextMode(mode)
    if mode == "hold" then return "toggle" end
    if mode == "toggle" then return "pulse" end
    return "hold"
end

local function connectedSeat(interactable)
    for _, parent in ipairs(interactable:getParents()) do
        if parent ~= nil and sm.exists(parent) and parent:hasSeat() then
            return parent
        end
    end
    return nil
end

local function playerInConnectedSeat(interactable, player)
    local character = player and player.character or nil
    if character == nil or not sm.exists(character) then return nil end
    local seat = connectedSeat(interactable)
    if seat == nil or character:getLockingInteractable() ~= seat then return nil end
    return seat
end

local function playerNear(self, player)
    local character = player and player.character or nil
    return character ~= nil and sm.exists(character)
        and (character.worldPosition - self.shape.worldPosition):length() <= 5.0
end

local function logicInputActive(interactable)
    for _, parent in ipairs(interactable:getParents()) do
        if parent ~= nil and sm.exists(parent) and not parent:hasSeat()
            and parent.active then
            return true
        end
    end
    return false
end

function ModeController.server_onCreate(self)
    local saved = self.storage:load() or {}
    self.sv = {
        key = validKey(saved.key) and saved.key or nil,
        mode = validMode(saved.mode) and saved.mode or "hold",
        inputInitialized = false,
        lastInputActive = false
    }
    self:sv_publish()
end

function ModeController.server_onFixedUpdate(self)
    local active = logicInputActive(self.interactable)
    if not self.sv.inputInitialized then
        -- A signal already held while loading/connecting must not cause an
        -- accidental mode change.
        self.sv.inputInitialized = true
        self.sv.lastInputActive = active
        return
    end

    if active and not self.sv.lastInputActive then
        self:sv_cycle()
    end
    self.sv.lastInputActive = active
end

function ModeController.sv_publish(self)
    self.storage:save({ key = self.sv.key, mode = self.sv.mode })
    -- The stable power value is a private command for connected Keybind Logic
    -- blocks. Keeping active false prevents this controller acting like a
    -- permanently-on ordinary logic gate.
    self.interactable:setActive(false)
    self.interactable:setPower(MODE_POWER[self.sv.mode])
    self.network:setClientData({ key = self.sv.key, mode = self.sv.mode })
end

function ModeController.sv_cycle(self)
    self.sv.mode = nextMode(self.sv.mode)
    self:sv_publish()
end

function ModeController.sv_cycleFromKeyboard(self, _, player)
    if playerInConnectedSeat(self.interactable, player) ~= nil then
        self:sv_cycle()
    end
end

function ModeController.sv_cycleManually(self, _, player)
    if playerNear(self, player) then
        self:sv_cycle()
    end
end

function ModeController.sv_setBinding(self, key, player)
    if not validKey(key) or not playerNear(self, player) then return end
    self.sv.key = key
    self:sv_publish()
end

function ModeController.client_onCreate(self)
    self.cl = {
        key = nil,
        mode = "hold",
        capturing = false,
        captureToken = nil,
        captureTicks = 0,
        lastPressSerial = 0,
        wasSeated = false,
        waitingForRelease = false
    }
end

function ModeController.client_onDestroy(self)
    if keybind and self.cl.captureToken then
        keybind.cancelCapture(self.cl.captureToken)
    end
end

function ModeController.client_onClientDataUpdate(self, data)
    if type(data) ~= "table" then return end
    if validKey(data.key) then self.cl.key = data.key end
    if validMode(data.mode) then self.cl.mode = data.mode end
    self.cl.lastPressSerial = keybind and self.cl.key
        and keybind.pressSerial(self.cl.key) or 0
    self.cl.wasSeated = false
    self.cl.waitingForRelease = false
end

function ModeController.client_canInteract(self, character)
    if keybind then
        local keyName = self.cl.key and keybind.keyName(self.cl.key) or "NOT BOUND"
        sm.gui.setInteractionText("", sm.gui.getKeyBinding("Use"),
            "Mode key: " .. keyName .. " [" .. string.upper(self.cl.mode) .. "]")
    else
        sm.gui.setInteractionText("", sm.gui.getKeyBinding("Use"),
            "KeybindBridge DLL is missing")
    end
    return true
end

function ModeController.client_onInteract(self, character, state)
    if not state then return end
    if not keybind then
        sm.gui.displayAlertText(
            "KeybindBridge: install keybind_bridge.dll into Release/DLLModules", 4)
        return
    end
    self.cl.captureToken = keybind.beginCapture()
    self.cl.capturing = true
    self.cl.captureTicks = 0
    sm.gui.displayAlertText("Press a mode key (Esc cancels)", 4)
end

function ModeController.client_canTinker(self, character)
    sm.gui.setInteractionText("", sm.gui.getKeyBinding("Tinker"),
        "Cycle mode [U]: " .. string.upper(self.cl.mode))
    return true
end

function ModeController.client_onTinker(self, character, state)
    if state then self.network:sendToServer("sv_cycleManually") end
end

function ModeController.client_onFixedUpdate(self)
    local pose = self.cl.mode == "hold" and 0
        or (self.cl.mode == "toggle" and 0.5 or 1)
    self.interactable:setPoseWeight(0, pose)

    if not keybind then return end

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
                self.network:sendToServer("sv_setBinding", captured)
                sm.gui.displayAlertText("Mode key: " .. keybind.keyName(captured), 2)
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

    if self.cl.key == nil then return end
    local serial = keybind.pressSerial(self.cl.key)
    local localPlayer = sm.localPlayer.getPlayer()
    if playerInConnectedSeat(self.interactable, localPlayer) == nil then
        self.cl.lastPressSerial = serial
        self.cl.wasSeated = false
        self.cl.waitingForRelease = false
        return
    end

    if not self.cl.wasSeated then
        self.cl.wasSeated = true
        self.cl.lastPressSerial = serial
        self.cl.waitingForRelease = keybind.isDown(self.cl.key)
        return
    end

    if self.cl.waitingForRelease then
        self.cl.lastPressSerial = serial
        if not keybind.isDown(self.cl.key) then
            self.cl.waitingForRelease = false
        end
        return
    end

    if serial ~= self.cl.lastPressSerial then
        self.cl.lastPressSerial = serial
        self.network:sendToServer("sv_cycleFromKeyboard")
    end
end
