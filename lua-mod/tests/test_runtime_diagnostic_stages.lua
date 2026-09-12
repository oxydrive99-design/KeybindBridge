local level = 5
local guiCalls = 0
local extensionLoads = 0
local pressReads = 0
local menuSerial = 0

local gui = {}
function gui:setText() end
function gui:open() end
function gui:close() end

sm = {
    isServerMode = function() return false end,
    localPlayer = { getPlayer = function() return {} end },
    keybind = {
        VK = { UP = 38, DOWN = 40, ENTER = 13, ESCAPE = 27, DELETE = 46 },
        diagnosticLevel = function() return level end,
        claimRuntime = function() return true end,
        claimGameMenu = function() end,
        menuToggleSerial = function() return menuSerial end,
        trace = function() end,
        pressSerial = function()
            pressReads = pressReads + 1
            return 0
        end,
        extensionCount = function() return 1 end,
        extensionScript = function() return "$CONTENT_TEST/extension.lua" end,
        actionCount = function() return 0 end,
        actionId = function() return nil end,
        actionLabel = function() return nil end,
        actionKeyName = function() return nil end,
        actionPressSerial = function() return 0 end,
        registerAction = function() end,
        beginCapture = function() return 1 end,
        captureNext = function() return nil end,
        cancelCapture = function() end,
        setActionKey = function() return true end,
        resetAction = function() return true end,
        keyName = function() return "?" end
    },
    gui = {
        createGuiFromLayout = function()
            guiCalls = guiCalls + 1
            return gui
        end
    }
}

local originalDofile = dofile
function dofile(path)
    if path == "$CONTENT_TEST/extension.lua" then
        extensionLoads = extensionLoads + 1
        return
    end
    return originalDofile(path)
end

local function runStage(expectedPressReads, expectedGuiCalls, expectedExtensionLoads)
    KeybindBridgeRuntime = nil
    assert(loadfile("lua-mod/Scripts/KeybindRuntime.lua"))()
    assert(KeybindBridgeRuntime ~= nil)
    assert(pressReads == expectedPressReads,
        "press reads: " .. tostring(pressReads) .. " expected " .. tostring(expectedPressReads))
    assert(guiCalls == expectedGuiCalls,
        "GUI calls: " .. tostring(guiCalls) .. " expected " .. tostring(expectedGuiCalls))
    assert(extensionLoads == expectedExtensionLoads,
        "extension loads: " .. tostring(extensionLoads) .. " expected " .. tostring(expectedExtensionLoads))
end

runStage(0, 0, 0)
level = 6
runStage(5, 0, 0)
level = 7
runStage(10, 0, 0)
menuSerial = menuSerial + 1
KeybindBridgeRuntime:update()
assert(guiCalls == 1)
level = 8
runStage(20, 1, 0)
KeybindBridgeRuntime:update()
assert(extensionLoads == 0)
menuSerial = menuSerial + 1
KeybindBridgeRuntime:update()
assert(guiCalls == 2)

print("Runtime diagnostic stage tests passed")
