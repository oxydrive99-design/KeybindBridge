-- Minimal blockless extension. Requires keybindbridge.json beside the mod's
-- description.json; see the SDK template for the complete folder structure.

local ACTION_ID = "yourname.yourmod.open_map"

local extension = {
    id = "yourname.yourmod.map_extension",
    actions = {
        {
            id = ACTION_ID,
            label = "Open map",
            defaultKey = "M"
        }
    }
}

function extension:onActionPressed(actionId)
    if actionId == ACTION_ID then
        -- Open your client-side map here.
        sm.gui.displayAlertText("Map action", 2)
    end
end

KeybindBridgeRuntime.registerExtension(extension)
