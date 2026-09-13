-- KeybindBridge 1.0.0 client runtime.
-- The DLL loads this file into one client Lua environment; no shape instance
-- is required. Keep this file compatible with Scrap Mechanic's restricted
-- sandbox (notably: no setmetatable).

if KeybindBridgeRuntime ~= nil then
    return
end

if sm == nil or sm.keybind == nil or pcall == nil or sm.isServerMode == nil then
    return
end

local modeOk, serverMode = pcall(sm.isServerMode)
if not modeOk or serverMode then
    return
end

local bridge = sm.keybind
local INPUT_RESUME_GAP_MS = 500
local diagnosticLevel = 8
if bridge.diagnosticLevel ~= nil then
    diagnosticLevel = bridge.diagnosticLevel()
end

-- Level 5 proves that the complete file can execute, claim ownership and
-- publish a tickable runtime without touching input state, GUI or extensions.
if diagnosticLevel == 5 then
    local minimalRuntime = { hasGameMenu = false }
    function minimalRuntime:update() end
    if bridge.claimRuntime ~= nil and not bridge.claimRuntime() then
        return
    end
    KeybindBridgeRuntime = minimalRuntime
    return
end

local runtime = {
    gui = nil,
    hasGameMenu = false,
    guiAttempted = false,
    clientApiMissingLogged = false,
    clientProbeLogged = false,
    clientReadyLogged = false,
    isOpen = false,
    selected = 1,
    firstVisible = 1,
    visibleRows = 10,
    captureToken = nil,
    extensions = {},
    extensionIds = {},
    lastMenuToggle = bridge.menuToggleSerial(),
    keySerials = {},
    lastInputUpdateMs = nil
}

local layout = "$CONTENT_9a2a8f43-6f74-4fc3-b25c-3e139b710001/Gui/Layouts/KeybindBridge.layout"

local function trace(message)
    if bridge.trace ~= nil then
        bridge.trace(message)
    end
end

function runtime:clientReady()
    if bridge.runtimeWarmupComplete ~= nil and not bridge.runtimeWarmupComplete() then
        return false
    end
    if sm.localPlayer == nil or sm.localPlayer.getPlayer == nil then
        if not self.clientApiMissingLogged then
            self.clientApiMissingLogged = true
            trace("local player API is unavailable in this sandbox")
        end
        return false
    end

    if not self.clientProbeLogged then
        self.clientProbeLogged = true
        trace("local player probe begin")
    end

    local ok, player = pcall(sm.localPlayer.getPlayer)
    if not ok or player == nil then
        return false
    end

    if not self.clientReadyLogged then
        self.clientReadyLogged = true
        trace("local player is ready")
    end
    return true
end

function runtime:ensureGui()
    if self.gui ~= nil then
        return true
    end
    if self.guiAttempted or diagnosticLevel < 7 or not self:clientReady() then
        return false
    end
    if sm.gui == nil or sm.gui.createGuiFromLayout == nil then
        trace("MyGUI API is unavailable")
        self.guiAttempted = true
        return false
    end

    self.guiAttempted = true
    trace("MyGUI creation begin")
    local ok, created = pcall(
        sm.gui.createGuiFromLayout,
        layout,
        false,
        { isHud = false, isInteractive = true, needsCursor = false })
    if not ok or created == nil then
        trace("MyGUI creation failed: " .. tostring(created))
        return false
    end

    self.gui = created
    self.hasGameMenu = true
    trace("MyGUI creation complete")
    return true
end

local watchedKeys = {
    up = bridge.VK.UP,
    down = bridge.VK.DOWN,
    enter = bridge.VK.ENTER,
    escape = bridge.VK.ESCAPE,
    delete = bridge.VK.DELETE
}

for name, key in pairs(watchedKeys) do
    runtime.keySerials[name] = bridge.pressSerial(key)
end

