local CUSTOM_FLASHLIGHT_EFFECT = "KeybindBridge - Character Flashlight"
local FALLBACK_FLASHLIGHT_EFFECT = "HeadLight"
-- Vanilla effect-set names for these FMOD events:
-- {c49b6045-9381-4d39-8da2-a59d51300fce}
-- event:/vehicle/triggers/trigger_switch_on
-- {5662a157-cbc5-4a71-bceb-d0ab1a2086cb}
-- event:/vehicle/triggers/trigger_switch_off
local TOGGLE_ON_EFFECT = "Button - On"
local TOGGLE_OFF_EFFECT = "Button - Off"
local MAX_TOGGLE_SOUND_ATTEMPTS = 200
local HEAD_BONE = "jnt_head"
local FIRST_PERSON_FORWARD_OFFSET = 0.005
local FIRST_PERSON_UP_OFFSET = 0.07
local THIRD_PERSON_FORWARD_OFFSET = 0.10
local THIRD_PERSON_UP_OFFSET = 0.07
local CROUCH_FALLBACK_OFFSET = -0.45

local flashlight = {
    id = "keybindbridge.autonomous_flashlight",
    actions = {
        {
            id = "keybindbridge.flashlight.toggle",
            label = "Flashlight",
            defaultKey = "F"
        }
    },
    -- The host bone keeps the source on the character. Only its inexpensive
    -- local offsets are refreshed so the beam follows the exact camera aim.
    -- Five milliseconds is below one frame at common refresh rates while the
    -- native runtime still coalesces duplicate Lua callbacks.
    updateIntervalMs = 5,
    enabled = false,
    effect = nil,
    effectName = nil,
    hostCharacter = nil,
    started = false,
    worldErrorLogged = false,
    blocked = false,
    customEffectUnavailable = false,
    postureCharacter = nil,
    standingEyeHeight = nil,
    pendingSounds = {},
    pendingSoundAttempts = 0,
    pendingSoundErrorLogged = false
}

local function trace(message)
    if sm.keybind ~= nil and sm.keybind.trace ~= nil then
        sm.keybind.trace("flashlight: " .. message)
    end
end

local function destroyLight(self)
    if self.effect ~= nil and sm.exists(self.effect) then
        self.effect:destroy()
    end
    self.effect = nil
    self.effectName = nil
    self.hostCharacter = nil
    self.started = false
end

local function getAimDirection(character)
    if sm.camera ~= nil and sm.camera.getDirection ~= nil then
        local cameraDirection = sm.camera.getDirection()
        if cameraDirection ~= nil
            and cameraDirection:length2() >= 0.000001 then
            return cameraDirection
                * (1.0 / math.sqrt(cameraDirection:length2()))
        end
    end

    if character.getSmoothViewDirection ~= nil then
        local smoothDirection = character:getSmoothViewDirection()
        if smoothDirection ~= nil and smoothDirection:length2() >= 0.000001 then
            return smoothDirection
                * (1.0 / math.sqrt(smoothDirection:length2()))
        end
    end
    local direction = character.direction
    if direction ~= nil and direction:length2() >= 0.000001 then
        return direction * (1.0 / math.sqrt(direction:length2()))
    end
    return sm.vec3.new(0, 1, 0)
end

local function isFirstPersonView()
    if sm.localPlayer ~= nil
        and sm.localPlayer.isInFirstPersonView ~= nil then
        return sm.localPlayer.isInFirstPersonView()
    end

    if sm.camera == nil then
        return false
    end

    if sm.camera.getCameraState ~= nil and sm.camera.state ~= nil then
        local state = sm.camera.getCameraState()
        if state == sm.camera.state.cutsceneFP
            or state == sm.camera.state.gyroSeatFP then
            return true
        end
        if state == sm.camera.state.cutsceneTP
            or state == sm.camera.state.forcedTP
            or state == sm.camera.state.gyroSeatTP
            or state == sm.camera.state.scriptedTP then
            return false
        end
    end

    -- The default player camera uses pullback step 0 for first person.
    if sm.camera.getCameraPullback ~= nil then
        local pullback = sm.camera.getCameraPullback()
        if type(pullback) == "number" then
            return pullback <= 0
        end
    end
    return false
end

local function getCharacterPosition(character)
    if character.getWorldPosition ~= nil then
        return character:getWorldPosition()
    end
    return character.worldPosition
