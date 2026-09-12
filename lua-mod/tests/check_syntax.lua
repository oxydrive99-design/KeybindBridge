local path = arg and arg[1] or nil
assert(path ~= nil, "Lua source path is required")
assert(loadfile(path))
