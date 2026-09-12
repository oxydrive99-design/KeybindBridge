#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstddef>
#include <mutex>

// Minimal Lua 5.1/LuaJIT ABI used by Scrap Mechanic. We resolve the functions
// from the game's already loaded lua51.dll, so contributors do not need vcpkg,
// LuaJIT headers, or an import library just to build KeybindBridge.
struct lua_State;
using lua_Number = double;
using lua_Integer = std::ptrdiff_t;
using lua_CFunction = int(__cdecl*)(lua_State*);

struct luaL_Reg
{
    const char* name;
    lua_CFunction func;
};

constexpr int LUA_TTABLE = 5;
constexpr int LUA_TNUMBER = 3;
constexpr int LUA_TSTRING = 4;
constexpr int LUA_TFUNCTION = 6;
constexpr int LUA_GLOBALSINDEX = -10002;

class LuaApi
{
public:
    bool load()
    {
        std::call_once(m_loadOnce, [this]() { m_loaded = loadImplementation(); });
        return m_loaded;
    }

    lua_Integer (__cdecl* checkInteger)(lua_State*, int) = nullptr;
    const char* (__cdecl* checkLString)(lua_State*, int, std::size_t*) = nullptr;
    int (__cdecl* argumentError)(lua_State*, int, const char*) = nullptr;
    void (__cdecl* pushBoolean)(lua_State*, int) = nullptr;
    void (__cdecl* pushNumber)(lua_State*, lua_Number) = nullptr;
    void (__cdecl* pushNil)(lua_State*) = nullptr;
    void (__cdecl* pushLString)(lua_State*, const char*, std::size_t) = nullptr;
    void (__cdecl* pushString)(lua_State*, const char*) = nullptr;
    void (__cdecl* createTable)(lua_State*, int, int) = nullptr;
    void (__cdecl* setField)(lua_State*, int, const char*) = nullptr;
    void (__cdecl* registerLibrary)(lua_State*, const char*, const luaL_Reg*) = nullptr;
    int (__cdecl* raiseError)(lua_State*, const char*, ...) = nullptr;
    void (__cdecl* getField)(lua_State*, int, const char*) = nullptr;
    void (__cdecl* getEnvironment)(lua_State*, int) = nullptr;
    int (__cdecl* type)(lua_State*, int) = nullptr;
    int (__cdecl* toBoolean)(lua_State*, int) = nullptr;
    void (__cdecl* pushValue)(lua_State*, int) = nullptr;
    const char* (__cdecl* toLString)(lua_State*, int, std::size_t*) = nullptr;
    void (__cdecl* setTop)(lua_State*, int) = nullptr;
    int (__cdecl* getTop)(lua_State*) = nullptr;
    int (__cdecl* protectedCall)(lua_State*, int, int, int) = nullptr;
    void (__cdecl* close)(lua_State*) = nullptr;
    int (__cdecl* loadBuffer)(lua_State*, const char*, std::size_t, const char*) = nullptr;
    int (__cdecl* setEnvironment)(lua_State*, int) = nullptr;

private:
    bool loadImplementation()
    {

        HMODULE module = GetModuleHandleW(L"lua51.dll");
        if (module == nullptr)
        {
            module = GetModuleHandleW(L"luajit.dll");
        }
        if (module == nullptr)
        {
            return false;
        }

        return resolve(module, checkInteger, "luaL_checkinteger") &&
            resolve(module, checkLString, "luaL_checklstring") &&
            resolve(module, argumentError, "luaL_argerror") &&
            resolve(module, pushBoolean, "lua_pushboolean") &&
            resolve(module, pushNumber, "lua_pushnumber") &&
            resolve(module, pushNil, "lua_pushnil") &&
            resolve(module, pushLString, "lua_pushlstring") &&
            resolve(module, pushString, "lua_pushstring") &&
            resolve(module, createTable, "lua_createtable") &&
            resolve(module, setField, "lua_setfield") &&
            resolve(module, registerLibrary, "luaL_register") &&
            resolve(module, raiseError, "luaL_error") &&
            resolve(module, getField, "lua_getfield") &&
            resolve(module, getEnvironment, "lua_getfenv") &&
            resolve(module, type, "lua_type") &&
            resolve(module, toBoolean, "lua_toboolean") &&
            resolve(module, pushValue, "lua_pushvalue") &&
            resolve(module, toLString, "lua_tolstring") &&
            resolve(module, setTop, "lua_settop") &&
            resolve(module, getTop, "lua_gettop") &&
            resolve(module, protectedCall, "lua_pcall") &&
            resolve(module, close, "lua_close") &&
            resolve(module, loadBuffer, "luaL_loadbuffer") &&
            resolve(module, setEnvironment, "lua_setfenv");
    }

    template<typename Function>
    static bool resolve(HMODULE module, Function& function, const char* name)
    {
        function = reinterpret_cast<Function>(GetProcAddress(module, name));
        return function != nullptr;
    }

    std::once_flag m_loadOnce;
    bool m_loaded = false;
};