function runtime:keyPressed(name)
    local key = watchedKeys[name]
    local serial = bridge.pressSerial(key)
    if serial == self.keySerials[name] then
        return false
    end
    self.keySerials[name] = serial
    return true
end

function runtime:setStatus(text)
    if self.gui ~= nil then
        self.gui:setText("Status", text)
    end
end

function runtime:refresh()
    if self.gui == nil then
        return
    end

    local count = bridge.actionCount()
    if count <= 0 then
        self.selected = 1
        self.firstVisible = 1
    else
        if self.selected < 1 then self.selected = count end
        if self.selected > count then self.selected = 1 end
        self.firstVisible = math.floor((self.selected - 1) / self.visibleRows) * self.visibleRows + 1
    end

    for row = 1, self.visibleRows do
        local index = self.firstVisible + row - 1
        local text = ""
        if index <= count then
            local marker = index == self.selected and ">  " or "   "
            local id = bridge.actionId(index)
            local label = bridge.actionLabel(index) or id or "?"
            local keyName = id and bridge.actionKeyName(id) or "?"
            text = marker .. label .. "    [" .. (keyName or "?") .. "]"
        end
        self.gui:setText("Action" .. row, text)
    end

    self.gui:setText("Counter", tostring(count) .. " action(s)")
end

function runtime:open()
    if self.gui == nil then
        return
    end
    self:refresh()
    self:setStatus("Up/Down: select    Enter: rebind    Delete: reset    Esc/End: close")
    self.gui:open()
    self.isOpen = true
    if bridge.setGameMenuOpen ~= nil then
        bridge.setGameMenuOpen(true)
    end
end

function runtime:close()
    if self.captureToken ~= nil then
        bridge.cancelCapture(self.captureToken)
        self.captureToken = nil
    end
    if self.gui ~= nil then
        self.gui:close()
    end
    self.isOpen = false
    if bridge.setGameMenuOpen ~= nil then
        bridge.setGameMenuOpen(false)
    end
end

function runtime:beginCapture()
    local id = bridge.actionId(self.selected)
    if id == nil then
        self:setStatus("No action selected")
        return
    end
    self.captureToken = bridge.beginCapture()
    self:setStatus("Press a new key    Escape: cancel    End: close")
end

function runtime:updateCapture()
    local captured = bridge.captureNext(self.captureToken)
    if captured == nil then
        return
    end

    self.captureToken = nil
    if captured == bridge.VK.ESCAPE then
        -- The capture sampler and the menu navigation observe the same raw
        -- Escape edge. Consume it here so cancelling a rebind does not also
        -- close the whole menu on the next callback.
        self.keySerials.escape = bridge.pressSerial(bridge.VK.ESCAPE)
        self:setStatus("Binding cancelled")
        return
    end

    local id = bridge.actionId(self.selected)
    if id ~= nil and bridge.setActionKey(id, captured) then
        self:setStatus("Saved: " .. (bridge.keyName(captured) or "?"))
        self:refresh()
    end
end

