local actions = {}
local order = {}
local actionPress = {}
local claimed = false
local nowMs = 0

local createdEffectNames = {}
local customEffect = { playing = false }
local effect = { playing = false }

local function installEffectMethods(target, startsSuccessfully)
    function target:start() self.playing = startsSuccessfully end
    function target:stop() self.playing = false end
    function target:destroy() self.playing = false; self.destroyed = true end
    function target:isPlaying() return self.playing end
    function target:setPosition(position) self.position = position end
    function target:setRotation(rotation) self.rotation = rotation end
    function target:setOffsetPosition(position) self.offsetPosition = position end
    function target:setOffsetRotation(rotation) self.offsetRotation = rotation end
end

installEffectMethods(customEffect, false)
installEffectMethods(effect, true)

local Vec = {}
Vec.__index = Vec
function Vec.new(x, y, z) return setmetatable({ x = x, y = y, z = z }, Vec) end
function Vec.__add(a, b) return Vec.new(a.x + b.x, a.y + b.y, a.z + b.z) end
function Vec.__sub(a, b) return Vec.new(a.x - b.x, a.y - b.y, a.z - b.z) end
function Vec.__mul(a, value) return Vec.new(a.x * value, a.y * value, a.z * value) end
function Vec:length() return math.sqrt(self.x * self.x + self.y * self.y + self.z * self.z) end
function Vec:length2() return self.x * self.x + self.y * self.y + self.z * self.z end

local Quat = {}
Quat.__index = Quat
function Quat.__mul(a, b)
    if getmetatable(b) == Vec then return b end
    return setmetatable({}, Quat)
end
function Quat:inverse() return setmetatable({}, Quat) end

local character = {
    worldPosition = Vec.new(10, 20, 30),
    direction = Vec.new(0, 1, 0),
    lockingInteractable = nil
}
function character:isCrouching() return false end
function character:isSeated() return false end
function character:getLockingInteractable() return self.lockingInteractable end
function character:getSmoothViewDirection() return self.direction end
function character:getTpBoneRot() return setmetatable({}, Quat) end
local localPlayer = { character = character }

sm = {
    isServerMode = function() return false end,
    localPlayer = { getPlayer = function() return localPlayer end },
    keybind = {
        VK = { UP = 38, DOWN = 40, ENTER = 13, ESCAPE = 27, DELETE = 46 },
        registerAction = function(id, label, defaultKey)
            if actions[id] == nil then order[#order + 1] = id end
            actions[id] = { label = label, key = defaultKey }
            actionPress[id] = actionPress[id] or 0
        end,
        actionCount = function() return #order end,
        actionId = function(index) return order[index] end,
        actionLabel = function(index)
            local id = order[index]
            return id and actions[id].label or nil
        end,
        actionKeyName = function(id) return actions[id] and actions[id].key or nil end,
        actionPressSerial = function(id) return actionPress[id] or 0 end,
        pressSerial = function() return 0 end,
        menuToggleSerial = function() return 0 end,
        monotonicMilliseconds = function()
            nowMs = nowMs + 50
            return nowMs
        end,
        claimRuntime = function() return true end,
        claimGameMenu = function() claimed = true end,
        extensionCount = function() return 1 end,
        extensionScript = function(index)
            if index == 1 then return "$CONTENT_TEST/Scripts/KeybindBridgeExtension.lua" end
        end,
        beginCapture = function() return 1 end,
        captureNext = function() return nil end,
        cancelCapture = function() end,
        setActionKey = function() return true end,
        resetAction = function() return true end,
        keyName = function(key) return tostring(key) end
    },
    gui = {
        createGuiFromLayout = function()
            error("layout is unavailable")
        end
    },
    audio = {
        play = function() end
    },
    effect = {
        createEffect = function(name, host, bone)
            assert(name == "KeybindBridge - Character Flashlight"
                or name == "HeadLight")
            assert(host == character)
            assert(bone == "jnt_head")
            createdEffectNames[#createdEffectNames + 1] = name
            local result = name == "HeadLight" and effect or customEffect
            result.destroyed = false
            return result
        end
    },
    exists = function(value) return value ~= nil and not value.destroyed end,
    quat = {
        angleAxis = function() return setmetatable({}, Quat) end
    },
    vec3 = {
        new = Vec.new,
        getRotation = function() return setmetatable({}, Quat) end
    },
    camera = {
        getPosition = function() return Vec.new(1, 2, 3) end,
        getDirection = function() return Vec.new(0, 1, 0) end,
        getRotation = function() return { rotation = true } end
    }
}

local originalDofile = dofile
function dofile(path)
    if path == "$CONTENT_TEST/Scripts/KeybindBridgeExtension.lua" then
        path = "examples/autonomous-flashlight-mod/Scripts/KeybindBridgeExtension.lua"
    end
    return originalDofile(path)
end

assert(loadfile("lua-mod/Scripts/KeybindRuntime.lua"))()
assert(KeybindBridgeRuntime ~= nil)
assert(not KeybindBridgeRuntime.hasGameMenu)
assert(claimed)
assert(#order == 0)
dofile("$CONTENT_TEST/Scripts/KeybindBridgeExtension.lua")
KeybindBridgeRuntime:update()
assert(#order == 1)

actionPress[order[1]] = 1
KeybindBridgeRuntime:update()
assert(effect.playing)
assert(customEffect.destroyed and not customEffect.playing)
assert(createdEffectNames[1] == "KeybindBridge - Character Flashlight")
assert(createdEffectNames[2] == "HeadLight")

print("Autonomous runtime fallback tests passed")
