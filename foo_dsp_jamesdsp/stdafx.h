#pragma once

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#define _WIN32_IE 0x0700

// Windows headers needed for foobar2000 SDK
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <mmsystem.h>

// ATL compatibility (minimal stub for building without full ATL)
#include <atlcompat.h>

// foobar2000 SDK includes
#include <SDK/foobar2000.h>
#include <helpers/helpers.h>
