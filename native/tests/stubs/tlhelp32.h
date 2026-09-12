#pragma once

#include <windows.h>

#define TH32CS_SNAPMODULE 0x00000008
#define TH32CS_SNAPMODULE32 0x00000010

struct MODULEENTRY32W
{
    DWORD dwSize;
    DWORD th32ModuleID;
    DWORD th32ProcessID;
    DWORD GlblcntUsage;
    DWORD ProccntUsage;
    BYTE* modBaseAddr;
    DWORD modBaseSize;
    HMODULE hModule;
    wchar_t szModule[256];
    wchar_t szExePath[MAX_PATH];
};

inline HANDLE CreateToolhelp32Snapshot(DWORD, DWORD) { return INVALID_HANDLE_VALUE; }
inline BOOL Module32FirstW(HANDLE, MODULEENTRY32W*) { return 0; }
inline BOOL Module32NextW(HANDLE, MODULEENTRY32W*) { return 0; }
