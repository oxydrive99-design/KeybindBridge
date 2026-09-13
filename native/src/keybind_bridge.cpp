#ifndef _WIN32
#error KeybindBridge is a Windows-only Lua module.
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>

#include <MinHook.h>

#include "lua_abi.hpp"

#include <array>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
constexpr const char* kVersion = "1.0.0";
constexpr std::size_t kKeyCount = 256;
constexpr const char* kCoreContentId = "9a2a8f43-6f74-4fc3-b25c-3e139b710001";
constexpr const char* kRuntimeScript =
    "$CONTENT_9a2a8f43-6f74-4fc3-b25c-3e139b710001/Scripts/KeybindRuntime.lua";
LuaApi g_lua;
HMODULE g_module = nullptr;
std::atomic<bool> g_loggedHookCall{false};
std::atomic<bool> g_loggedClientRuntimeTick{false};
std::atomic<bool> g_gameMenuClaimed{false};
std::atomic<bool> g_gameMenuOpen{false};
std::atomic<std::uint32_t> g_menuToggleSerial{0};
std::atomic<int> g_featureLevel{8};
std::atomic<std::uint64_t> g_runtimeClaimedAtMs{0};
std::once_flag g_logFileInitialized;
std::mutex g_logWriteMutex;
using LuaPcall = int (__cdecl*)(lua_State*, int, int, int);
using LuaClose = void (__cdecl*)(lua_State*);
LuaPcall g_originalLuaPcall = nullptr;
LuaClose g_originalLuaClose = nullptr;
thread_local bool g_insideBridgeRuntime = false;
thread_local unsigned int g_luaPcallHookDepth = 0;

std::mutex g_runtimeOwnerMutex;
lua_State* g_runtimeOwnerState = nullptr;
std::uint64_t g_runtimeOwnerLastTickMs = 0;
constexpr std::uint64_t kRuntimeOwnerStaleMs = 2000;

std::mutex g_extensionMutex;
struct ExtensionSource
{
    std::string resourcePath;
    std::string source;
};
std::vector<ExtensionSource> g_extensions;
std::string g_runtimeSource;

std::uint64_t monotonicMilliseconds()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool claimRuntimeOwner(lua_State* state, bool& replacedStaleOwner)
{
    const std::uint64_t now = monotonicMilliseconds();
    std::lock_guard lock(g_runtimeOwnerMutex);
    const bool stale = g_runtimeOwnerState != nullptr &&
        g_runtimeOwnerState != state &&
        now - g_runtimeOwnerLastTickMs >= kRuntimeOwnerStaleMs;
    if (g_runtimeOwnerState == nullptr || g_runtimeOwnerState == state || stale)
    {
        replacedStaleOwner = stale;
        g_runtimeOwnerState = state;
        g_runtimeOwnerLastTickMs = now;
        g_runtimeClaimedAtMs.store(now, std::memory_order_release);
        return true;
    }
    return false;
}

bool isRuntimeOwner(lua_State* state)
{
    std::lock_guard lock(g_runtimeOwnerMutex);
    return g_runtimeOwnerState == state;
}

void touchRuntimeOwner(lua_State* state)
{
    std::lock_guard lock(g_runtimeOwnerMutex);
    if (g_runtimeOwnerState == state)
    {
        g_runtimeOwnerLastTickMs = monotonicMilliseconds();
    }
}

bool releaseRuntimeOwner(lua_State* state)
{
    std::lock_guard lock(g_runtimeOwnerMutex);
    if (g_runtimeOwnerState != state)
    {
        return false;
    }
    g_runtimeOwnerState = nullptr;
    g_runtimeOwnerLastTickMs = 0;
    g_runtimeClaimedAtMs.store(0, std::memory_order_release);
    g_gameMenuClaimed.store(false, std::memory_order_release);
    g_gameMenuOpen.store(false, std::memory_order_release);
    return true;
}

void requestBindingMenuToggle();
void requestBindingMenuRefresh();
void startBindingMenu();
void updateRegisteredActions();
void logLine(const char* message);
void scanAutonomousExtensions();

int loadFeatureLevel()
{
#ifdef _MSC_VER
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(g_module, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        return 8;
    }

    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash == nullptr)
    {
        return 8;
    }
    *(slash + 1) = L'\0';
    wcscat_s(path, L"KeybindBridge.diagnostics.ini");
    const UINT configured = GetPrivateProfileIntW(L"Diagnostics", L"FeatureLevel", 8, path);
    return static_cast<int>(std::min<UINT>(configured, 8));
#else
    return 8;
#endif
}

template <typename Function>
LPVOID functionAddress(Function function)
{
    static_assert(sizeof(function) == sizeof(LPVOID));
    LPVOID address = nullptr;
    std::memcpy(&address, &function, sizeof(address));
    return address;
}

template <typename Function>
Function functionFromAddress(LPVOID address)
{
    static_assert(sizeof(Function) == sizeof(address));
    Function function = nullptr;
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

enum class CaptureState : std::uint8_t
{
    idle,
    armed,
    captured
};

bool isForegroundProcess()
{
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr)
    {
        return false;
    }

    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(foreground, &foregroundPid);
    return foregroundPid == GetCurrentProcessId();
}

bool isCaptureCandidate(const int vk)
{
    // Generic modifier codes are reported together with their left/right forms.
    // Skipping them lets capture return VK_LSHIFT instead of VK_SHIFT, etc.
    if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU)
    {
        return false;
    }

    // End is reserved for the shared binding menu.
    return vk > 0 && vk < 255 && vk != VK_END;
}

class InputSampler
{
public:
    InputSampler()
    {
        for (std::size_t index = 0; index < kKeyCount; ++index)
        {
            m_down[index].store(false, std::memory_order_relaxed);
            m_pressSerial[index].store(0, std::memory_order_relaxed);
            m_releaseSerial[index].store(0, std::memory_order_relaxed);
            m_captureIgnored[index].store(false, std::memory_order_relaxed);
        }
    }

    void start()
    {
        bool expected = false;
        if (!m_running.compare_exchange_strong(expected, true))
        {
            return;
        }

        // The module stays loaded for the lifetime of Scrap Mechanic. The worker
        // owns no Lua state and only writes atomics, so it never touches Lua from
        // a foreign thread.
        std::thread([this]() { run(); }).detach();
    }

    bool isDown(const std::uint8_t vk) const
    {
        return m_down[vk].load(std::memory_order_acquire);
    }

    std::uint32_t pressSerial(const std::uint8_t vk) const
    {
        return m_pressSerial[vk].load(std::memory_order_acquire);
    }

    std::uint32_t releaseSerial(const std::uint8_t vk) const
    {
        return m_releaseSerial[vk].load(std::memory_order_acquire);
    }

    std::uint32_t beginCapture()
    {
        // Ignore every key already held when capture starts (normally the E
        // used to interact with the block). Unlike waiting for *all* controls
        // to be released, this cannot get stuck on a mouse/controller state.
        m_captureState.store(CaptureState::idle, std::memory_order_release);
        for (int vk = 1; vk < 255; ++vk)
        {
            const bool held = isCaptureCandidate(vk) &&
                (GetAsyncKeyState(vk) & 0x8000) != 0;
            m_captureIgnored[vk].store(held, std::memory_order_release);
        }

        const std::uint32_t generation =
            m_captureGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
        m_capturedKey.store(-1, std::memory_order_release);
        m_captureState.store(CaptureState::armed, std::memory_order_release);
        return generation;
    }

    bool takeCapturedKey(const std::uint32_t generation, int& key)
    {
        if (generation != m_captureGeneration.load(std::memory_order_acquire))
        {
            return false;
        }

        if (m_captureState.load(std::memory_order_acquire) != CaptureState::captured)
        {
            return false;
        }

        key = m_capturedKey.exchange(-1, std::memory_order_acq_rel);
        m_captureState.store(CaptureState::idle, std::memory_order_release);
        return key >= 0;
    }

