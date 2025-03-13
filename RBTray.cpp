// ****************************************************************************
//
// RBTray
// Copyright (C) 1998-2010  Nikolay Redko, J.D. Purcell
// Copyright (C) 2015 Benbuck Nason
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// ****************************************************************************

#include <windows.h>
#include "RBTray.h"
#include "resource.h"

#define MAXTRAYITEMS 64

static UINT WM_TASKBAR_CREATED;

static HINSTANCE _hInstance;
static HMODULE _hLib;
static HWND _hwndHook;
static HWND _hwndItems[MAXTRAYITEMS];
static HWND _hwndForMenu;

// 在系统托盘列表中查找窗口句柄
int FindInTray(HWND hwnd) {
    for (int i = 0; i < MAXTRAYITEMS; i++) {
        if (_hwndItems[i] == hwnd) {
            return i;
        }
    }
    return -1;
}

// 获取窗口的图标
HICON GetWindowIcon(HWND hwnd) {
    HICON icon;
    if (icon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_SMALL, 0)) {
        return icon;
    }
    if (icon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_BIG, 0)) {
        return icon;
    }
    if (icon = (HICON)GetClassLongPtr(hwnd, GCLP_HICONSM)) {
        return icon;
    }
    if (icon = (HICON)GetClassLongPtr(hwnd, GCLP_HICON)) {
        return icon;
    }
    return LoadIcon(NULL, IDI_WINLOGO);
}

// 将窗口添加到系统托盘
static bool AddToTray(int i) {
    NOTIFYICONDATA nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize           = NOTIFYICONDATA_V2_SIZE;
    nid.hWnd             = _hwndHook;
    nid.uID              = (UINT)i;
    nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYCMD;
    nid.hIcon            = GetWindowIcon(_hwndItems[i]);
    GetWindowText(_hwndItems[i], nid.szTip, sizeof(nid.szTip) / sizeof(nid.szTip[0]));
    nid.uVersion         = NOTIFYICON_VERSION;
    if (!Shell_NotifyIcon(NIM_ADD, &nid)) {
        return false;
    }
    if (!Shell_NotifyIcon(NIM_SETVERSION, &nid)) {
        Shell_NotifyIcon(NIM_DELETE, &nid);
        return false;
    }
    return true;
}

// 将窗口添加到系统托盘
static bool AddWindowToTray(HWND hwnd) {
    int i = FindInTray(NULL);
    if (i == -1) {
        return false;
    }
    _hwndItems[i] = hwnd;
    return AddToTray(i);
}

// 将窗口最小化到系统托盘
static void MinimizeWindowToTray(HWND hwnd) {
    // 不处理 MDI 子窗口的最小化操作
    if ((UINT)GetWindowLongPtr(hwnd, GWL_EXSTYLE) & WS_EX_MDICHILD) {
        return;
    }

    // 如果 hwnd 是子窗口，找到其父窗口（例如，Office 2007（ ribbon 界面）中的最小化按钮位于子窗口中）
    if ((UINT)GetWindowLongPtr(hwnd, GWL_STYLE) & WS_CHILD) {
        hwnd = GetAncestor(hwnd, GA_ROOT);
    }

    // 在调用 AddWindowToTray 之前隐藏窗口，因为有时在程序窗口实际隐藏之前，RefreshWindowInTray 可能会从 ShowWindow 内部调用，
    // 结果会调用 RemoveWindowFromTray，从而立即移除刚刚添加的系统托盘图标。
    ShowWindow(hwnd, SW_MINIMIZE);
    ShowWindow(hwnd, SW_HIDE);

    // 如果窗口还不在系统托盘中，则添加图标
    if (FindInTray(hwnd) == -1) {
        if (!AddWindowToTray(hwnd)) {
          // 如果系统托盘图标添加失败，则恢复程序窗口。
          ShowWindow(hwnd, SW_RESTORE);
          ShowWindow(hwnd, SW_SHOW);
          SetForegroundWindow(hwnd);
          return;
        }
    }
}

// 从系统托盘中移除窗口
static bool RemoveFromTray(int i) {
    NOTIFYICONDATA nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = NOTIFYICONDATA_V2_SIZE;
    nid.hWnd   = _hwndHook;
    nid.uID    = (UINT)i;
    if (!Shell_NotifyIcon(NIM_DELETE, &nid)) {
        return false;
    }
    return true;
}

