local extensionQueries = 0

sm = {
    isServerMode = function() return false end,
    keybind = {
        VK = { UP = 38, DOWN = 40, ENTER = 13, ESCAPE = 27, DELETE = 46 },
        pressSerial = function() return 0 end,
        menuToggleSerial = function() return 0 end,
        claimRuntime = function() return false end,
        extensionCount = function()
            extensionQueries = extensionQueries + 1
            return 1
        end
    },
    gui = {
        createGuiFromLayout = function()
            return {
                setText = function() end,
                open = function() end,
                close = function() end
            }
        end
    }
}

assert(loadfile("lua-mod/Scripts/KeybindRuntime.lua"))()
assert(KeybindBridgeRuntime == nil)
assert(extensionQueries == 0)

print("Single runtime owner tests passed")