    void cancelCapture(const std::uint32_t generation)
    {
        if (generation == m_captureGeneration.load(std::memory_order_acquire))
        {
            m_capturedKey.store(-1, std::memory_order_release);
            m_captureState.store(CaptureState::idle, std::memory_order_release);
        }
    }

private:
    void run()
    {
        std::array<bool, kKeyCount> previousPhysical{};

        while (m_running.load(std::memory_order_acquire))
        {
            const bool foreground = isForegroundProcess();
            int newlyPressedCaptureKey = -1;

            for (int vk = 1; vk < 255; ++vk)
            {
                const bool physicalDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
                const bool transitionedDown = physicalDown && !previousPhysical[vk];
                const bool transitionedUp = !physicalDown && previousPhysical[vk];
                const bool effectiveDown = foreground && physicalDown;

                m_down[vk].store(effectiveDown, std::memory_order_release);

                // Serials are non-consuming edge markers. Every Lua object can
                // compare its own last seen value, so two blocks may share a key.
                if (foreground && transitionedDown)
                {
                    m_pressSerial[vk].fetch_add(1, std::memory_order_acq_rel);
                    if (vk == VK_END)
                    {
                        g_menuToggleSerial.fetch_add(1, std::memory_order_acq_rel);
                        requestBindingMenuToggle();
                    }
                }
                if (foreground && transitionedUp)
                {
                    m_releaseSerial[vk].fetch_add(1, std::memory_order_acq_rel);
                }

                if (isCaptureCandidate(vk))
                {
                    const bool ignored = m_captureIgnored[vk].load(std::memory_order_acquire);
                    if (ignored && !physicalDown)
                    {
                        m_captureIgnored[vk].store(false, std::memory_order_release);
                    }
                    else if (!ignored && foreground && transitionedDown &&
                             newlyPressedCaptureKey < 0)
                    {
                        newlyPressedCaptureKey = vk;
                    }
                }

                previousPhysical[vk] = physicalDown;
            }

            updateRegisteredActions();

            const CaptureState captureState = m_captureState.load(std::memory_order_acquire);
            if (captureState == CaptureState::armed && newlyPressedCaptureKey >= 0)
            {
                m_capturedKey.store(newlyPressedCaptureKey, std::memory_order_release);
                m_captureState.store(CaptureState::captured, std::memory_order_release);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
    }

    std::atomic<bool> m_running{false};
    std::array<std::atomic<bool>, kKeyCount> m_down{};
    std::array<std::atomic<std::uint32_t>, kKeyCount> m_pressSerial{};
    std::array<std::atomic<std::uint32_t>, kKeyCount> m_releaseSerial{};
    std::array<std::atomic<bool>, kKeyCount> m_captureIgnored{};
    std::atomic<CaptureState> m_captureState{CaptureState::idle};
    std::atomic<std::uint32_t> m_captureGeneration{0};
    std::atomic<int> m_capturedKey{-1};
};

InputSampler& sampler()
{
    // Intentionally process-lifetime storage: unloading a native Lua module while
    // its interpreter is alive is unsupported by LuaJIT as used by the game.
    static InputSampler* instance = new InputSampler();
    return *instance;
}

int checkVirtualKey(lua_State* state, const int argument)
{
    const lua_Integer value = g_lua.checkInteger(state, argument);
    if (value < 0 || value > 255)
    {
        g_lua.argumentError(state, argument, "virtual-key code must be in range 0..255");
    }
    return static_cast<int>(value);
}

std::string wideToUtf8(const wchar_t* text)
{
    const int bytes = WideCharToMultiByte(
        CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1)
    {
        return {};
    }

    std::string result(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text, -1, result.data(), bytes, nullptr, nullptr);
    result.pop_back();
    return result;
}

std::string keyName(const int vk)
{
    switch (vk)
    {
    case VK_LBUTTON: return "Mouse 1";
    case VK_RBUTTON: return "Mouse 2";
    case VK_MBUTTON: return "Mouse 3";
    case VK_XBUTTON1: return "Mouse 4";
    case VK_XBUTTON2: return "Mouse 5";
    case VK_BACK: return "Backspace";
    case VK_TAB: return "Tab";
    case VK_RETURN: return "Enter";
    case VK_ESCAPE: return "Escape";
    case VK_SPACE: return "Space";
    case VK_PRIOR: return "Page Up";
    case VK_NEXT: return "Page Down";
    case VK_END: return "End";
    case VK_HOME: return "Home";
    case VK_LEFT: return "Left";
    case VK_UP: return "Up";
    case VK_RIGHT: return "Right";
    case VK_DOWN: return "Down";
    case VK_INSERT: return "Insert";
    case VK_DELETE: return "Delete";
    case VK_LWIN: return "Left Windows";
    case VK_RWIN: return "Right Windows";
    case VK_LSHIFT: return "Left Shift";
    case VK_RSHIFT: return "Right Shift";
    case VK_LCONTROL: return "Left Ctrl";
    case VK_RCONTROL: return "Right Ctrl";
    case VK_LMENU: return "Left Alt";
    case VK_RMENU: return "Right Alt";
    default: break;
    }

    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z'))
    {
        return std::string(1, static_cast<char>(vk));
    }

    const UINT scanCode = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC_EX);
    LONG keyInfo = static_cast<LONG>((scanCode & 0xffu) << 16u);
    if ((scanCode & 0xff00u) != 0)
    {
        keyInfo |= 1L << 24L;
    }

    wchar_t buffer[128]{};
    if (GetKeyNameTextW(keyInfo, buffer, static_cast<int>(std::size(buffer))) > 0)
    {
        const std::string translated = wideToUtf8(buffer);
        if (!translated.empty())
        {
            return translated;
        }
    }

    return "VK_" + std::to_string(vk);
}

struct BindingActionSnapshot
{
    std::string id;
    std::string label;
    int key = 0;
};

struct BindingAction
{
    std::string id;
    std::string label;
    int defaultKey = 0;
    int key = 0;
    std::uint32_t pressSerial = 0;
    std::uint32_t releaseSerial = 0;
    std::uint32_t lastRawPress = 0;
    std::uint32_t lastRawRelease = 0;
};

class BindingRegistry
{
public:
    int registerAction(const std::string& id, const std::string& label, const int defaultKey)
    {
        std::lock_guard lock(m_mutex);
        auto iterator = m_actions.find(id);
        if (iterator == m_actions.end())
        {
            BindingAction action;
            action.id = id;
            action.label = label;
            action.defaultKey = defaultKey;
            action.key = loadBinding(id, defaultKey);
            action.lastRawPress = sampler().pressSerial(static_cast<std::uint8_t>(action.key));
            action.lastRawRelease = sampler().releaseSerial(static_cast<std::uint8_t>(action.key));
            m_order.push_back(id);
            iterator = m_actions.emplace(id, std::move(action)).first;
        }
        else
        {
            iterator->second.label = label;
            iterator->second.defaultKey = defaultKey;
        }

        return iterator->second.key;
    }

    bool setKey(const std::string& id, const int key)
    {
        std::lock_guard lock(m_mutex);
        const auto iterator = m_actions.find(id);
        if (iterator == m_actions.end())
        {
            return false;
        }

        BindingAction& action = iterator->second;
        action.key = key;
        action.lastRawPress = sampler().pressSerial(static_cast<std::uint8_t>(key));
        action.lastRawRelease = sampler().releaseSerial(static_cast<std::uint8_t>(key));
        saveBinding(id, key);
        return true;
    }

    bool resetKey(const std::string& id)
    {
        std::lock_guard lock(m_mutex);
        const auto iterator = m_actions.find(id);
        if (iterator == m_actions.end())
        {
            return false;
        }

        BindingAction& action = iterator->second;
        action.key = action.defaultKey;
        action.lastRawPress = sampler().pressSerial(static_cast<std::uint8_t>(action.key));
        action.lastRawRelease = sampler().releaseSerial(static_cast<std::uint8_t>(action.key));
        saveBinding(id, action.key);
        return true;
    }

    int key(const std::string& id) const
    {
        std::lock_guard lock(m_mutex);
        const auto iterator = m_actions.find(id);
        return iterator == m_actions.end() ? -1 : iterator->second.key;
    }

    std::uint32_t pressSerial(const std::string& id) const
    {
        std::lock_guard lock(m_mutex);
        const auto iterator = m_actions.find(id);
        return iterator == m_actions.end() ? 0 : iterator->second.pressSerial;
    }

    std::uint32_t releaseSerial(const std::string& id) const
    {
        std::lock_guard lock(m_mutex);
        const auto iterator = m_actions.find(id);
        return iterator == m_actions.end() ? 0 : iterator->second.releaseSerial;
    }

    void sample()
    {
        std::lock_guard lock(m_mutex);
        for (auto& [id, action] : m_actions)
        {
            (void)id;
            const std::uint8_t key = static_cast<std::uint8_t>(action.key);
            const std::uint32_t rawPress = sampler().pressSerial(key);
            const std::uint32_t rawRelease = sampler().releaseSerial(key);
            action.pressSerial += rawPress - action.lastRawPress;
            action.releaseSerial += rawRelease - action.lastRawRelease;
            action.lastRawPress = rawPress;
            action.lastRawRelease = rawRelease;
        }
    }