// 从系统托盘中移除窗口
static bool RemoveWindowFromTray(HWND hwnd) {
    int i = FindInTray(hwnd);
    if (i == -1) {
        return false;
    }
    if (!RemoveFromTray(i)) {
        return false;
    }
    _hwndItems[i] = NULL;
    return true;
}

// 从系统托盘恢复窗口
static void RestoreWindowFromTray(HWND hwnd) {
    ShowWindow(hwnd, SW_RESTORE);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    RemoveWindowFromTray(hwnd);
}

// 从系统托盘关闭窗口
static void CloseWindowFromTray(HWND hwnd) {
    // 使用 PostMessage 避免在程序退出时弹出对话框导致阻塞。
    // 此外，资源管理器窗口会忽略 SendMessage 发送的 WM_CLOSE 消息。
    PostMessage(hwnd, WM_CLOSE, 0, 0);

    Sleep(50);
    if (IsWindow(hwnd)) {
        Sleep(50);
    }

    if (!IsWindow(hwnd)) {
        // 成功关闭
        RemoveWindowFromTray(hwnd);
    }
}

// 刷新系统托盘中的窗口信息
void RefreshWindowInTray(HWND hwnd) {
    int i = FindInTray(hwnd);
    if (i == -1) {
        return;
    }
    if (!IsWindow(hwnd) || IsWindowVisible(hwnd)) {
        RemoveWindowFromTray(hwnd);
    } else {
        NOTIFYICONDATA nid;
        ZeroMemory(&nid, sizeof(nid));
        nid.cbSize = NOTIFYICONDATA_V2_SIZE;
        nid.hWnd   = _hwndHook;
        nid.uID    = (UINT)i;
        nid.uFlags = NIF_TIP;
        GetWindowText(hwnd, nid.szTip, sizeof(nid.szTip) / sizeof(nid.szTip[0]));
        Shell_NotifyIcon(NIM_MODIFY, &nid);
    }
}

// 执行系统托盘菜单
void ExecuteMenu() {
    HMENU hMenu;
    POINT point;

    hMenu = CreatePopupMenu();
    if (!hMenu) {
        MessageBox(NULL, L"创建菜单时出错。", L"RBTray", MB_OK | MB_ICONERROR);
        return;
    }
    AppendMenu(hMenu, MF_STRING, IDM_ABOUT,   L"关于 RBTray");
    AppendMenu(hMenu, MF_STRING, IDM_EXIT,    L"退出 RBTray");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL); // 分隔线
    AppendMenu(hMenu, MF_STRING, IDM_CLOSE,   L"关闭窗口");
    AppendMenu(hMenu, MF_STRING, IDM_RESTORE, L"恢复窗口");

    GetCursorPos(&point);
    SetForegroundWindow(_hwndHook);

    TrackPopupMenu(hMenu, TPM_LEFTBUTTON | TPM_RIGHTBUTTON | TPM_RIGHTALIGN | TPM_BOTTOMALIGN, point.x, point.y, 0, _hwndHook, NULL);

    PostMessage(_hwndHook, WM_USER, 0, 0);
    DestroyMenu(hMenu);
}

// 关于对话框的处理过程
BOOL CALLBACK AboutDlgProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    switch (Msg) {
        case WM_CLOSE:
            PostMessage(hWnd, WM_COMMAND, IDCANCEL, 0);
            break;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK:
                    EndDialog(hWnd, TRUE);
                    break;
                case IDCANCEL:
                    EndDialog(hWnd, FALSE);
                    break;
            }
            break;
        default:
            return FALSE;
    }
    return TRUE;
}