end

local function queueToggleSound(self, enabled)
    -- The native key callback is occasionally a client callback without an
    -- active world. Queue every real state change and let onUpdate retry it
    -- from a callback where world-dependent audio calls are legal.
    self.pendingSounds[#self.pendingSounds + 1] = enabled
        and TOGGLE_ON_EFFECT
        or TOGGLE_OFF_EFFECT
end

local function tryPendingToggleSound(self)
    local sound = self.pendingSounds[1]
    if sound == nil then
        return
    end

    self.pendingSoundAttempts = self.pendingSoundAttempts + 1
    local ok = false
    local errorText = nil
    local player = sm.localPlayer.getPlayer()
    local character = player and player.character or nil
    local position = character ~= nil and getCharacterPosition(character) or nil
    if sm.effect ~= nil and sm.effect.playEffect ~= nil and position ~= nil then
        ok, errorText = pcall(sm.effect.playEffect, sound, position)
    else
        errorText = "sm.effect.playEffect or character position is missing"
    end

    if ok then
        trace("sound: " .. sound)
        table.remove(self.pendingSounds, 1)
        self.pendingSoundAttempts = 0
        self.pendingSoundErrorLogged = false
        return
    end

    if not self.pendingSoundErrorLogged then
        self.pendingSoundErrorLogged = true
        trace("sound waiting for world context: " .. tostring(errorText))
    end
    if self.pendingSoundAttempts >= MAX_TOGGLE_SOUND_ATTEMPTS then
        trace("sound dropped after retries: " .. sound)
        table.remove(self.pendingSounds, 1)
        self.pendingSoundAttempts = 0
        self.pendingSoundErrorLogged = false
    end
end

local function updateStandingEyeHeight(self, character, firstPerson)
    if self.postureCharacter ~= character then
        self.postureCharacter = character
        self.standingEyeHeight = nil
    end
    if not firstPerson or character:isCrouching()
        or sm.camera == nil or sm.camera.getPosition == nil then
        return
    end

    local cameraPosition = sm.camera.getPosition()
    local characterPosition = getCharacterPosition(character)
    if cameraPosition ~= nil and characterPosition ~= nil then
        self.standingEyeHeight = cameraPosition.z - characterPosition.z
    end
end

local function getCrouchOffset(self, character, firstPerson)
    if not character:isCrouching() then
        return 0.0
    end
    if firstPerson and self.standingEyeHeight ~= nil
        and sm.camera ~= nil and sm.camera.getPosition ~= nil then
        local cameraPosition = sm.camera.getPosition()
        local characterPosition = getCharacterPosition(character)
        if cameraPosition ~= nil and characterPosition ~= nil then
            local currentEyeHeight = cameraPosition.z - characterPosition.z
            return math.min(0.0, currentEyeHeight - self.standingEyeHeight)
        end
    end
    return CROUCH_FALLBACK_OFFSET
end

local function updateHostedAim(self, character)
    if self.effect == nil or not sm.exists(self.effect) then
        return
    end

    local direction = getAimDirection(character)
    local boneRotation = character:getTpBoneRot(HEAD_BONE)
    local inverseBoneRotation = boneRotation:inverse()
    local worldRotation = sm.vec3.getRotation(
        sm.vec3.new(0, 0, 1),
        direction)
    local firstPerson = isFirstPersonView()
    local forwardOffset = firstPerson
        and FIRST_PERSON_FORWARD_OFFSET
        or THIRD_PERSON_FORWARD_OFFSET
    local upOffset = firstPerson
        and FIRST_PERSON_UP_OFFSET
        or THIRD_PERSON_UP_OFFSET
    updateStandingEyeHeight(self, character, firstPerson)
    local crouchOffset = getCrouchOffset(self, character, firstPerson)
    local worldPositionOffset = direction * forwardOffset
        + sm.vec3.new(0, 0, upOffset + crouchOffset)

    -- Never move the spotLight to the camera itself: Scrap Mechanic stops
    -- producing reliable dynamic shadows when the shadow-casting light origin
    -- is almost coincident with the first-person camera. Only the vertical
    -- posture delta is applied; the source remains hosted on jnt_head.

    -- Both values are local to jnt_head. Re-applying the inverse of the
    -- current bone rotation cancels head-animation lag and produces the exact
    -- camera direction in world space. The source itself remains character-
    -- hosted and never follows the third-person camera position.
    self.effect:setOffsetPosition(
        inverseBoneRotation * worldPositionOffset)
    self.effect:setOffsetRotation(inverseBoneRotation * worldRotation)
end

local function createHostedLight(self, character, effectName)
    local effect = sm.effect.createEffect(
        effectName,
        character,
        HEAD_BONE)
    -- Store it immediately so the outer error handler can destroy it if any
    -- of the host/bone setup calls fail in a not-yet-ready world.
    self.effect = effect
    self.effectName = effectName
    self.hostCharacter = character
    self.started = false

    -- The light is attached to the animated head bone. The game moves its
    -- origin together with the character; Lua only aligns the local offsets
    -- with the current camera ray.
    updateHostedAim(self, character)

    trace("hosted effect created: " .. effectName .. " on " .. HEAD_BONE)
end

local function startHostedLight(self, character)
    local effectName = self.customEffectUnavailable
        and FALLBACK_FLASHLIGHT_EFFECT
        or CUSTOM_FLASHLIGHT_EFFECT

    createHostedLight(self, character, effectName)
    self.effect:start()

    -- Scrap Mechanic 1.0 can compile a mod effect-set but keep it outside the
    -- sandbox that owns this runtime. A missing effect does not reliably throw
    -- a Lua error; isPlaying() is the only usable runtime probe.
    if effectName == CUSTOM_FLASHLIGHT_EFFECT
        and not self.effect:isPlaying() then
        destroyLight(self)
        self.customEffectUnavailable = true
        trace("custom spotLight unavailable; falling back to HeadLight")
        createHostedLight(self, character, FALLBACK_FLASHLIGHT_EFFECT)
        self.effect:start()
    end

    if not self.effect:isPlaying() then
        local failedName = self.effectName or "unknown"
        destroyLight(self)
        error("flashlight effect did not start: " .. failedName)
    end

    self.started = true
    trace("ON: " .. self.effectName)
end

local function updateLightInWorld(self)
    local player = sm.localPlayer.getPlayer()
    local character = player and player.character or nil
    if character == nil or not sm.exists(character) then
        destroyLight(self)
        return
    end

    local locked = character:isSeated()
        or character:getLockingInteractable() ~= nil
    self.blocked = locked
    updateStandingEyeHeight(self, character, isFirstPersonView())
    if locked and self.enabled then
        self.enabled = false
        trace("automatic OFF: character is seated or sleeping")
    end

    if not self.enabled then
        if self.effect ~= nil then
            destroyLight(self)
            trace("OFF")
        end
        return
    end

    -- A respawn or world re-entry replaces the Character userdata. Never keep
    -- an effect hosted to the old character.
    if self.hostCharacter ~= character
        or self.effect == nil
        or not sm.exists(self.effect) then
        destroyLight(self)
        startHostedLight(self, character)
    end

    updateHostedAim(self, character)
end

function flashlight:onCreate()
    self.enabled = false
    self.effect = nil
    self.effectName = nil
    self.hostCharacter = nil
    self.started = false
    self.worldErrorLogged = false
    self.blocked = false
    self.customEffectUnavailable = false
    self.postureCharacter = nil
    self.standingEyeHeight = nil
    self.pendingSounds = {}
    self.pendingSoundAttempts = 0
    self.pendingSoundErrorLogged = false
    trace("ready: character-hosted camera-aim spotLight")
end

function flashlight:onActionPressed(actionId)
    if actionId ~= "keybindbridge.flashlight.toggle" then
        return
    end

    if self.blocked then
        trace("press ignored while character is seated or sleeping")
        return
    end

    self.enabled = not self.enabled
    trace(self.enabled and "requested ON" or "requested OFF")
    queueToggleSound(self, self.enabled)
end

function flashlight:onUpdate()
    -- The native runtime can observe client callbacks that have no active
    -- world. Keep the requested state and retry after the world becomes valid.
    local ok, errorText = pcall(updateLightInWorld, self)
    if not ok then
        -- A partially created effect must not survive a failed host setup.
        destroyLight(self)
        if not self.worldErrorLogged then
            self.worldErrorLogged = true
            trace("waiting for world context: " .. tostring(errorText))
        end
    else
        self.worldErrorLogged = false
        tryPendingToggleSound(self)
    end
end

KeybindBridgeRuntime.registerExtension(flashlight)