    std::vector<BindingActionSnapshot> snapshot() const
    {
        std::lock_guard lock(m_mutex);
        std::vector<BindingActionSnapshot> result;
        result.reserve(m_order.size());
        for (const std::string& id : m_order)
        {
            const auto iterator = m_actions.find(id);
            if (iterator != m_actions.end())
            {
                result.push_back({ iterator->second.id, iterator->second.label, iterator->second.key });
            }
        }
        return result;
    }

private:
    static std::wstring settingsPath()
    {
#ifdef _MSC_VER
        wchar_t path[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(g_module, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
            return L"KeybindBridge.bindings.ini";
        }

        wchar_t* slash = wcsrchr(path, L'\\');
        if (slash != nullptr)
        {
            *(slash + 1) = L'\0';
        }
        wcscat_s(path, L"KeybindBridge.bindings.ini");
        return path;
#else
        return {};
#endif
    }

    static std::wstring utf8ToWide(const std::string& text)
    {
#ifdef _MSC_VER
        if (text.empty())
        {
            return {};
        }
        const int characters = MultiByteToWideChar(
            CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (characters <= 0)
        {
            return {};
        }
        std::wstring result(static_cast<std::size_t>(characters), L'\0');
        MultiByteToWideChar(
            CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
            result.data(), characters);
        return result;
#else
        return std::wstring(text.begin(), text.end());
#endif
    }

    static int loadBinding(const std::string& id, const int defaultKey)
    {
#ifdef _MSC_VER
        const std::wstring keyNameWide = utf8ToWide(id);
        const std::wstring fallback = std::to_wstring(defaultKey);
        wchar_t value[32]{};
        GetPrivateProfileStringW(
            L"Bindings", keyNameWide.c_str(), fallback.c_str(), value,
            static_cast<DWORD>(std::size(value)), settingsPath().c_str());
        wchar_t* end = nullptr;
        const long parsed = std::wcstol(value, &end, 10);
        if (end != value && parsed > 0 && parsed < 255 && parsed != VK_END)
        {
            return static_cast<int>(parsed);
        }
#else
        (void)id;
#endif
        return defaultKey;
    }

    static void saveBinding(const std::string& id, const int key)
    {
#ifdef _MSC_VER
        const std::wstring keyNameWide = utf8ToWide(id);
        const std::wstring value = std::to_wstring(key);
        WritePrivateProfileStringW(
            L"Bindings", keyNameWide.c_str(), value.c_str(), settingsPath().c_str());
#else
        (void)id;
        (void)key;
#endif
    }

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, BindingAction> m_actions;
    std::vector<std::string> m_order;
};

BindingRegistry& bindingRegistry()
{
    static BindingRegistry* instance = new BindingRegistry();
    return *instance;
}

void updateRegisteredActions()
{
    bindingRegistry().sample();
}

std::string readSmallTextFile(const std::filesystem::path& path)
{
#ifdef _MSC_VER
    std::ifstream stream(path, std::ios::binary);
#else
    std::ifstream stream(path.string(), std::ios::binary);
#endif
    if (!stream)
    {
        return {};
    }

    stream.seekg(0, std::ios::end);
    const std::streamoff size = stream.tellg();
    if (size <= 0 || size > 256 * 1024)
    {
        return {};
    }
    stream.seekg(0, std::ios::beg);

    std::string result(static_cast<std::size_t>(size), '\0');
    stream.read(result.data(), size);
    return stream ? result : std::string{};
}

std::string jsonStringValue(const std::string& document, const std::string& key)
{
    const std::string quotedKey = "\"" + key + "\"";
    std::size_t cursor = document.find(quotedKey);
    if (cursor == std::string::npos)
    {
        return {};
    }
    cursor = document.find(':', cursor + quotedKey.size());
    if (cursor == std::string::npos)
    {
        return {};
    }
    cursor = document.find('"', cursor + 1);
    if (cursor == std::string::npos)
    {
        return {};
    }

    std::string value;
    for (++cursor; cursor < document.size(); ++cursor)
    {
        const char character = document[cursor];
        if (character == '"')
        {
            return value;
        }
        if (character == '\\' && cursor + 1 < document.size())
        {
            const char escaped = document[++cursor];
            if (escaped == '\\' || escaped == '/' || escaped == '"')
            {
                value.push_back(escaped);
            }
            else if (escaped == 'n')
            {
                value.push_back('\n');
            }
            else if (escaped == 'r')
            {
                value.push_back('\r');
            }
            else if (escaped == 't')
            {
                value.push_back('\t');
            }
            continue;
        }
        value.push_back(character);
    }
    return {};
}

bool validContentId(const std::string& id)
{
    if (id.size() != 36)
    {
        return false;
    }
    for (std::size_t index = 0; index < id.size(); ++index)
    {
        const unsigned char character = static_cast<unsigned char>(id[index]);
        const bool dash = index == 8 || index == 13 || index == 18 || index == 23;
        if ((dash && character != '-') || (!dash && std::isxdigit(character) == 0))
        {
            return false;
        }
    }
    return true;
}

bool validExtensionScript(const std::string& script)
{
    if (script.empty() || script.size() > 240)
    {
        return false;
    }

    const std::filesystem::path relative(script);
    if (relative.is_absolute() || relative.extension() != ".lua")
    {
        return false;
    }
    for (const auto& component : relative)
    {
        if (component == "..")
        {
            return false;
        }
    }
    return true;
}

void inspectExtensionMod(const std::filesystem::path& directory,
                         std::unordered_set<std::string>& uniqueScripts)
{
    const std::string description = readSmallTextFile(directory / "description.json");
    if (description.empty())
    {
        return;
    }

    std::string contentId = jsonStringValue(description, "localId");
    if (contentId.empty())
    {
        contentId = jsonStringValue(description, "contentId");
    }
    if (!validContentId(contentId))
    {
        return;
    }

    if (contentId == kCoreContentId && g_runtimeSource.empty())
    {
        g_runtimeSource = readSmallTextFile(directory / "Scripts" / "KeybindRuntime.lua");
    }

    const std::string manifest = readSmallTextFile(directory / "keybindbridge.json");
    if (manifest.empty())
    {
        return;
    }

    std::string script = jsonStringValue(manifest, "script");
    std::replace(script.begin(), script.end(), '\\', '/');
    if (!validExtensionScript(script))
    {
        return;
    }

    const std::string resourcePath = "$CONTENT_" + contentId + "/" + script;
    const std::string source = readSmallTextFile(directory / script);
    if (source.empty())
    {
        return;
    }
    if (uniqueScripts.insert(resourcePath).second)
    {
        g_extensions.push_back({resourcePath, source});
    }
}

void scanModDirectory(const std::filesystem::path& root,
                      std::unordered_set<std::string>& uniqueScripts)
{
    std::error_code error;
    if (!std::filesystem::is_directory(root, error))
    {
        return;
    }

    for (std::filesystem::directory_iterator iterator(root, error), end;
         !error && iterator != end; iterator.increment(error))
    {
        if (iterator->is_directory(error))
        {
            inspectExtensionMod(iterator->path(), uniqueScripts);
        }
    }
}

void scanAutonomousExtensions()
{
    std::lock_guard lock(g_extensionMutex);
    g_extensions.clear();
    g_runtimeSource.clear();
    std::unordered_set<std::string> uniqueScripts;

#ifdef _MSC_VER
    wchar_t appData[32768]{};
    const DWORD appDataLength = GetEnvironmentVariableW(
        L"APPDATA", appData, static_cast<DWORD>(std::size(appData)));
    if (appDataLength > 0 && appDataLength < std::size(appData))
    {
        const std::filesystem::path userRoot =
            std::filesystem::path(appData) / "Axolot Games" / "Scrap Mechanic" / "User";
        std::error_code error;
        for (std::filesystem::directory_iterator iterator(userRoot, error), end;
             !error && iterator != end; iterator.increment(error))
        {
            if (iterator->is_directory(error))
            {
                scanModDirectory(iterator->path() / "Mods", uniqueScripts);
            }
        }
    }
#endif

    wchar_t modulePath[32768]{};
    const DWORD moduleLength = GetModuleFileNameW(
        g_module, modulePath, static_cast<DWORD>(std::size(modulePath)));
    if (moduleLength > 0 && moduleLength < std::size(modulePath))
    {
        const std::filesystem::path gameRoot =
            std::filesystem::path(modulePath).parent_path().parent_path().parent_path();
        const std::filesystem::path steamApps = gameRoot.parent_path().parent_path();
        scanModDirectory(steamApps / "workshop" / "content" / "387990", uniqueScripts);
    }

    char message[160]{};
    sprintf_s(message, "OK: discovered %zu autonomous extension manifest(s)",
              g_extensions.size());
    logLine(message);
    logLine(g_runtimeSource.empty()
        ? "WARNING: core KeybindRuntime.lua source was not found"
        : "OK: core KeybindRuntime.lua source loaded from disk");
}

std::string checkString(lua_State* state, const int argument)
{
    std::size_t length = 0;
    const char* value = g_lua.checkLString(state, argument, &length);
    return std::string(value, length);
}

bool validActionId(const std::string& id)
{
    if (id.empty() || id.size() > 96)
    {
        return false;
    }

    return std::all_of(id.begin(), id.end(), [](const unsigned char character) {
        return std::isalnum(character) != 0 || character == '.' ||
            character == '_' || character == '-' || character == ':';
    });
}

int parseKeyName(std::string name)
{
    std::string normalized;
    normalized.reserve(name.size());
    for (const unsigned char character : name)
    {
        if (character != ' ' && character != '_' && character != '-')
        {
            normalized.push_back(static_cast<char>(std::toupper(character)));
        }
    }

    if (normalized.size() == 1)
    {
        const unsigned char value = static_cast<unsigned char>(normalized.front());
        if (std::isalnum(value) != 0)
        {
            return value;
        }
    }

    static const std::unordered_map<std::string, int> names = {
        {"MOUSE1", VK_LBUTTON}, {"MOUSE2", VK_RBUTTON},
        {"MOUSE3", VK_MBUTTON}, {"MOUSE4", VK_XBUTTON1},
        {"MOUSE5", VK_XBUTTON2}, {"BACKSPACE", VK_BACK},
        {"TAB", VK_TAB}, {"ENTER", VK_RETURN}, {"RETURN", VK_RETURN},
        {"SHIFT", VK_SHIFT}, {"CTRL", VK_CONTROL}, {"CONTROL", VK_CONTROL},
        {"ALT", VK_MENU}, {"ESC", VK_ESCAPE}, {"ESCAPE", VK_ESCAPE},
        {"SPACE", VK_SPACE}, {"PAGEUP", VK_PRIOR}, {"PAGEDOWN", VK_NEXT},
        {"END", VK_END}, {"HOME", VK_HOME}, {"LEFT", VK_LEFT},
        {"UP", VK_UP}, {"RIGHT", VK_RIGHT}, {"DOWN", VK_DOWN},
        {"INSERT", VK_INSERT}, {"DELETE", VK_DELETE},
        {"LEFTSHIFT", VK_LSHIFT}, {"RIGHTSHIFT", VK_RSHIFT},
        {"LEFTCTRL", VK_LCONTROL}, {"RIGHTCTRL", VK_RCONTROL},
        {"LEFTALT", VK_LMENU}, {"RIGHTALT", VK_RMENU}
    };
    const auto named = names.find(normalized);
    if (named != names.end())
    {
        return named->second;
    }

    if (normalized.size() >= 2 && normalized.front() == 'F')
    {
        const int functionNumber = std::atoi(normalized.c_str() + 1);
        if (functionNumber >= 1 && functionNumber <= 24 &&
            normalized == "F" + std::to_string(functionNumber))
        {
            return VK_F1 + functionNumber - 1;
        }
    }

    if (normalized.rfind("VK", 0) == 0 && normalized.size() > 2)
    {
        const int numeric = std::atoi(normalized.c_str() + 2);
        if (numeric > 0 && numeric < 255)
        {
            return numeric;
        }
    }
    return -1;
}

int checkKeySpec(lua_State* state, const int argument)
{
    int key = -1;
    const int type = g_lua.type(state, argument);
    if (type == LUA_TNUMBER)
    {
        key = checkVirtualKey(state, argument);
    }
    else if (type == LUA_TSTRING)
    {
        key = parseKeyName(checkString(state, argument));
    }
    else
    {
        return g_lua.argumentError(state, argument, "key must be a name such as 'F' or a virtual-key number");
    }

    if (!isCaptureCandidate(key))
    {
        return g_lua.argumentError(state, argument, "unknown key or reserved key (End opens the binding menu)");
    }
    return key;
}

#ifdef _MSC_VER
constexpr UINT kMenuToggleMessage = WM_APP + 31;
constexpr UINT kMenuRefreshMessage = WM_APP + 32;
constexpr UINT_PTR kCaptureTimer = 1;
constexpr int kActionListId = 1001;
constexpr int kRebindButtonId = 1002;
constexpr int kCloseButtonId = 1003;
constexpr int kDetailsTextId = 1004;
constexpr int kStatusTextId = 1005;
constexpr int kResetButtonId = 1006;

std::atomic<HWND> g_bindingMenuWindow{nullptr};
std::atomic<bool> g_bindingMenuStarted{false};
std::vector<BindingActionSnapshot> g_bindingMenuRows;
HWND g_previousGameWindow = nullptr;
std::uint32_t g_menuCaptureToken = 0;
ULONGLONG g_menuCaptureStarted = 0;
bool g_menuCapturing = false;

std::wstring utf8ToWideText(const std::string& text)
{
    if (text.empty())
    {
        return {};
    }
    const int characters = MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (characters <= 0)
    {
        return L"?";
    }
    std::wstring result(static_cast<std::size_t>(characters), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        result.data(), characters);
    return result;
}

HWND menuControl(const HWND window, const int id)
{
    return GetDlgItem(window, id);
}

void updateBindingMenuDetails(const HWND window)
{
    const LRESULT selected = SendMessageW(
        menuControl(window, kActionListId), LB_GETCURSEL, 0, 0);
    std::wstring details = L"Select an action and click Rebind";
    if (selected != LB_ERR && static_cast<std::size_t>(selected) < g_bindingMenuRows.size())
    {
        details = L"ID: " + utf8ToWideText(g_bindingMenuRows[static_cast<std::size_t>(selected)].id);
    }
    SetWindowTextW(menuControl(window, kDetailsTextId), details.c_str());
}

void refreshBindingMenu(const HWND window)
{
    const HWND list = menuControl(window, kActionListId);
    const LRESULT oldSelection = SendMessageW(list, LB_GETCURSEL, 0, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    g_bindingMenuRows = bindingRegistry().snapshot();

    for (const BindingActionSnapshot& action : g_bindingMenuRows)
    {
        const std::wstring line = utf8ToWideText(action.label) + L"    [" +
            utf8ToWideText(keyName(action.key)) + L"]";
        SendMessageW(
            list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    }

    if (!g_bindingMenuRows.empty())
    {
        const LRESULT selection = oldSelection >= 0 &&
            static_cast<std::size_t>(oldSelection) < g_bindingMenuRows.size()
            ? oldSelection : 0;
        SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(selection), 0);
    }
    updateBindingMenuDetails(window);
}

void finishMenuCapture(const HWND window, const wchar_t* status)
{
    if (g_menuCapturing)
    {
        sampler().cancelCapture(g_menuCaptureToken);
    }
    g_menuCapturing = false;
    g_menuCaptureToken = 0;
    KillTimer(window, kCaptureTimer);
    EnableWindow(menuControl(window, kActionListId), TRUE);
    EnableWindow(menuControl(window, kRebindButtonId), TRUE);
    EnableWindow(menuControl(window, kResetButtonId), TRUE);
    SetWindowTextW(menuControl(window, kStatusTextId), status);
}

void hideBindingMenu(const HWND window)
{
    finishMenuCapture(window, L"End - close menu");
    ShowWindow(window, SW_HIDE);
    if (g_previousGameWindow != nullptr && IsWindow(g_previousGameWindow))
    {
        SetForegroundWindow(g_previousGameWindow);
    }
}

void beginMenuCapture(const HWND window)
{
    const LRESULT selected = SendMessageW(
        menuControl(window, kActionListId), LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR || static_cast<std::size_t>(selected) >= g_bindingMenuRows.size())
    {
        MessageBoxW(window, L"Select an action first.", L"KeybindBridge", MB_OK);
        return;
    }

    g_menuCaptureToken = sampler().beginCapture();
    g_menuCaptureStarted = GetTickCount64();
    g_menuCapturing = true;
    EnableWindow(menuControl(window, kActionListId), FALSE);
    EnableWindow(menuControl(window, kRebindButtonId), FALSE);
    EnableWindow(menuControl(window, kResetButtonId), FALSE);
    SetWindowTextW(
        menuControl(window, kStatusTextId),
        L"Press a new key. Escape cancels; End is reserved.");
    SetTimer(window, kCaptureTimer, 25, nullptr);
}

void pollMenuCapture(const HWND window)
{
    if (!g_menuCapturing)
    {
        return;
    }

    int capturedKey = -1;
    if (sampler().takeCapturedKey(g_menuCaptureToken, capturedKey))
    {
        if (capturedKey == VK_ESCAPE)
        {
            finishMenuCapture(window, L"Binding cancelled. End - close menu");
            return;
        }

        const LRESULT selected = SendMessageW(
            menuControl(window, kActionListId), LB_GETCURSEL, 0, 0);
        if (selected != LB_ERR && static_cast<std::size_t>(selected) < g_bindingMenuRows.size())
        {
            bindingRegistry().setKey(
                g_bindingMenuRows[static_cast<std::size_t>(selected)].id,
                capturedKey);
        }
        finishMenuCapture(window, L"Saved. End - close menu");
        refreshBindingMenu(window);
        return;
    }

    if (GetTickCount64() - g_menuCaptureStarted >= 15000)
    {
        finishMenuCapture(window, L"Binding timed out. End - close menu");
    }
}

void layoutBindingMenu(const HWND window)
{
    RECT area{};
    GetClientRect(window, &area);
    const int width = area.right - area.left;
    const int height = area.bottom - area.top;
    MoveWindow(menuControl(window, kStatusTextId), 14, 12, width - 28, 22, TRUE);
    MoveWindow(menuControl(window, kActionListId), 14, 40, width - 28, height - 126, TRUE);
    MoveWindow(menuControl(window, kDetailsTextId), 14, height - 78, width - 28, 20, TRUE);
    MoveWindow(menuControl(window, kRebindButtonId), 14, height - 48, 150, 32, TRUE);
    MoveWindow(menuControl(window, kResetButtonId), 174, height - 48, 120, 32, TRUE);
    MoveWindow(menuControl(window, kCloseButtonId), width - 134, height - 48, 120, 32, TRUE);
}

LRESULT CALLBACK bindingMenuWindowProcedure(
    const HWND window, const UINT message, const WPARAM word, const LPARAM value)
{
    switch (message)
    {
    case WM_CREATE:
    {
        const HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE));
        CreateWindowExW(
            0, L"STATIC", L"End - close menu", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatusTextId)), instance, nullptr);
        CreateWindowExW(
            WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kActionListId)), instance, nullptr);
        CreateWindowExW(
            0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDetailsTextId)), instance, nullptr);
        CreateWindowExW(
            0, L"BUTTON", L"Rebind...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRebindButtonId)), instance, nullptr);
        CreateWindowExW(
            0, L"BUTTON", L"Reset", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kResetButtonId)), instance, nullptr);
        CreateWindowExW(
            0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCloseButtonId)), instance, nullptr);

        const HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        for (const int id : { kStatusTextId, kActionListId, kDetailsTextId,
                              kRebindButtonId, kResetButtonId, kCloseButtonId })
        {
            SendMessageW(menuControl(window, id), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
        refreshBindingMenu(window);
        return 0;
    }
    case WM_SIZE:
        layoutBindingMenu(window);
        return 0;
    case WM_COMMAND:
    {
        const int id = LOWORD(word);
        const int notification = HIWORD(word);
        if (id == kCloseButtonId && notification == BN_CLICKED)
        {
            hideBindingMenu(window);
            return 0;
        }
        if ((id == kRebindButtonId && notification == BN_CLICKED) ||
            (id == kActionListId && notification == LBN_DBLCLK))
        {
            beginMenuCapture(window);
            return 0;
        }
        if (id == kResetButtonId && notification == BN_CLICKED)
        {
            const LRESULT selected = SendMessageW(
                menuControl(window, kActionListId), LB_GETCURSEL, 0, 0);
            if (selected != LB_ERR && static_cast<std::size_t>(selected) < g_bindingMenuRows.size())
            {
                bindingRegistry().resetKey(
                    g_bindingMenuRows[static_cast<std::size_t>(selected)].id);
                SetWindowTextW(menuControl(window, kStatusTextId), L"Default restored.");
                refreshBindingMenu(window);
            }
            return 0;
        }
        if (id == kActionListId && notification == LBN_SELCHANGE)
        {
            updateBindingMenuDetails(window);
            return 0;
        }
        break;
    }
    case WM_TIMER:
        if (word == kCaptureTimer)
        {
            pollMenuCapture(window);
            return 0;
        }
        break;
    case kMenuToggleMessage:
        if (IsWindowVisible(window))
        {
            hideBindingMenu(window);
        }
        else
        {
            g_previousGameWindow = GetForegroundWindow();
            refreshBindingMenu(window);
            ShowWindow(window, SW_SHOW);
            SetForegroundWindow(window);
        }
        return 0;
    case kMenuRefreshMessage:
        refreshBindingMenu(window);
        return 0;
    case WM_CLOSE:
        hideBindingMenu(window);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, word, value);
}

