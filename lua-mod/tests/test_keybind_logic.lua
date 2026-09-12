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
    interactable = { connectionType = { none = 0, logic = 1, power = 2, bearing = 4, seated = 8 } },
    color = { new = function(value) return value end },
    exists = function(value) return value ~= nil end,
    gui = {
        displayAlertText = function() end,
        getKeyBinding = function(name) return name end,
        setInteractionText = function() end
    },
    localPlayer = {},
    uuid = { new = function(value) return value end }
}
sm.keybind = native

local localPlayer = { id = 1 }
local otherPlayer = { id = 2 }
local localCharacter = { worldPosition = Vec.new(0, 0, 0) }
local otherCharacter = { worldPosition = Vec.new(0, 0, 0) }
localPlayer.character = localCharacter
otherPlayer.character = otherCharacter
function localCharacter:getPlayer() return localPlayer end
function otherCharacter:getPlayer() return otherPlayer end
function localCharacter:getLockingInteractable() return self.lockingInteractable end
function otherCharacter:getLockingInteractable() return self.lockingInteractable end
sm.localPlayer.getPlayer = function() return localPlayer end

local creationBody = {}
function creationBody:getCreationBodies() return { self } end

local otherBody = {}
function otherBody:getCreationBodies() return { self } end

local seat = { body = creationBody }
function seat:hasSeat() return true end
function seat:getBody() return self.body end
localCharacter.lockingInteractable = seat

local function makeInteractable()
    local value = { body = creationBody, active = false, power = 0, setCount = 0, parents = { seat } }
    function value:getBody() return self.body end
    function value:getParents() return self.parents end
    function value:setActive(state) self.active = state; self.setCount = self.setCount + 1 end
    function value:setPower(power) self.power = power end
    function value:setPoseWeight(index, weight) self.poseWeight = weight end
    return value
end

local function makeNetwork()
    local value = { sent = {}, clientData = nil }
    function value:setClientData(data) self.clientData = data end
    function value:sendToServer(name, data)
        table.insert(self.sent, { name = name, data = data })
    end
    return value
end

local function makeStorage(saved)
    local value = { value = saved }
    function value:load() return self.value end
    function value:save(data) self.value = data end
    return value
end

assert(loadfile("lua-mod/Scripts/KeybindLogic.lua"))()
assert(KeybindLogic.maxParentCount == 2)
assert(KeybindLogic.connectionInput == sm.interactable.connectionType.power + sm.interactable.connectionType.logic)
assert(KeybindLogic.connectionOutput == sm.interactable.connectionType.logic + sm.interactable.connectionType.power + sm.interactable.connectionType.bearing)

local server = setmetatable({
    interactable = makeInteractable(),
    network = makeNetwork(),
    storage = makeStorage(nil),
    shape = { worldPosition = Vec.new(0, 0, 0) }
}, { __index = KeybindLogic })

KeybindLogic.server_onCreate(server)
assert(server.sv.key == 70 and server.sv.mode == "hold")
assert(server.network.clientData.key == 70)

KeybindLogic.sv_setBinding(server, { key = 66, mode = "toggle" }, localPlayer)
assert(server.sv.key == 66 and server.sv.mode == "toggle")

otherPlayer.character.worldPosition = Vec.new(100, 0, 0)
KeybindLogic.sv_setBinding(server, { key = 67, mode = "pulse" }, otherPlayer)
assert(server.sv.key == 66, "far player changed binding")
otherPlayer.character.worldPosition = Vec.new(0, 0, 0)

local otherSeat = { body = creationBody }
function otherSeat:hasSeat() return true end
function otherSeat:getBody() return self.body end
otherCharacter.lockingInteractable = otherSeat
KeybindLogic.sv_requestState(server, true, otherPlayer)
assert(server.interactable.active == false, "passenger in another seat activated output")
KeybindLogic.sv_requestState(server, true, localPlayer)
assert(server.interactable.active == true, "driver did not activate output")
assert(server.interactable.power == 1, "power output did not activate")

