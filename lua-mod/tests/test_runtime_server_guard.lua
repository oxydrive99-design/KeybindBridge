local guiCalls = 0
local runtimeClaims = 0

sm = {
    isServerMode = function() return true end,
    keybind = {
        claimRuntime = function()
            runtimeClaims = runtimeClaims + 1
            return true
        end
    },
    gui = {
        createGuiFromLayout = function()
            guiCalls = guiCalls + 1
            error("server must never create a client GUI")
        end
    }
}

assert(loadfile("lua-mod/Scripts/KeybindRuntime.lua"))()
assert(KeybindBridgeRuntime == nil)
assert(guiCalls == 0)
assert(runtimeClaims == 0)

print("Server runtime guard tests passed")