DWORD WINAPI bindingMenuThread(LPVOID)
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t* className = L"KeybindBridgeBindingMenu";
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = bindingMenuWindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground =
        reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_WINDOW + 1));
    windowClass.lpszClassName = className;
    RegisterClassExW(&windowClass);

    const HWND window = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        className,
        L"KeybindBridge - Key bindings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 570, 430,
        nullptr, nullptr, instance, nullptr);
    if (window == nullptr)
    {
        logLine("ERROR: binding menu window could not be created");
        return 1;
    }

    g_bindingMenuWindow.store(window, std::memory_order_release);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    g_bindingMenuWindow.store(nullptr, std::memory_order_release);
    return 0;
}

void startBindingMenu()
{
    bool expected = false;
    if (!g_bindingMenuStarted.compare_exchange_strong(expected, true))
    {
        return;
    }
    const HANDLE thread = CreateThread(nullptr, 0, bindingMenuThread, nullptr, 0, nullptr);
    if (thread != nullptr)
    {
        CloseHandle(thread);
    }
}

void requestBindingMenuToggle()
{
    if (g_featureLevel.load(std::memory_order_acquire) >= 7 ||
        g_gameMenuClaimed.load(std::memory_order_acquire))
    {
        return;
    }
    const HWND window = g_bindingMenuWindow.load(std::memory_order_acquire);
    if (window != nullptr)
    {
        PostMessageW(window, kMenuToggleMessage, 0, 0);
    }
}