function runtime.registerExtension(extension)
    if extension == nil or extension.id == nil or runtime.extensionIds[extension.id] then
        return false
    end

    extension._actions = extension.actions or {}
    for _, action in ipairs(extension._actions) do
        bridge.registerAction(action.id, action.label or action.id, action.defaultKey or "F")
        action._lastPressSerial = bridge.actionPressSerial(action.id)
    end

    runtime.extensionIds[extension.id] = true
    runtime.extensions[#runtime.extensions + 1] = extension
    extension._created = false
    extension._lastUpdateMs = nil
    runtime:refresh()
    return true
end

function runtime:gameplayInputBlocked()
    if self.isOpen then
        return true
    end
    if sm.gui == nil or sm.gui.hasActiveGui == nil then
        return false
    end

    local ok, active = pcall(sm.gui.hasActiveGui)
    return ok and active == true
end

function runtime:updateExtensions()
    local updateNow = bridge.monotonicMilliseconds
        and bridge.monotonicMilliseconds() or nil
    local resumedAfterGap = updateNow ~= nil
        and self.lastInputUpdateMs ~= nil
        and updateNow - self.lastInputUpdateMs > INPUT_RESUME_GAP_MS
    self.lastInputUpdateMs = updateNow
    local inputBlocked = self:gameplayInputBlocked() or resumedAfterGap

    for _, extension in ipairs(self.extensions) do
        if not extension._created then
            extension._created = true
            if extension.onCreate then
                local ok, errorText = pcall(extension.onCreate, extension)
                if not ok then
                    trace("extension onCreate failed [" .. extension.id .. "]: " .. tostring(errorText))
                end
            end
        end
        for _, action in ipairs(extension._actions) do
            local serial = bridge.actionPressSerial(action.id)
            if serial ~= action._lastPressSerial then
                action._lastPressSerial = serial
                -- Raw Win32 input continues while any game GUI is open. Always
                -- consume the serial, but never dispatch it from inventory,
                -- containers, the pause menu or our bindings menu. A long Lua
                -- update gap is treated the same way so an input sampled while
                -- paused cannot replay on the first frame after returning.
                if not inputBlocked and extension.onActionPressed then
                    trace("action pressed: " .. action.id)
                    local ok, errorText = pcall(
                        extension.onActionPressed, extension, action.id)
                    if not ok then
                        trace("action callback failed [" .. action.id .. "]: " .. tostring(errorText))
                    end
                end
            end
        end
        local updateDue = extension.onUpdate ~= nil
        if updateDue and type(extension.updateIntervalMs) == "number"
            and extension.updateIntervalMs > 0 and updateNow ~= nil then
            updateDue = extension._lastUpdateMs == nil
                or updateNow - extension._lastUpdateMs >= extension.updateIntervalMs
        end
        if updateDue then
            extension._lastUpdateMs = updateNow
            local ok, errorText = pcall(extension.onUpdate, extension)
            if not ok and not extension._updateErrorLogged then
                extension._updateErrorLogged = true
                trace("extension onUpdate failed [" .. extension.id .. "]: " .. tostring(errorText))
            end
        end
    end
end

function runtime:update()
    if diagnosticLevel >= 7 then
        local menuToggle = bridge.menuToggleSerial()
        if menuToggle ~= self.lastMenuToggle then
            self.lastMenuToggle = menuToggle
            if self.isOpen then
                self:close()
            elseif self:ensureGui() then
                self:open()
            end
        end
    end

    if self.isOpen then
        if self.captureToken ~= nil then
            self:updateCapture()
        else
            local count = bridge.actionCount()
            -- MyGUI can hide itself on Escape before Lua receives another
            -- callback. Mirror that close in runtime state so bindings remain
            -- committed and gameplay actions are no longer suppressed.
            if self:keyPressed("escape") then
                self:close()
            elseif self:keyPressed("up") and count > 0 then
                self.selected = self.selected - 1
                self:refresh()
            elseif self:keyPressed("down") and count > 0 then
                self.selected = self.selected + 1
                self:refresh()
            elseif self:keyPressed("enter") then
                self:beginCapture()
            elseif self:keyPressed("delete") then
                local id = bridge.actionId(self.selected)
                if id ~= nil and bridge.resetAction(id) then
                    self:setStatus("Default binding restored")
                    self:refresh()
                end
            end
        end
    end

    if diagnosticLevel >= 8 and self:clientReady() then
        self:updateExtensions()
    end
end

if bridge.claimRuntime ~= nil and not bridge.claimRuntime() then
    return
end

KeybindBridgeRuntime = runtime
if bridge.setGameMenuOpen ~= nil then
    bridge.setGameMenuOpen(false)
end
if diagnosticLevel >= 7 then
    -- Reserve End immediately so the native Win32 fallback does not open next
    -- to the lazily-created in-game MyGUI menu.
    bridge.claimGameMenu()
end
