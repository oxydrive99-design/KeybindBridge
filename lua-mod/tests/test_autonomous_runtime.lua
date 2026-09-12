local actions = {}
local order = {}
local actionPress = {}
local rawPress = {}
local menuSerial = 0
local claimed = false
local guiOpened = false
local nativeMenuOpen = false
local guiText = {}
local nowMs = 0
local clockStepMs = 50
local cameraDirection = nil
local cameraPullback = 2
local firstPerson = false
local cameraPosition = nil
local headPosition = nil
local capturedKey = nil
local alertCount = 0
local playedSounds = {}
local audioFailures = 1

local gui = {}
function gui:setText(name, text) guiText[name] = text end
function gui:open() guiOpened = true end
function gui:close() guiOpened = false end

local effect = {
    playing = false,
    startFailures = 1,
    positionCalls = 0,
    rotationCalls = 0,
    offsetPositionCalls = 0,
    offsetRotationCalls = 0
}
function effect:start()
    if self.startFailures > 0 then
        self.startFailures = self.startFailures - 1
        error("Calling world dependent functions in a no world script!")
    end
    self.playing = true
end
function effect:stop() self.playing = false end
function effect:destroy() self.playing = false; self.destroyed = true end
function effect:isPlaying() return self.playing end
function effect:setPosition(position)
    self.positionCalls = self.positionCalls + 1
    self.position = position
end
function effect:setRotation(rotation)
    self.rotationCalls = self.rotationCalls + 1
    self.rotation = rotation
end
function effect:setOffsetPosition(position)
    self.offsetPositionCalls = self.offsetPositionCalls + 1
    self.offsetPosition = position
end
function effect:setOffsetRotation(rotation)
    self.offsetRotationCalls = self.offsetRotationCalls + 1
    self.offsetRotation = rotation
end

local Vec = {}
Vec.__index = Vec
function Vec.new(x, y, z) return setmetatable({ x = x, y = y, z = z }, Vec) end
function Vec.__add(a, b) return Vec.new(a.x + b.x, a.y + b.y, a.z + b.z) end
function Vec.__sub(a, b) return Vec.new(a.x - b.x, a.y - b.y, a.z - b.z) end
function Vec.__mul(a, value) return Vec.new(a.x * value, a.y * value, a.z * value) end
function Vec:length() return math.sqrt(self.x * self.x + self.y * self.y + self.z * self.z) end
function Vec:length2() return self.x * self.x + self.y * self.y + self.z * self.z end

cameraPosition = Vec.new(10, 20, 31)
headPosition = Vec.new(10, 20, 31)

local Quat = {}
Quat.__index = Quat
function Quat.__mul(a, b)
    if getmetatable(b) == Vec then return b end
    return setmetatable({ combined = true, target = b.to }, Quat)
end
function Quat:inverse() return setmetatable({ inverse = true }, Quat) end

local character = {
    worldPosition = Vec.new(10, 20, 30),
    direction = Vec.new(0, 1, 0),
    crouching = false,
    seated = false,
    lockingInteractable = nil
}
function character:isCrouching() return self.crouching end
function character:isSeated() return self.seated end
function character:getLockingInteractable() return self.lockingInteractable end
function character:getSmoothViewDirection() return self.direction end
function character:getTpBoneRot() return setmetatable({ bone = true }, Quat) end
function character:getTpBonePos() return headPosition end
local localPlayer = { character = character }