void requestBindingMenuRefresh()
{
    const HWND window = g_bindingMenuWindow.load(std::memory_order_acquire);
    if (window != nullptr)
    {
        PostMessageW(window, kMenuRefreshMessage, 0, 0);
    }
}
#else
void startBindingMenu() {}
void requestBindingMenuToggle() {}
void requestBindingMenuRefresh() {}
#endif

int luaRegisterAction(lua_State* state)
{
    const std::string id = checkString(state, 1);
    const std::string label = checkString(state, 2);
    const int defaultKey = checkKeySpec(state, 3);
    if (!validActionId(id))
    {
        return g_lua.argumentError(
            state, 1,
            "action id must contain only letters, digits, '.', '_', '-', ':' and be at most 96 bytes");
    }
    if (label.empty() || label.size() > 160)
    {
        return g_lua.argumentError(state, 2, "action label must be 1..160 bytes");
    }

    const int assignedKey = bindingRegistry().registerAction(id, label, defaultKey);
    requestBindingMenuRefresh();
    g_lua.pushNumber(state, static_cast<lua_Number>(assignedKey));
    return 1;
}

int luaActionKey(lua_State* state)
{
    const int key = bindingRegistry().key(checkString(state, 1));
    if (key < 0)
    {
        g_lua.pushNil(state);
    }
    else
    {
        g_lua.pushNumber(state, static_cast<lua_Number>(key));
    }
    return 1;
}

int luaActionKeyName(lua_State* state)
{
    const int key = bindingRegistry().key(checkString(state, 1));
    if (key < 0)
    {
        g_lua.pushNil(state);
        return 1;
    }

    const std::string name = keyName(key);
    g_lua.pushLString(state, name.data(), name.size());
    return 1;
}

int luaActionIsDown(lua_State* state)
{
    const int key = bindingRegistry().key(checkString(state, 1));
    g_lua.pushBoolean(
        state,
        key >= 0 && sampler().isDown(static_cast<std::uint8_t>(key)));
    return 1;
}

int luaActionPressSerial(lua_State* state)
{
    const std::uint32_t serial = bindingRegistry().pressSerial(checkString(state, 1));
    g_lua.pushNumber(state, static_cast<lua_Number>(serial));
    return 1;
}

int luaActionReleaseSerial(lua_State* state)
{
    const std::uint32_t serial = bindingRegistry().releaseSerial(checkString(state, 1));
    g_lua.pushNumber(state, static_cast<lua_Number>(serial));
    return 1;
}

int luaActionCount(lua_State* state)
{
    const auto actions = bindingRegistry().snapshot();
    g_lua.pushNumber(state, static_cast<lua_Number>(actions.size()));
    return 1;
}

int luaActionId(lua_State* state)
{
    const lua_Integer index = g_lua.checkInteger(state, 1);
    const auto actions = bindingRegistry().snapshot();
    if (index < 1 || static_cast<std::size_t>(index) > actions.size())
    {
        g_lua.pushNil(state);
        return 1;
    }
    const std::string& id = actions[static_cast<std::size_t>(index - 1)].id;
    g_lua.pushLString(state, id.data(), id.size());
    return 1;
}

int luaActionLabel(lua_State* state)
{
    const lua_Integer index = g_lua.checkInteger(state, 1);
    const auto actions = bindingRegistry().snapshot();
    if (index < 1 || static_cast<std::size_t>(index) > actions.size())
    {
        g_lua.pushNil(state);
        return 1;
    }
    const std::string& label = actions[static_cast<std::size_t>(index - 1)].label;
    g_lua.pushLString(state, label.data(), label.size());
    return 1;
}

int luaSetActionKey(lua_State* state)
{
    const std::string id = checkString(state, 1);
    const int key = checkKeySpec(state, 2);
    g_lua.pushBoolean(state, bindingRegistry().setKey(id, key));
    return 1;
}

int luaResetAction(lua_State* state)
{
    g_lua.pushBoolean(state, bindingRegistry().resetKey(checkString(state, 1)));
    return 1;
}

int luaClaimGameMenu(lua_State* state)
{
    g_gameMenuClaimed.store(true, std::memory_order_release);
    g_lua.pushBoolean(state, true);
    return 1;
}

int luaSetGameMenuOpen(lua_State* state)
{
    g_gameMenuOpen.store(
        g_lua.toBoolean(state, 1) != 0,
        std::memory_order_release);
    return 0;
}

int luaIsGameMenuOpen(lua_State* state)
{
    g_lua.pushBoolean(
        state,
        g_gameMenuOpen.load(std::memory_order_acquire));
    return 1;
}

int luaClaimRuntime(lua_State* state)
{
    bool replacedStaleOwner = false;
    const bool claimed = claimRuntimeOwner(state, replacedStaleOwner);
    if (claimed && replacedStaleOwner)
    {
        logLine("OK: stale Lua runtime owner replaced for a new world");
    }
    g_lua.pushBoolean(state, claimed);
    return 1;
}

int luaMenuToggleSerial(lua_State* state)
{
    g_lua.pushNumber(
        state,
        static_cast<lua_Number>(g_menuToggleSerial.load(std::memory_order_acquire)));
    return 1;
}

int luaExtensionCount(lua_State* state)
{
    std::lock_guard lock(g_extensionMutex);
    g_lua.pushNumber(state, static_cast<lua_Number>(g_extensions.size()));
    return 1;
}

int luaExtensionScript(lua_State* state)
{
    const lua_Integer index = g_lua.checkInteger(state, 1);
    std::lock_guard lock(g_extensionMutex);
    if (index < 1 || static_cast<std::size_t>(index) > g_extensions.size())
    {
        g_lua.pushNil(state);
        return 1;
    }
    const std::string& path =
        g_extensions[static_cast<std::size_t>(index - 1)].resourcePath;
    g_lua.pushLString(state, path.data(), path.size());
    return 1;
}

int luaIsDown(lua_State* state)
{
    const int vk = checkVirtualKey(state, 1);
    g_lua.pushBoolean(state, sampler().isDown(static_cast<std::uint8_t>(vk)));
    return 1;
}

int luaPressSerial(lua_State* state)
{
    const int vk = checkVirtualKey(state, 1);
    g_lua.pushNumber(
        state,
        static_cast<lua_Number>(sampler().pressSerial(static_cast<std::uint8_t>(vk))));
    return 1;
}

int luaReleaseSerial(lua_State* state)
{
    const int vk = checkVirtualKey(state, 1);
    g_lua.pushNumber(
        state,
        static_cast<lua_Number>(sampler().releaseSerial(static_cast<std::uint8_t>(vk))));
    return 1;
}

int luaBeginCapture(lua_State* state)
{
    g_lua.pushNumber(state, static_cast<lua_Number>(sampler().beginCapture()));
    return 1;
}

int luaCaptureNext(lua_State* state)
{
    const lua_Integer generation = g_lua.checkInteger(state, 1);
    if (generation < 0)
    {
        return g_lua.argumentError(state, 1, "capture token must be non-negative");
    }

    int key = -1;
    if (!sampler().takeCapturedKey(static_cast<std::uint32_t>(generation), key))
    {
        g_lua.pushNil(state);
        return 1;
    }

    g_lua.pushNumber(state, static_cast<lua_Number>(key));
    return 1;
}

int luaCancelCapture(lua_State* state)
{
    const lua_Integer generation = g_lua.checkInteger(state, 1);
    if (generation >= 0)
    {
        sampler().cancelCapture(static_cast<std::uint32_t>(generation));
    }
    return 0;
}

int luaKeyName(lua_State* state)
{
    const int vk = checkVirtualKey(state, 1);
    const std::string name = keyName(vk);
    g_lua.pushLString(state, name.data(), name.size());
    return 1;
}

int luaIsForeground(lua_State* state)
{
    g_lua.pushBoolean(state, isForegroundProcess());
    return 1;
}

int luaVersion(lua_State* state)
{
    g_lua.pushString(state, kVersion);
    return 1;
}

int luaMonotonicMilliseconds(lua_State* state)
{
    g_lua.pushNumber(
        state,
        static_cast<lua_Number>(monotonicMilliseconds()));
    return 1;
}

int luaDiagnosticLevel(lua_State* state)
{
    g_lua.pushNumber(
        state,
        static_cast<lua_Number>(g_featureLevel.load(std::memory_order_acquire)));
    return 1;
}

int luaRuntimeWarmupComplete(lua_State* state)
{
    const std::uint64_t claimedAt =
        g_runtimeClaimedAtMs.load(std::memory_order_acquire);
    const bool complete =
        claimedAt != 0 && monotonicMilliseconds() - claimedAt >= 2500;
    g_lua.pushBoolean(state, complete);
    return 1;
}

int luaTrace(lua_State* state)
{
    const std::string message = checkString(state, 1);
    logLine(("TRACE: Lua runtime: " + message).c_str());
    return 0;
}

void addConstant(lua_State* state, const char* name, const int value)
{
    g_lua.pushNumber(state, static_cast<lua_Number>(value));
    g_lua.setField(state, -2, name);
}

