local native = {
    VK = { ESCAPE = 27 },
    serials = {},
    down = {},
    captured = nil,
    cancelled = 0
}

function native.pressSerial(key) return native.serials[key] or 0 end
function native.isDown(key) return native.down[key] or false end
function native.beginCapture() return 1 end
function native.captureNext(token)
    local value = native.captured
    native.captured = nil
    return value
end
function native.cancelCapture(token) native.cancelled = native.cancelled + 1 end
function native.keyName(key) return "VK_" .. tostring(key) end

function class(base) return {} end

local Vec = {}
Vec.__index = Vec
function Vec.new(x, y, z) return setmetatable({ x = x, y = y, z = z }, Vec) end
function Vec.__sub(a, b) return Vec.new(a.x - b.x, a.y - b.y, a.z - b.z) end
function Vec:length() return math.sqrt(self.x * self.x + self.y * self.y + self.z * self.z) end

sm = {
    keybind = native,
    interactable = { connectionType = { logic = 1, power = 2 } },
    color = { new = function(value) return value end },
    exists = function(value) return value ~= nil end,
    gui = {
        displayAlertText = function() end,
        getKeyBinding = function(name) return name end,
        setInteractionText = function() end
    },
    localPlayer = {}
}

local localPlayer = { character = { worldPosition = Vec.new(0, 0, 0) } }
local otherPlayer = { character = { worldPosition = Vec.new(100, 0, 0) } }
sm.localPlayer.getPlayer = function() return localPlayer end

local body = {}
local seat = { body = body }
function seat:hasSeat() return true end
function seat:getBody() return body end
function localPlayer.character:getLockingInteractable() return self.lockingInteractable end
function otherPlayer.character:getLockingInteractable() return self.lockingInteractable end
localPlayer.character.lockingInteractable = seat

local button = { active = false }
function button:hasSeat() return false end

local function makeInteractable()
    local value = { parents = { seat, button }, active = true, power = 0 }
    function value:getParents() return self.parents end
    function value:setActive(state) self.active = state end
    function value:setPower(power) self.power = power end
    function value:setPoseWeight(index, weight) self.poseWeight = weight end
    return value
end

local function makeStorage(saved)
    local value = { value = saved }
    function value:load() return self.value end
    function value:save(data) self.value = data end
    return value
end

local function makeNetwork()
    local value = { sent = {}, clientData = nil }
    function value:setClientData(data) self.clientData = data end
    function value:sendToServer(name, data)
        self.sent[#self.sent + 1] = { name = name, data = data }
    end
    return value
end

assert(loadfile("lua-mod/Scripts/ModeController.lua"))()
assert(ModeController.maxParentCount == -1)
assert(ModeController.maxChildCount == -1)

local server = setmetatable({
    interactable = makeInteractable(),
    storage = makeStorage(nil),
    network = makeNetwork(),
    shape = { worldPosition = Vec.new(0, 0, 0) }
}, { __index = ModeController })

ModeController.server_onCreate(server)
assert(server.sv.mode == "hold" and server.interactable.power == -1)
assert(server.interactable.active == false, "mode command leaked as active logic")

button.active = true
ModeController.server_onFixedUpdate(server)
assert(server.sv.mode == "hold", "held input cycled during initialization")
button.active = false
ModeController.server_onFixedUpdate(server)
button.active = true
ModeController.server_onFixedUpdate(server)
assert(server.sv.mode == "toggle" and server.interactable.power == 0)
ModeController.server_onFixedUpdate(server)
assert(server.sv.mode == "toggle", "held signal cycled more than once")
button.active = false
ModeController.server_onFixedUpdate(server)
button.active = true
ModeController.server_onFixedUpdate(server)
assert(server.sv.mode == "pulse" and server.interactable.power == 1)

ModeController.sv_cycleManually(server, nil, otherPlayer)
assert(server.sv.mode == "pulse", "far player changed the controller")
ModeController.sv_cycleFromKeyboard(server, nil, localPlayer)
assert(server.sv.mode == "hold", "connected seat key did not cycle")

ModeController.sv_setBinding(server, 66, localPlayer)
assert(server.sv.key == 66 and server.network.clientData.key == 66)

local client = setmetatable({
    interactable = makeInteractable(),
    network = makeNetwork(),
    shape = { worldPosition = Vec.new(0, 0, 0) }
}, { __index = ModeController })
ModeController.client_onCreate(client)
ModeController.client_onClientDataUpdate(client, { key = 66, mode = "hold" })
ModeController.client_onFixedUpdate(client)
native.serials[66] = 1
ModeController.client_onFixedUpdate(client)
assert(client.network.sent[#client.network.sent].name == "sv_cycleFromKeyboard")

localPlayer.character.lockingInteractable = nil
native.serials[66] = 2
local countOutside = #client.network.sent
ModeController.client_onFixedUpdate(client)
localPlayer.character.lockingInteractable = seat
ModeController.client_onFixedUpdate(client)
assert(#client.network.sent == countOutside,
    "press outside the seat was replayed after entering")

ModeController.client_onInteract(client, localPlayer.character, true)
native.captured = 67
ModeController.client_onFixedUpdate(client)
assert(client.network.sent[#client.network.sent].name == "sv_setBinding")
assert(client.network.sent[#client.network.sent].data == 67)

print("Mode Controller tests passed")