localCharacter.lockingInteractable = nil
KeybindLogic.server_onFixedUpdate(server)
assert(server.interactable.active == false, "output stayed active after leaving seat")
assert(server.interactable.power == 0, "power output stayed active after leaving seat")
localCharacter.lockingInteractable = seat

server.interactable.parents = {}
KeybindLogic.sv_requestState(server, true, localPlayer)
assert(server.interactable.active == false, "output activated without a connected seat")
server.interactable.parents = { seat }

local controller = {
    power = 1,
    shape = { uuid = sm.uuid.new("9a2a8f43-6f74-4fc3-b25c-3e139b710003") }
}
function controller:hasSeat() return false end
server.interactable.parents = { seat, controller }
KeybindLogic.server_onFixedUpdate(server)
assert(server.sv.mode == "pulse", "mode controller did not update Keybind Logic")
assert(server.network.clientData.mode == "pulse", "controlled mode was not synchronized")
controller.power = 0
KeybindLogic.server_onFixedUpdate(server)
assert(server.sv.mode == "toggle", "zero power did not select toggle mode")
controller.power = -1
KeybindLogic.server_onFixedUpdate(server)
assert(server.sv.mode == "hold", "negative power did not select hold mode")

local client = setmetatable({
    interactable = makeInteractable(),
    network = makeNetwork(),
    shape = { worldPosition = Vec.new(0, 0, 0) }
}, { __index = KeybindLogic })

KeybindLogic.client_onCreate(client)
KeybindLogic.client_onClientDataUpdate(client, { key = 70, mode = "hold" })
native.down[70] = false
KeybindLogic.client_onFixedUpdate(client)
native.down[70] = true
KeybindLogic.client_onFixedUpdate(client)
assert(#client.network.sent == 1 and client.network.sent[1].data == true)
KeybindLogic.client_onFixedUpdate(client)
assert(#client.network.sent == 1, "hold mode spammed an unchanged state")
native.down[70] = false
KeybindLogic.client_onFixedUpdate(client)
assert(#client.network.sent == 2 and client.network.sent[2].data == false)

KeybindLogic.client_onClientDataUpdate(client, { key = 70, mode = "toggle" })
KeybindLogic.client_onFixedUpdate(client)
native.serials[70] = 1
KeybindLogic.client_onFixedUpdate(client)
assert(client.network.sent[#client.network.sent].data == true)
localCharacter.lockingInteractable = nil
KeybindLogic.client_onFixedUpdate(client)
assert(client.network.sent[#client.network.sent].data == false)
local sentBeforeOutsidePress = #client.network.sent
native.serials[70] = 2
KeybindLogic.client_onFixedUpdate(client)
localCharacter.lockingInteractable = seat
KeybindLogic.client_onFixedUpdate(client)
assert(#client.network.sent == sentBeforeOutsidePress,
    "press made outside the seat was replayed after entering")
native.serials[70] = 3
KeybindLogic.client_onFixedUpdate(client)
assert(client.network.sent[#client.network.sent].data == true,
    "fresh press in the seat did not toggle the block")

KeybindLogic.client_onInteract(client, localCharacter, true)
native.captured = 66
KeybindLogic.client_onFixedUpdate(client)
local bindingMessage = client.network.sent[#client.network.sent]
assert(bindingMessage.name == "sv_setBinding" and bindingMessage.data.key == 66)

KeybindLogic.client_onInteract(client, localCharacter, true)
for _ = 1, 400 do
    KeybindLogic.client_onFixedUpdate(client)
end
assert(client.cl.capturing == false and native.cancelled == 1, "capture did not time out")

client.cl.mode = "hold"
KeybindLogic.client_onTinker(client, localCharacter, true)
local modeMessage = client.network.sent[#client.network.sent]
assert(modeMessage.name == "sv_setMode" and modeMessage.data == "toggle")

print("Lua logic tests passed")