// 钩子窗口的处理过程
LRESULT CALLBACK HookWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_RESTORE:
                    RestoreWindowFromTray(_hwndForMenu);
                    break;
                case IDM_CLOSE:
                    CloseWindowFromTray(_hwndForMenu);
                    break;
                case IDM_ABOUT:
                    DialogBox(_hInstance, MAKEINTRESOURCE(IDD_ABOUT), _hwndHook, (DLGPROC)AboutDlgProc);
                    break;
                case IDM_EXIT:
                    SendMessage(_hwndHook, WM_DESTROY, 0, 0);
                    break;
            }
            break;
        case WM_ADDTRAY:
            MinimizeWindowToTray((HWND)lParam);
            break;
        case WM_REMTRAY:
            RestoreWindowFromTray((HWND)lParam);
            break;
        case WM_REFRTRAY:
            RefreshWindowInTray((HWND)lParam);
            break;
        case WM_TRAYCMD:
            switch ((UINT)lParam) {
                case NIN_SELECT:
                    RestoreWindowFromTray(_hwndItems[wParam]);
                    break;
                case WM_CONTEXTMENU:
                    _hwndForMenu = _hwndItems[wParam];
                    ExecuteMenu();
                    break;
                case WM_MOUSEMOVE:
                    RefreshWindowInTray(_hwndItems[wParam]);
                    break;
            }
            break;
        case WM_HOTKEY:
        {
            HWND fgWnd = GetForegroundWindow();
            if (!fgWnd)
                break;

            LONG style = GetWindowLong(fgWnd, GWL_STYLE);
            if (!(style & WS_MINIMIZEBOX)) {
                // 跳过，没有最小化按钮
                break;
            }

            MinimizeWindowToTray(fgWnd);

            break;
        }
        case WM_DESTROY:
            for (int i = 0; i < MAXTRAYITEMS; i++) {
                if (_hwndItems[i]) {
                    RestoreWindowFromTray(_hwndItems[i]);
                }
            }
            if (_hLib) {
                UnRegisterHook();
                FreeLibrary(_hLib);
            }
            PostQuitMessage(0);
            break;
        default:
            if (msg == WM_TASKBAR_CREATED) {
                for (int i = 0; i < MAXTRAYITEMS; i++) {
                    if (_hwndItems[i]) {
                        AddToTray(i);
                    }
                }
            }
            break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// 程序入口点
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE /*hPrevInstance*/, _In_ LPSTR /*szCmdLine*/, _In_ int /*iCmdShow*/) {
    _hInstance = hInstance;

    int argc;
    LPWSTR * argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool shouldExit = false;
    bool useHook = true;
    for (int a = 0; a < argc; ++a) {
        if (!wcscmp(argv[a], L"--exit")) {
            shouldExit = true;
        }
        if (!wcscmp(argv[a], L"--no-hook")) {
            useHook = false;
        }
    }

    _hwndHook = FindWindow(NAME, NAME);
    if (_hwndHook) {
        if (shouldExit) {
            SendMessage(_hwndHook, WM_CLOSE, 0, 0);
        } else {
            MessageBox(NULL, L"RBTray 已经在运行。", L"RBTray", MB_OK | MB_ICONINFORMATION);
        }
        return 0;
    }

    if (useHook) {
        if (!(_hLib = LoadLibrary(L"RBHook.dll"))) {
            MessageBox(NULL, L"加载 RBHook.dll 时出错。", L"RBTray", MB_OK | MB_ICONERROR);
            return 0;
        }
        if (!RegisterHook(_hLib)) {
            MessageBox(NULL, L"设置钩子过程时出错。", L"RBTray", MB_OK | MB_ICONERROR);
            return 0;
        }
    }

    WNDCLASS wc;
    wc.style         = 0;
    wc.lpfnWndProc   = HookWndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hInstance;
    wc.hIcon         = NULL;
    wc.hCursor       = NULL;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName  = NULL;
    wc.lpszClassName = NAME;
    if (!RegisterClass(&wc)) {
        MessageBox(NULL, L"创建窗口类时出错", L"RBTray", MB_OK | MB_ICONERROR);
        return 0;
    }

    if (!(_hwndHook = CreateWindow(NAME, NAME, WS_OVERLAPPED, 0, 0, 0, 0, (HWND)NULL, (HMENU)NULL, (HINSTANCE)hInstance, (LPVOID)NULL))) {
        MessageBox(NULL, L"创建窗口时出错", L"RBTray", MB_OK | MB_ICONERROR);
        return 0;
    }

    for (int i = 0; i < MAXTRAYITEMS; i++) {
        _hwndItems[i] = NULL;
    }

    WM_TASKBAR_CREATED = RegisterWindowMessage(L"TaskbarCreated");

    BOOL registeredHotKey = RegisterHotKey(_hwndHook, 0, MOD_ALT | MOD_CONTROL, VK_DOWN);
    if (!registeredHotKey) {
        MessageBox(NULL, L"无法注册热键", L"RBTray", MB_OK | MB_ICONERROR);
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (registeredHotKey) {
        UnregisterHotKey(_hwndHook, 0);
    }

    return (int)msg.wParam;
}
