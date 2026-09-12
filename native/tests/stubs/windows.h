#pragma once

#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cwchar>
#include <cstring>

#define __declspec(x)
#define __cdecl
#define WINAPI
#define APIENTRY WINAPI
#define CP_UTF8 65001
#define MAPVK_VK_TO_VSC_EX 4
#define MB_OK 0
#define MB_ICONERROR 0x10
#define MAX_PATH 260
#define FILE_APPEND_DATA 0x0004
#define FILE_SHARE_READ 0x0001
#define FILE_SHARE_WRITE 0x0002
#define OPEN_ALWAYS 4
#define FILE_ATTRIBUTE_NORMAL 0x80
#define PAGE_READWRITE 0x04
#define DLL_PROCESS_ATTACH 1
#define TRUE 1
#define IMAGE_DOS_SIGNATURE 0x5A4D
#define IMAGE_NT_SIGNATURE 0x00004550
#define IMAGE_DIRECTORY_ENTRY_IMPORT 1
#define IMAGE_ORDINAL_FLAG64 0x8000000000000000ull
#define IMAGE_SNAP_BY_ORDINAL(value) (((value) & IMAGE_ORDINAL_FLAG64) != 0)

using HWND = void*;
using DWORD = std::uint32_t;
using UINT = unsigned int;
using LONG = std::int32_t;
using SHORT = short;
using HMODULE = void*;
using FARPROC = void (*)();
using HANDLE = void*;
using LPVOID = void*;
using VOID = void;
using LPCSTR = const char*;
using LPCWSTR = const wchar_t*;
using BOOL = int;
using BYTE = unsigned char;
using WORD = std::uint16_t;
using ULONG_PTR = std::uintptr_t;

using LPTHREAD_START_ROUTINE = DWORD (WINAPI*)(LPVOID);

inline const HANDLE INVALID_HANDLE_VALUE = reinterpret_cast<HANDLE>(-1);

struct IMAGE_DOS_HEADER
{
    WORD e_magic;
    BYTE padding[58];
    LONG e_lfanew;
};

struct IMAGE_FILE_HEADER {};

struct IMAGE_DATA_DIRECTORY
{
    DWORD VirtualAddress;
    DWORD Size;
};

struct IMAGE_OPTIONAL_HEADER
{
    IMAGE_DATA_DIRECTORY DataDirectory[16];
};

struct IMAGE_NT_HEADERS
{
    DWORD Signature;
    IMAGE_FILE_HEADER FileHeader;
    IMAGE_OPTIONAL_HEADER OptionalHeader;
};

struct IMAGE_IMPORT_DESCRIPTOR
{
    union
    {
        DWORD Characteristics;
        DWORD OriginalFirstThunk;
    };
    DWORD TimeDateStamp;
    DWORD ForwarderChain;
    DWORD Name;
    DWORD FirstThunk;
};

struct IMAGE_THUNK_DATA
{
    union
    {
        ULONG_PTR ForwarderString;
        ULONG_PTR Function;
        ULONG_PTR Ordinal;
        ULONG_PTR AddressOfData;
    } u1;
};

struct IMAGE_IMPORT_BY_NAME
{
    WORD Hint;
    BYTE Name[1];
};

#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_MBUTTON 0x04
#define VK_XBUTTON1 0x05
#define VK_XBUTTON2 0x06
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#define VK_F1 0x70
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5

inline HWND GetForegroundWindow() { return nullptr; }
inline HMODULE GetModuleHandleW(const wchar_t*) { return nullptr; }
inline FARPROC GetProcAddress(HMODULE, const char*) { return nullptr; }
inline void OutputDebugStringA(const char*) {}
inline DWORD GetModuleFileNameW(HMODULE, wchar_t*, DWORD) { return 0; }
inline HANDLE CreateFileW(const wchar_t*, DWORD, DWORD, LPVOID, DWORD, DWORD, HANDLE) { return nullptr; }
inline BOOL WriteFile(HANDLE, const void*, DWORD, DWORD*, LPVOID) { return TRUE; }
inline BOOL CloseHandle(HANDLE) { return TRUE; }
inline BOOL VirtualProtect(void*, std::size_t, DWORD, DWORD*) { return TRUE; }
inline BOOL FlushInstructionCache(HANDLE, const void*, std::size_t) { return TRUE; }
inline HANDLE GetCurrentProcess() { return nullptr; }
inline void Sleep(DWORD) {}
inline BOOL DisableThreadLibraryCalls(HMODULE) { return TRUE; }
inline HANDLE CreateThread(LPVOID, std::size_t, LPTHREAD_START_ROUTINE, LPVOID, DWORD, DWORD*) { return nullptr; }
inline DWORD GetCurrentProcessId() { return 0; }
inline DWORD GetWindowThreadProcessId(HWND, DWORD*) { return 0; }
inline SHORT GetAsyncKeyState(int) { return 0; }
inline UINT MapVirtualKeyW(UINT, UINT) { return 0; }
inline int GetKeyNameTextW(LONG, wchar_t*, int) { return 0; }
inline int WideCharToMultiByte(UINT, DWORD, const wchar_t*, int, char*, int, const char*, bool*) { return 0; }
inline int MessageBoxW(HWND, const wchar_t*, const wchar_t*, UINT) { return 0; }

template<std::size_t Size>
inline int wcscat_s(wchar_t (&destination)[Size], const wchar_t* source)
{
    std::wcsncat(destination, source, Size - std::wcslen(destination) - 1);
    return 0;
}

inline int _stricmp(const char* left, const char* right)
{
    while (*left != '\0' && *right != '\0')
    {
        const unsigned char a = static_cast<unsigned char>(*left >= 'A' && *left <= 'Z' ? *left + 32 : *left);
        const unsigned char b = static_cast<unsigned char>(*right >= 'A' && *right <= 'Z' ? *right + 32 : *right);
        if (a != b) return a < b ? -1 : 1;
        ++left;
        ++right;
    }
    return static_cast<unsigned char>(*left) - static_cast<unsigned char>(*right);
}

template<std::size_t Size, typename... Arguments>
inline int sprintf_s(char (&buffer)[Size], const char* format, Arguments... arguments)
{
    return std::snprintf(buffer, Size, format, arguments...);
}