void addVirtualKeyTable(lua_State* state)
{
    g_lua.createTable(state, 0, 0);
    addConstant(state, "MOUSE1", VK_LBUTTON);
    addConstant(state, "MOUSE2", VK_RBUTTON);
    addConstant(state, "MOUSE3", VK_MBUTTON);
    addConstant(state, "MOUSE4", VK_XBUTTON1);
    addConstant(state, "MOUSE5", VK_XBUTTON2);
    addConstant(state, "BACKSPACE", VK_BACK);
    addConstant(state, "TAB", VK_TAB);
    addConstant(state, "ENTER", VK_RETURN);
    addConstant(state, "SHIFT", VK_SHIFT);
    addConstant(state, "CTRL", VK_CONTROL);
    addConstant(state, "ALT", VK_MENU);
    addConstant(state, "ESCAPE", VK_ESCAPE);
    addConstant(state, "SPACE", VK_SPACE);
    addConstant(state, "LEFT", VK_LEFT);
    addConstant(state, "UP", VK_UP);
    addConstant(state, "RIGHT", VK_RIGHT);
    addConstant(state, "DOWN", VK_DOWN);
    addConstant(state, "HOME", VK_HOME);
    addConstant(state, "END", VK_END);
    addConstant(state, "INSERT", VK_INSERT);
    addConstant(state, "DELETE", VK_DELETE);
    addConstant(state, "LSHIFT", VK_LSHIFT);
    addConstant(state, "RSHIFT", VK_RSHIFT);
    addConstant(state, "LCTRL", VK_LCONTROL);
    addConstant(state, "RCTRL", VK_RCONTROL);
    addConstant(state, "LALT", VK_LMENU);
    addConstant(state, "RALT", VK_RMENU);

    for (int index = 1; index <= 24; ++index)
    {
        const std::string name = "F" + std::to_string(index);
        addConstant(state, name.c_str(), VK_F1 + index - 1);
    }

    g_lua.setField(state, -2, "VK");
}

const luaL_Reg kBaselineFunctions[] = {
    {"registerAction", luaRegisterAction},
    {"actionKey", luaActionKey},
    {"actionKeyName", luaActionKeyName},
    {"actionIsDown", luaActionIsDown},
    {"actionPressSerial", luaActionPressSerial},
    {"actionReleaseSerial", luaActionReleaseSerial},
    {"isDown", luaIsDown},
    {"pressSerial", luaPressSerial},
    {"releaseSerial", luaReleaseSerial},
    {"beginCapture", luaBeginCapture},
    {"captureNext", luaCaptureNext},
    {"cancelCapture", luaCancelCapture},
    {"keyName", luaKeyName},
    {"isForeground", luaIsForeground},
    {"monotonicMilliseconds", luaMonotonicMilliseconds},
    {"version", luaVersion},
    {nullptr, nullptr}
};

const luaL_Reg kExtendedFunctions[] = {
    {"registerAction", luaRegisterAction},
    {"actionKey", luaActionKey},
    {"actionKeyName", luaActionKeyName},
    {"actionIsDown", luaActionIsDown},
    {"actionPressSerial", luaActionPressSerial},
    {"actionReleaseSerial", luaActionReleaseSerial},
    {"actionCount", luaActionCount},
    {"actionId", luaActionId},
    {"actionLabel", luaActionLabel},
    {"setActionKey", luaSetActionKey},
    {"resetAction", luaResetAction},
    {"claimRuntime", luaClaimRuntime},
    {"claimGameMenu", luaClaimGameMenu},
    {"setGameMenuOpen", luaSetGameMenuOpen},
    {"isGameMenuOpen", luaIsGameMenuOpen},
    {"menuToggleSerial", luaMenuToggleSerial},
    {"extensionCount", luaExtensionCount},
    {"extensionScript", luaExtensionScript},
    {"isDown", luaIsDown},
    {"pressSerial", luaPressSerial},
    {"releaseSerial", luaReleaseSerial},
    {"beginCapture", luaBeginCapture},
    {"captureNext", luaCaptureNext},
    {"cancelCapture", luaCancelCapture},
    {"keyName", luaKeyName},
    {"isForeground", luaIsForeground},
    {"monotonicMilliseconds", luaMonotonicMilliseconds},
    {"version", luaVersion},
    {"diagnosticLevel", luaDiagnosticLevel},
    {"runtimeWarmupComplete", luaRuntimeWarmupComplete},
    {"trace", luaTrace},
    {nullptr, nullptr}
};

void pushBridgeTable(lua_State* state)
{
    const luaL_Reg* functions =
        g_featureLevel.load(std::memory_order_acquire) >= 1
            ? kExtendedFunctions
            : kBaselineFunctions;
    g_lua.registerLibrary(state, "keybind_bridge", functions);
    g_lua.pushString(state, kVersion);
    g_lua.setField(state, -2, "_VERSION");
    g_lua.pushString(state, "win32-async");
    g_lua.setField(state, -2, "_BACKEND");
    addVirtualKeyTable(state);
}

void logLine(const char* message);

bool attachBridgeToSmTable(lua_State* state, const int smTableIndex)
{
    const int initialTop = g_lua.getTop(state);
    g_lua.getField(state, smTableIndex, "keybind");
    if (g_lua.type(state, -1) == LUA_TTABLE)
    {
        g_lua.setTop(state, initialTop);
        return false;
    }
    g_lua.setTop(state, initialTop);

    sampler().start();
    pushBridgeTable(state);
    g_lua.pushValue(state, -1);
    g_lua.setField(state, smTableIndex, "keybind");
    g_lua.setTop(state, initialTop);
    return true;
}

bool registerInjectedApi(lua_State* state)
{
    const int initialTop = g_lua.getTop(state);

    g_lua.getField(state, LUA_GLOBALSINDEX, "sm");
    if (g_lua.type(state, -1) != LUA_TTABLE)
    {
        g_lua.setTop(state, initialTop);
        return false;
    }

    const int smTableIndex = g_lua.getTop(state);
    const bool attached = attachBridgeToSmTable(state, smTableIndex);
    g_lua.setTop(state, initialTop);

    if (attached)
    {
        char message[160]{};
        sprintf_s(
            message,
            "OK: sm.keybind registered in global environment of Lua state %p",
            static_cast<void*>(state));
        logLine(message);
    }
    return true;
}

bool registerInjectedApiForCall(lua_State* state, const int argumentCount)
{
    const int initialTop = g_lua.getTop(state);
    const int functionIndex = initialTop - argumentCount;
    if (functionIndex < 1)
    {
        return false;
    }

    g_lua.getEnvironment(state, functionIndex);
    if (g_lua.type(state, -1) != LUA_TTABLE)
    {
        g_lua.setTop(state, initialTop);
        return false;
    }

    const int environmentIndex = g_lua.getTop(state);
    g_lua.getField(state, environmentIndex, "sm");
    if (g_lua.type(state, -1) != LUA_TTABLE)
    {
        g_lua.setTop(state, initialTop);
        return false;
    }

    const int smTableIndex = g_lua.getTop(state);
    const bool attached = attachBridgeToSmTable(state, smTableIndex);
    g_lua.setTop(state, initialTop);

    if (attached)
    {
        char message[160]{};
        sprintf_s(
            message,
            "OK: sm.keybind registered in sandbox environment of Lua state %p",
            static_cast<void*>(state));
        logLine(message);
    }
    return attached;
}

std::string luaErrorText(lua_State* state)
{
    std::size_t length = 0;
    const char* text = g_lua.toLString(state, -1, &length);
    if (text == nullptr)
    {
        return "unknown Lua error";
    }
    return std::string(text, length);
}

bool hasRuntime(lua_State* state)
{
    const int initialTop = g_lua.getTop(state);
    g_lua.getField(state, LUA_GLOBALSINDEX, "__KeybindBridgeRuntime");
    const bool found = g_lua.type(state, -1) == LUA_TTABLE;
    g_lua.setTop(state, initialTop);
    return found;
}

bool environmentSupportsClientRuntime(lua_State* state, const int environmentIndex)
{
    const int initialTop = g_lua.getTop(state);
    g_lua.getField(state, environmentIndex, "sm");
    if (g_lua.type(state, -1) != LUA_TTABLE)
    {
        g_lua.setTop(state, initialTop);
        return false;
    }

    const int smIndex = g_lua.getTop(state);
    g_lua.getField(state, smIndex, "isServerMode");
    if (g_lua.type(state, -1) != LUA_TFUNCTION)
    {
        g_lua.setTop(state, initialTop);
        return false;
    }

    g_insideBridgeRuntime = true;
    const int modeResult = g_originalLuaPcall(state, 0, 1, 0);
    g_insideBridgeRuntime = false;
    if (modeResult != 0 || g_lua.toBoolean(state, -1) != 0)
    {
        g_lua.setTop(state, initialTop);
        return false;
    }

    g_lua.setTop(state, smIndex);
    g_lua.getField(state, smIndex, "gui");
    if (g_lua.type(state, -1) != LUA_TTABLE)
    {
        g_lua.setTop(state, initialTop);
        return false;
    }

    const int guiIndex = g_lua.getTop(state);
    g_lua.getField(state, guiIndex, "createGuiFromLayout");
    const bool supported = g_lua.type(state, -1) == LUA_TFUNCTION;
    g_lua.setTop(state, initialTop);
    return supported;
}

void stageRuntimeEnvironmentForCall(lua_State* state, const int argumentCount)
{
    if (g_originalLuaPcall == nullptr)
    {
        return;
    }

    const int initialTop = g_lua.getTop(state);
    // Never allow a failed staging attempt to reuse the previous callback's
    // environment. That could run client APIs while a server callback is active.
    g_lua.pushNil(state);
    g_lua.setField(state, LUA_GLOBALSINDEX, "__KeybindBridgePendingEnvironment");

    const int functionIndex = initialTop - argumentCount;
    if (functionIndex < 1)
    {
        return;
    }

    g_lua.getEnvironment(state, functionIndex);
    if (g_lua.type(state, -1) != LUA_TTABLE)
    {
        g_lua.setTop(state, initialTop);
        return;
    }
    const int environmentIndex = g_lua.getTop(state);

    g_lua.pushValue(state, environmentIndex);
    g_lua.setField(state, LUA_GLOBALSINDEX, "__KeybindBridgePendingEnvironment");
    g_lua.setTop(state, initialTop);
}

