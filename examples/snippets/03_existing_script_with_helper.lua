-- Copy Scripts/KeybindAction.lua into your mod, then use it from an existing
-- client script instance. This variant is useful when you need self.network.

dofile("$CONTENT_DATA/Scripts/KeybindAction.lua")

NetworkedActionExample = class(nil)

function NetworkedActionExample.client_onCreate(self)
    self.openStorageAction = KeybindAction.create(
        "yourname.yourmod.open_storage",
        "Open extra storage",
        "B")
end

function NetworkedActionExample.client_onFixedUpdate(self)
    if self.openStorageAction:wasPressed() then
        -- A GUI can be opened locally. Authoritative changes must go through
        -- a server RPC and be checked again on the server.
        self.network:sendToServer("sv_requestStorageAction")
    end
end

function NetworkedActionExample.sv_requestStorageAction(self, _, player)
    local character = player and player.character or nil
    if character == nil or not sm.exists(character) then
        return
    end

    -- Replace this example distance check with ownership, equipment,
    -- inventory and other validation required by your feature.
    if (character.worldPosition - self.shape.worldPosition):length() > 5.0 then
        return
    end

    -- Perform the authoritative server-side operation here.
end
