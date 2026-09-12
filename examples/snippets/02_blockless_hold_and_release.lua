local ACTION_ID = "yourname.yourmod.boost"

local extension = {
    id = "yourname.yourmod.boost_extension",
    updateIntervalMs = 16,
    actions = {
        {
            id = ACTION_ID,
            label = "Character boost",
            defaultKey = "LeftShift"
        }
    },
    wasDown = false
}

function extension:onUpdate()
    local menuOpen = sm.keybind.isGameMenuOpen ~= nil
        and sm.keybind.isGameMenuOpen()
    local down = not menuOpen and sm.keybind.actionIsDown(ACTION_ID)

    if down and not self.wasDown then
        -- Hold started.
    elseif not down and self.wasDown then
        -- Hold released.
    end
    self.wasDown = down
end

KeybindBridgeRuntime.registerExtension(extension)