bool stagedEnvironmentSupportsClientRuntime(lua_State* state)
{
    const int initialTop = g_lua.getTop(state);
    g_lua.getField(state, LUA_GLOBALSINDEX, "__KeybindBridgePendingEnvironment");
    if (g_lua.type(state, -1) != LUA_TTABLE)
    {
        g_lua.setTop(state, initialTop);
        return false;
    }

    const int environmentIndex = g_lua.getTop(state);
    const bool supported =
        environmentSupportsClientRuntime(state, environmentIndex);
    g_lua.setTop(state, initialTop);
    return supported;
}

void runNativeExtensions(lua_State* state, const int environmentIndex)
{
    std::vector<ExtensionSource> extensions;
    {
        std::lock_guard lock(g_extensionMutex);
        extensions = g_extensions;
    }

    for (const ExtensionSource& extension : extensions)
    {
        const int initialTop = g_lua.getTop(state);
        logLine(("TRACE: native extension begin: " + extension.resourcePath).c_str());

        const int loadResult = g_lua.loadBuffer(
            state,
            extension.source.data(),
            extension.source.size(),
            extension.resourcePath.c_str());
        if (loadResult != 0)
        {
            const std::string error = luaErrorText(state);
            logLine(("ERROR: native extension compilation failed: " + error).c_str());
            g_lua.setTop(state, initialTop);
            continue;
        }

        g_lua.pushValue(state, environmentIndex);
        if (g_lua.setEnvironment(state, -2) == 0)
        {
            logLine("ERROR: native extension rejected its sandbox environment");
            g_lua.setTop(state, initialTop);
            continue;
        }

        g_insideBridgeRuntime = true;
        const int callResult = g_originalLuaPcall(state, 0, 0, 0);
        g_insideBridgeRuntime = false;
        if (callResult != 0)
        {
            const std::string error = luaErrorText(state);
            logLine(("ERROR: native extension execution failed: " + error).c_str());
            g_lua.setTop(state, initialTop);
            continue;
        }

        logLine(("TRACE: native extension complete: " + extension.resourcePath).c_str());
        g_lua.setTop(state, initialTop);
    }
}

void runNativeLoaderAfterCall(lua_State* state, const bool probeOnly)
{
    if ((!probeOnly && hasRuntime(state)) || g_originalLuaPcall == nullptr)
    {
        return;
    }

    const int initialTop = g_lua.getTop(state);
    g_lua.getField(state, LUA_GLOBALSINDEX, "__KeybindBridgePendingEnvironment");
    if (g_lua.type(state, -1) != LUA_TTABLE)
    {
        g_lua.setTop(state, initialTop);
        return;
    }
    const int environmentIndex = g_lua.getTop(state);

    // Server sandboxes expose much of the same `sm` namespace, including GUI
    // functions that are only callable on the client. The explicit
    // sm.isServerMode() result above is therefore the authoritative guard.
    if (!environmentSupportsClientRuntime(state, environmentIndex))
    {
        g_lua.setTop(state, initialTop);
        return;
    }

    const char* attemptedField = probeOnly
        ? "__KeybindBridgeNativeLoaderProbeAttempted"
        : "__KeybindBridgeBootstrapAttempted";
    g_lua.getField(state, environmentIndex, attemptedField);
    const bool attempted = g_lua.type(state, -1) != 0;
    g_lua.setTop(state, environmentIndex);
    if (attempted)
    {
        g_lua.setTop(state, initialTop);
        return;
    }

    std::string source;
    if (probeOnly)
    {
        source = "KeybindBridgeNativeLoaderProbe = true";
    }
    else
    {
        std::lock_guard lock(g_extensionMutex);
        source = g_runtimeSource;
    }
    if (source.empty())
    {
        g_lua.setTop(state, initialTop);
        return;
    }

    const int loadResult = g_lua.loadBuffer(
        state, source.data(), source.size(), kRuntimeScript);
    if (loadResult != 0)
    {
        const std::string error = luaErrorText(state);
        logLine(("ERROR: native runtime compilation failed: " + error).c_str());
        g_lua.setTop(state, initialTop);
        return;
    }

    g_lua.pushValue(state, environmentIndex);
    if (g_lua.setEnvironment(state, -2) == 0)
    {
        logLine("ERROR: native runtime chunk rejected its sandbox environment");
        g_lua.setTop(state, initialTop);
        return;
    }

    g_insideBridgeRuntime = true;
    const int callResult = g_originalLuaPcall(state, 0, 0, 0);
    g_insideBridgeRuntime = false;
    if (callResult != 0)
    {
        const std::string error = luaErrorText(state);
        logLine(("WARNING: native runtime execution failed: " + error).c_str());
        g_lua.setTop(state, initialTop);
        return;
    }

    if (probeOnly)
    {
        g_lua.pushBoolean(state, true);
        g_lua.setField(state, environmentIndex, attemptedField);
        logLine("TRACE: native Lua loader probe completed");
        g_lua.setTop(state, initialTop);
        return;
    }

    g_lua.getField(state, environmentIndex, "KeybindBridgeRuntime");
    if (g_lua.type(state, -1) != LUA_TTABLE)
    {
        // Another live Lua state still owns the runtime. Do not mark this
        // sandbox as attempted: after the old world closes, a later callback
        // must be allowed to claim ownership and bootstrap the new world.
        g_lua.setTop(state, initialTop);
        return;
    }

    g_lua.pushValue(state, -1);
    g_lua.setField(state, LUA_GLOBALSINDEX, "__KeybindBridgeRuntime");
    g_lua.setTop(state, environmentIndex);

    if (g_featureLevel.load(std::memory_order_acquire) >= 8)
    {
        runNativeExtensions(state, environmentIndex);
    }

    g_lua.pushBoolean(state, true);
    g_lua.setField(state, environmentIndex, attemptedField);
    logLine("OK: KeybindBridge client runtime started");
    g_lua.setTop(state, initialTop);
}

void probeRuntimeEnvironmentAfterCall(lua_State* state)
{
    static std::atomic<bool> probeStarted{false};
    static std::atomic<bool> probeFinished{false};

    const int initialTop = g_lua.getTop(state);
    g_lua.getField(state, LUA_GLOBALSINDEX, "__KeybindBridgePendingEnvironment");
    if (g_lua.type(state, -1) != LUA_TTABLE)
    {
        g_lua.setTop(state, initialTop);
        return;
    }
    const int environmentIndex = g_lua.getTop(state);

    if (!probeStarted.exchange(true, std::memory_order_acq_rel))
    {
        logLine("TRACE: client environment guard begin");
    }
    const bool client = environmentSupportsClientRuntime(state, environmentIndex);
    if (!probeFinished.exchange(true, std::memory_order_acq_rel))
    {
        logLine(client
            ? "TRACE: client environment guard result=client"
            : "TRACE: client environment guard result=not-client");
    }
    g_lua.setTop(state, initialTop);
}

void tickRuntime(lua_State* state)
{
    if (g_originalLuaPcall == nullptr || !isRuntimeOwner(state))
    {
        return;
    }

    const int initialTop = g_lua.getTop(state);
    g_lua.getField(state, LUA_GLOBALSINDEX, "__KeybindBridgeRuntime");
    if (g_lua.type(state, -1) != LUA_TTABLE)
    {
        g_lua.setTop(state, initialTop);
        return;
    }

    const int runtimeIndex = g_lua.getTop(state);
    g_lua.getField(state, runtimeIndex, "update");
    if (g_lua.type(state, -1) != LUA_TFUNCTION)
    {
        g_lua.setTop(state, initialTop);
        return;
    }
    g_lua.pushValue(state, runtimeIndex);

    g_insideBridgeRuntime = true;
    const int callResult = g_originalLuaPcall(state, 1, 0, 0);
    g_insideBridgeRuntime = false;
    if (callResult != 0)
    {
        const std::string error = luaErrorText(state);
        logLine(("ERROR: KeybindBridge client runtime stopped: " + error).c_str());

        // Clear both published references so a later client callback can
        // bootstrap a fresh runtime instead of leaving End/actions dead.
        g_lua.setTop(state, initialTop);
        g_lua.pushNil(state);
        g_lua.setField(state, LUA_GLOBALSINDEX, "__KeybindBridgeRuntime");
        g_lua.getField(state, LUA_GLOBALSINDEX, "__KeybindBridgePendingEnvironment");
        if (g_lua.type(state, -1) == LUA_TTABLE)
        {
            g_lua.pushNil(state);
            g_lua.setField(state, -2, "KeybindBridgeRuntime");
            g_lua.pushNil(state);
            g_lua.setField(state, -2, "__KeybindBridgeBootstrapAttempted");
        }
        g_lua.setTop(state, initialTop);
        releaseRuntimeOwner(state);
        return;
    }
    touchRuntimeOwner(state);
    g_lua.setTop(state, initialTop);
}

void logLine(const char* message)
{
    OutputDebugStringA(message);
    OutputDebugStringA("\n");

    if (g_module == nullptr)
    {
        return;
    }

    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(g_module, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        return;
    }

    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash == nullptr)
    {
        return;
    }
    *(slash + 1) = L'\0';
    wcscat_s(path, L"KeybindBridge.log");

    std::lock_guard lock(g_logWriteMutex);
    std::call_once(g_logFileInitialized, [&path]() {
        // The log describes one game process only. Truncate it on the first
        // write after this DLL is loaded, then append normally for the rest of
        // the process lifetime.
        const HANDLE resetFile = CreateFileW(
            path,
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (resetFile != INVALID_HANDLE_VALUE)
        {
            CloseHandle(resetFile);
        }
    });

    const HANDLE file = CreateFileW(
        path,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }

    DWORD written = 0;
    WriteFile(file, message, static_cast<DWORD>(std::strlen(message)), &written, nullptr);
    WriteFile(file, "\r\n", 2, &written, nullptr);
    CloseHandle(file);
}