local VK = { UP = 38, DOWN = 40, ENTER = 13, ESCAPE = 27, DELETE = 46 }
sm = {
    isServerMode = function() return false end,
    localPlayer = {
        getPlayer = function() return localPlayer end,
        isInFirstPersonView = function() return firstPerson end
    },
    keybind = {
        VK = VK,
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
        pressSerial = function(key) return rawPress[key] or 0 end,
        menuToggleSerial = function() return menuSerial end,
        monotonicMilliseconds = function()
            nowMs = nowMs + clockStepMs
            return nowMs
        end,
        claimRuntime = function() return true end,
        claimGameMenu = function() claimed = true end,
        setGameMenuOpen = function(open) nativeMenuOpen = open end,
        isGameMenuOpen = function() return nativeMenuOpen end,
        extensionCount = function() return 1 end,
        extensionScript = function(index)
            if index == 1 then return "$CONTENT_TEST/Scripts/KeybindBridgeExtension.lua" end
        end,
        beginCapture = function() return 1 end,
        captureNext = function()
            local result = capturedKey
            capturedKey = nil
            return result
        end,
        cancelCapture = function() end,
        setActionKey = function(id, key) actions[id].key = tostring(key); return true end,
        resetAction = function(id) actions[id].key = "F"; return true end,
        keyName = function(key) return tostring(key) end
    },
    gui = {
        createGuiFromLayout = function(path, destroyOnClose, settings)
            assert(path:find("KeybindBridge.layout", 1, true))
            assert(destroyOnClose == false)
            assert(settings.isInteractive and not settings.needsCursor)
            return gui
        end,
        displayAlertText = function()
            alertCount = alertCount + 1
        end
    },
    audio = {},
    effect = {
        playEffect = function(name, position)
            if audioFailures > 0 then
                audioFailures = audioFailures - 1
                error("Calling world dependent functions in a no world script!")
            end
            playedSounds[#playedSounds + 1] = {
                name = name,
                position = position
            }
        end,
        createEffect = function(name, host, bone)
            assert(name == "KeybindBridge - Character Flashlight"
                or name == "HeadLight")
            assert(host == character)
            assert(bone == "jnt_head")
            effect.destroyed = false
            effect.host = host
            effect.bone = bone
            return effect
        end
    },
    exists = function(value) return value ~= nil and not value.destroyed end,
    quat = {
        angleAxis = function(angle, axis)
            return setmetatable({ angle = angle, axis = axis }, Quat)
        end
    },
    vec3 = {
        new = Vec.new,
        getRotation = function(from, to)
            return setmetatable({ from = from, to = to }, Quat)
        end
    },
    camera = {
        state = {
            default = 1,
            cutsceneFP = 2,
            cutsceneTP = 3,
            forcedTP = 4,
            gyroSeatFP = 5,
            gyroSeatTP = 6,
            scriptedTP = 7,
            seatLockedCamera = 8
        },
        getCameraState = function() return 1 end,
        getCameraPullback = function() return cameraPullback, 0 end,
        getPosition = function() return cameraPosition end,
        getDirection = function()
            return cameraDirection or Vec.new(0, 1, 0)
        end,
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
assert(claimed)
dofile("$CONTENT_TEST/Scripts/KeybindBridgeExtension.lua")
KeybindBridgeRuntime:update()
assert(#order == 1)
assert(order[1] == "keybindbridge.flashlight.toggle")

KeybindBridgeRuntime:update()
assert(not effect.playing)
actionPress[order[1]] = 1
KeybindBridgeRuntime:update()
assert(#playedSounds == 0,
    "failed no-world audio call was incorrectly treated as played")
assert(alertCount == 0, "flashlight toggle still displayed top-screen text")
assert(not effect.playing, "world-context failure was not deferred")
KeybindBridgeRuntime:update()
assert(effect.playing)
assert(effect.host == character and effect.bone == "jnt_head")
assert(effect.offsetPosition ~= nil and effect.offsetRotation ~= nil)
assert(effect.positionCalls == 0 and effect.rotationCalls == 0,
    "hosted flashlight still writes world transforms")
assert(#playedSounds == 0,
    "no-world audio failure was incorrectly treated as played")
KeybindBridgeRuntime:update()
assert(#playedSounds == 1
    and playedSounds[1].name == "Button - On"
    and playedSounds[1].position == character.worldPosition,
    "flashlight ON sound was not retried with the vanilla button effect")
local offsetPositionCalls = effect.offsetPositionCalls
local offsetRotationCalls = effect.offsetRotationCalls
clockStepMs = 0
character.worldPosition = Vec.new(11, 20, 30)
KeybindBridgeRuntime:update()
assert(effect.offsetPositionCalls == offsetPositionCalls
    and effect.offsetRotationCalls == offsetRotationCalls,
    "hosted flashlight rewrote transforms before its update interval")
clockStepMs = 50
cameraDirection = Vec.new(0, 0.6, 0.8)
KeybindBridgeRuntime:update()
assert(effect.offsetPositionCalls == offsetPositionCalls + 1
    and effect.offsetRotationCalls == offsetRotationCalls + 1,
    "hosted flashlight did not refresh its camera-relative offsets")
assert(math.abs(effect.offsetRotation.target.x - cameraDirection.x) < 0.000001
    and math.abs(effect.offsetRotation.target.y - cameraDirection.y) < 0.000001
    and math.abs(effect.offsetRotation.target.z - cameraDirection.z) < 0.000001,
    "hosted flashlight did not use the exact camera direction")
assert(effect.positionCalls == 0 and effect.rotationCalls == 0,
    "camera aim update wrote world transforms")

firstPerson = true
cameraDirection = Vec.new(0, 1, 0)
KeybindBridgeRuntime:update()
assert(math.abs(effect.offsetPosition.y - 0.005) < 0.000001
    and math.abs(effect.offsetPosition.z - 0.07) < 0.000001,
    "first-person flashlight origin was not pulled back to the head")
cameraPosition = Vec.new(10, 20, 30.5)
character.crouching = true
KeybindBridgeRuntime:update()
assert(math.abs(effect.offsetPosition.z + 0.43) < 0.000001,
    "first-person flashlight did not follow the crouching camera")
firstPerson = false
KeybindBridgeRuntime:update()
assert(math.abs(effect.offsetPosition.y - 0.10) < 0.000001
    and math.abs(effect.offsetPosition.z + 0.38) < 0.000001,
    "third-person flashlight did not apply the crouch fallback")
character.crouching = false
KeybindBridgeRuntime:update()
assert(math.abs(effect.offsetPosition.y - 0.10) < 0.000001
    and math.abs(effect.offsetPosition.z - 0.07) < 0.000001,
    "third-person flashlight did not use its configured bone offset")

actionPress[order[1]] = 2
KeybindBridgeRuntime:update()
assert(#playedSounds == 2
    and playedSounds[2].name == "Button - Off"
    and playedSounds[2].position == character.worldPosition,
    "flashlight OFF did not play exactly one positional button effect")
assert(alertCount == 0, "OFF press still displayed top-screen text")
assert(not effect.playing and effect.destroyed,
    "single OFF press did not destroy the light")

menuSerial = 1
KeybindBridgeRuntime:update()
assert(claimed)
assert(guiOpened)
assert(nativeMenuOpen, "runtime did not publish the open menu state")
assert(guiText.Action1:find("Flashlight", 1, true))
actionPress[order[1]] = 3
KeybindBridgeRuntime:update()
assert(not effect.playing, "binding menu triggered an autonomous action")
rawPress[VK.ENTER] = 1
KeybindBridgeRuntime:update()
rawPress[VK.ESCAPE] = 1
capturedKey = VK.ESCAPE
KeybindBridgeRuntime:update()
KeybindBridgeRuntime:update()
assert(guiOpened and KeybindBridgeRuntime.isOpen,
    "Escape capture cancel also closed the binding menu")
rawPress[VK.ENTER] = 2
KeybindBridgeRuntime:update()
capturedKey = 71
KeybindBridgeRuntime:update()
assert(actions[order[1]].key == "71", "captured binding was not saved")
rawPress[VK.ESCAPE] = 2
KeybindBridgeRuntime:update()
assert(not guiOpened and not KeybindBridgeRuntime.isOpen,
    "Escape did not close both MyGUI and runtime state")
assert(not nativeMenuOpen, "runtime did not clear the open menu state")
assert(actions[order[1]].key == "71",
    "Escape discarded a binding that was already committed")
assert(not effect.playing, "menu key press replayed after Escape close")

actionPress[order[1]] = 4
KeybindBridgeRuntime:update()
assert(#playedSounds == 3, "post-menu toggle did not play one switch sound")
assert(effect.playing, "flashlight did not turn on after the menu closed")
character.seated = true
KeybindBridgeRuntime:update()
assert(not effect.playing, "flashlight stayed on while seated")
actionPress[order[1]] = 5
KeybindBridgeRuntime:update()
assert(#playedSounds == 3, "blocked seat press played the switch sound")
assert(not effect.playing, "seat key press turned the flashlight back on")
character.seated = false
character.lockingInteractable = { bed = true }
KeybindBridgeRuntime:update()
assert(not effect.playing, "flashlight was active while locked to a bed")
character.lockingInteractable = nil
KeybindBridgeRuntime:update()
assert(not effect.playing, "flashlight restarted automatically after leaving a seat")

print("Autonomous runtime tests passed")