int __cdecl hookedLuaPcall(lua_State* state, int argumentCount, int resultCount, int errorFunction)
{
    if (g_insideBridgeRuntime)
    {
        return g_originalLuaPcall(state, argumentCount, resultCount, errorFunction);
    }
    if (!g_loggedHookCall.exchange(true, std::memory_order_acq_rel))
    {
        logLine("OK: hooked lua_pcall was invoked");
    }
    ++g_luaPcallHookDepth;
    registerInjectedApiForCall(state, argumentCount);
    registerInjectedApi(state);
    const int featureLevel = g_featureLevel.load(std::memory_order_acquire);
    const bool environmentStagingEnabled = featureLevel >= 2;
    if (environmentStagingEnabled && g_luaPcallHookDepth == 1)
    {
        stageRuntimeEnvironmentForCall(state, argumentCount);
    }
    const int result = g_originalLuaPcall(state, argumentCount, resultCount, errorFunction);
    if (result == 0 && g_luaPcallHookDepth == 1)
    {
        if (featureLevel == 3)
        {
            probeRuntimeEnvironmentAfterCall(state);
        }
        else if (featureLevel == 4)
        {
            runNativeLoaderAfterCall(state, true);
        }
        else if (featureLevel >= 5)
        {
            // Never execute bridge Lua while another game lua_pcall is active.
            // Scrap Mechanic's wrappers enforce their own stack invariants and
            // the old pre-call bootstrap violated them during initialization.
            runNativeLoaderAfterCall(state, false);
            if (stagedEnvironmentSupportsClientRuntime(state))
            {
                if (!g_loggedClientRuntimeTick.exchange(true, std::memory_order_acq_rel))
                {
                    logLine("TRACE: runtime tick admitted from client callback");
                }
                tickRuntime(state);
            }
        }
    }
    --g_luaPcallHookDepth;
    return result;
}

void __cdecl hookedLuaClose(lua_State* state)
{
    if (releaseRuntimeOwner(state))
    {
        logLine("OK: Lua runtime owner released when world state closed");
    }
    g_originalLuaClose(state);
}

bool installInlineLuaPcallHook()
{
    const LPVOID target = functionAddress(g_lua.protectedCall);
    const LPVOID detour = functionAddress(&hookedLuaPcall);
    LPVOID trampoline = nullptr;
    const MH_STATUS createStatus = MH_CreateHook(target, detour, &trampoline);
    if (createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED)
    {
        char message[128]{};
        sprintf_s(message, "WARNING: MH_CreateHook(lua_pcall) failed (%d)", createStatus);
        logLine(message);
        return false;
    }

    if (trampoline != nullptr)
    {
        g_originalLuaPcall = functionFromAddress<LuaPcall>(trampoline);
    }
    else
    {
        // Only possible when another copy of this module created the hook.
        // The export remains a safe fallback for the IAT path below.
        g_originalLuaPcall = g_lua.protectedCall;
    }

    const MH_STATUS enableStatus = MH_EnableHook(target);
    if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED)
    {
        char message[128]{};
        sprintf_s(message, "WARNING: MH_EnableHook(lua_pcall) failed (%d)", enableStatus);
        logLine(message);
        return false;
    }

    return true;
}

bool installInlineLuaCloseHook()
{
    const LPVOID target = functionAddress(g_lua.close);
    const LPVOID detour = functionAddress(&hookedLuaClose);
    LPVOID trampoline = nullptr;
    const MH_STATUS createStatus = MH_CreateHook(target, detour, &trampoline);
    if (createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED)
    {
        char message[128]{};
        sprintf_s(message, "WARNING: MH_CreateHook(lua_close) failed (%d)", createStatus);
        logLine(message);
        return false;
    }

    g_originalLuaClose = trampoline != nullptr
        ? functionFromAddress<LuaClose>(trampoline)
        : g_lua.close;

    const MH_STATUS enableStatus = MH_EnableHook(target);
    if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED)
    {
        char message[128]{};
        sprintf_s(message, "WARNING: MH_EnableHook(lua_close) failed (%d)", enableStatus);
        logLine(message);
        return false;
    }
    return true;
}

std::size_t patchModuleLuaPcallImports(HMODULE module)
{
    if (module == nullptr)
    {
        return 0;
    }

    auto* base = reinterpret_cast<std::byte*>(module);
    auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
    {
        return 0;
    }

    auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
    {
        return 0;
    }

    const IMAGE_DATA_DIRECTORY& importDirectory =
        ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDirectory.VirtualAddress == 0)
    {
        return 0;
    }

    auto* descriptor =
        reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + importDirectory.VirtualAddress);
    const auto originalAddress = reinterpret_cast<ULONG_PTR>(g_lua.protectedCall);
    const auto replacementAddress = reinterpret_cast<ULONG_PTR>(&hookedLuaPcall);
    std::size_t patchedCount = 0;

    for (; descriptor->Name != 0; ++descriptor)
    {
        const char* libraryName = reinterpret_cast<const char*>(base + descriptor->Name);
        if (_stricmp(libraryName, "lua51.dll") != 0 &&
            _stricmp(libraryName, "luajit.dll") != 0)
        {
            continue;
        }

        auto* firstThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        IMAGE_THUNK_DATA* nameThunk = nullptr;
        if (descriptor->OriginalFirstThunk != 0)
        {
            nameThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
                base + descriptor->OriginalFirstThunk);
        }

        for (std::size_t index = 0; firstThunk[index].u1.Function != 0; ++index)
        {
            bool matches = firstThunk[index].u1.Function == originalAddress;
            if (!matches && nameThunk != nullptr &&
                !IMAGE_SNAP_BY_ORDINAL(nameThunk[index].u1.Ordinal))
            {
                auto* import = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                    base + nameThunk[index].u1.AddressOfData);
                matches = std::strcmp(
                    reinterpret_cast<const char*>(import->Name), "lua_pcall") == 0;
            }

            if (!matches)
            {
                continue;
            }

            if (firstThunk[index].u1.Function == replacementAddress)
            {
                continue;
            }

            DWORD oldProtection = 0;
            if (!VirtualProtect(
                    &firstThunk[index].u1.Function,
                    sizeof(firstThunk[index].u1.Function),
                    PAGE_READWRITE,
                    &oldProtection))
            {
                continue;
            }

            firstThunk[index].u1.Function = replacementAddress;
            FlushInstructionCache(
                GetCurrentProcess(),
                &firstThunk[index].u1.Function,
                sizeof(firstThunk[index].u1.Function));

            DWORD ignoredProtection = 0;
            VirtualProtect(
                &firstThunk[index].u1.Function,
                sizeof(firstThunk[index].u1.Function),
                oldProtection,
                &ignoredProtection);
            ++patchedCount;
        }
    }

    return patchedCount;
}

std::size_t installLuaPcallIatHooks()
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
        GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::size_t patchedCount = 0;

    if (Module32FirstW(snapshot, &entry))
    {
        do
        {
            patchedCount += patchModuleLuaPcallImports(entry.hModule);
        }
        while (Module32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return patchedCount;
}

DWORD WINAPI initializeInjectedBridge(LPVOID)
{
    for (int attempt = 0; attempt < 600; ++attempt)
    {
        if (GetModuleHandleW(L"lua51.dll") != nullptr ||
            GetModuleHandleW(L"luajit.dll") != nullptr)
        {
            break;
        }
        Sleep(100);
    }

    if (!g_lua.load())
    {
        logLine("ERROR: lua51.dll was not found or its Lua 5.1 exports are incomplete");
        return 1;
    }

    sampler().start();
    const int featureLevel = loadFeatureLevel();
    g_featureLevel.store(featureLevel, std::memory_order_release);
    if (featureLevel >= 1)
    {
        scanAutonomousExtensions();
    }
    if (featureLevel < 7)
    {
        startBindingMenu();
    }
    g_originalLuaPcall = g_lua.protectedCall;

    const MH_STATUS initializeStatus = MH_Initialize();
    if (initializeStatus != MH_OK && initializeStatus != MH_ERROR_ALREADY_INITIALIZED)
    {
        char message[128]{};
        sprintf_s(message, "ERROR: MH_Initialize failed (%d)", initializeStatus);
        logLine(message);
        return 2;
    }

    const bool inlineHookInstalled = installInlineLuaPcallHook();
    if (inlineHookInstalled)
    {
        logLine("OK: inline lua_pcall hook installed");
    }

    if (installInlineLuaCloseHook())
    {
        logLine("OK: inline lua_close lifecycle hook installed");
    }

    const std::size_t patchedCount = installLuaPcallIatHooks();
    if (patchedCount > 0)
    {
        char message[128]{};
        sprintf_s(message, "OK: IAT fallback patched %zu lua_pcall import(s)", patchedCount);
        logLine(message);
    }

    if (!inlineHookInstalled && patchedCount == 0)
    {
        logLine("ERROR: no Lua hook could be installed");
        return 3;
    }

    char startupMessage[192]{};
    sprintf_s(
        startupMessage,
        "OK: KeybindBridge 1.0.0 initialized; FeatureLevel=%d",
        featureLevel);
    logLine(startupMessage);
    return 0;
}
} // namespace

extern "C" __declspec(dllexport) int luaopen_keybind_bridge(lua_State* state)
{
    if (!g_lua.load())
    {
        MessageBoxW(
            nullptr,
            L"KeybindBridge could not resolve the Lua 5.1 API from lua51.dll.",
            L"KeybindBridge",
            MB_OK | MB_ICONERROR);
        return 0;
    }

    try
    {
        sampler().start();
        pushBridgeTable(state);

        // Optional fallback for unrestricted Lua 5.1 environments. Scrap
        // Mechanic itself receives this table through the injected pcall hook.
        g_lua.getField(state, LUA_GLOBALSINDEX, "sm");
        if (g_lua.type(state, -1) == LUA_TTABLE)
        {
            g_lua.pushValue(state, -2);
            g_lua.setField(state, -2, "keybind");
        }
        g_lua.setTop(state, -2);
        return 1;
    }
    catch (...)
    {
        return g_lua.raiseError(state, "KeybindBridge failed to start its input sampler");
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
        const HANDLE thread = CreateThread(
            nullptr, 0, initializeInjectedBridge, nullptr, 0, nullptr);
        if (thread != nullptr)
        {
            CloseHandle(thread);
        }
    }
    return TRUE;
}
