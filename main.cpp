#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <uxtheme.h>
#include <string>
#include <vector>
#include <fstream>
#include <iterator>
#include <cstdio>

#include "StringTable.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

#define IDC_BTN_ADD_REGION  1001
#define IDC_BTN_DEL_REGION  1002
#define IDC_BTN_ADD_KEY     1003
#define IDC_BTN_DEL_KEY     1004
#define IDC_BTN_ADD_CAT     1005
#define IDC_BTN_DEL_CAT     1006
#define IDC_BTN_IMPORT      1007
#define IDC_BTN_EXPORT      1008
#define IDC_LIST            1009

#define IDC_CELL_EDIT       2001

static StringTable g_table;
static HWND g_hWnd = nullptr;
static HWND g_hListView = nullptr;
static HWND g_hEdit = nullptr;
static int g_editKeyIdx = -1;
static int g_editRegionIdx = -1;
static int g_lastItem = -1;
static int g_lastSubItem = -1;
static WNDPROC g_oldEditProc = nullptr;

static HFONT g_hFont = nullptr;

static const char* WINDOW_CLASS = "StringTableEditorClass";
static const char* INPUT_DLG_CLASS = "InputBoxClass";

static std::string g_lastFilePath;

// -----------------------------------------------------------------------
// UTF-8 conversion helpers
// -----------------------------------------------------------------------
static std::string AnsiToUtf8(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), &w[0], n);
    n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string u(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &u[0], n, nullptr, nullptr);
    return u;
}

static std::string Utf8ToAnsi(const std::string& u) {
    if (u.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, u.data(), (int)u.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u.data(), (int)u.size(), &w[0], n);
    n = WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string a(n, '\0');
    WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(), &a[0], n, nullptr, nullptr);
    return a;
}

// -----------------------------------------------------------------------
// Config persistence
// -----------------------------------------------------------------------
static std::string GetConfigPath() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string path(buf);
    size_t pos = path.find_last_of('\\');
    if (pos != std::string::npos) path.resize(pos + 1);
    return path + "last_path.txt";
}

static void SaveLastPath() {
    std::ofstream f(GetConfigPath(), std::ios::binary);
    if (f.is_open()) f.write(g_lastFilePath.data(), g_lastFilePath.size());
}

static void LoadLastPath() {
    std::ifstream f(GetConfigPath(), std::ios::binary);
    if (!f.is_open()) return;
    g_lastFilePath.assign(std::istreambuf_iterator<char>(f), {});
}

// -----------------------------------------------------------------------
// Input dialog (simple text prompt)
// -----------------------------------------------------------------------
static char g_inputBuf[1024];
static bool g_inputCancelled;
static bool g_inputDone;

static LRESULT CALLBACK InputDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                          10, 10, 280, 20, hwnd, nullptr, nullptr, nullptr);
            SetWindowText(GetDlgItem(hwnd, 0), (const char*)cs->lpCreateParams);
            CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                          10, 35, 280, 24, hwnd, (HMENU)100, nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                          130, 70, 75, 26, hwnd, (HMENU)IDOK, nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                          215, 70, 75, 26, hwnd, (HMENU)IDCANCEL, nullptr, nullptr);
            SendMessage(hwnd, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
            SetFocus(GetDlgItem(hwnd, 100));
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                GetWindowTextA(GetDlgItem(hwnd, 100), g_inputBuf, 1024);
                g_inputCancelled = false;
                g_inputDone = true;
                DestroyWindow(hwnd);
            } else if (LOWORD(wParam) == IDCANCEL) {
                g_inputCancelled = true;
                g_inputDone = true;
                DestroyWindow(hwnd);
            }
            return 0;
        case WM_CLOSE:
            g_inputCancelled = true;
            g_inputDone = true;
            DestroyWindow(hwnd);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static bool InputBox(HWND parent, const char* title, const char* prompt, std::string& out) {
    static bool registered = false;
    if (!registered) {
        WNDCLASS wc = {};
        wc.lpfnWndProc = InputDlgProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = INPUT_DLG_CLASS;
        RegisterClass(&wc);
        registered = true;
    }
    g_inputBuf[0] = 0;
    g_inputCancelled = true;
    g_inputDone = false;
    HWND hDlg = CreateWindowEx(0, INPUT_DLG_CLASS, title,
        WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 310, 140,
        parent, nullptr, GetModuleHandle(nullptr), (void*)prompt);
    if (!hDlg) return false;
    RECT rcP, rcD;
    GetWindowRect(parent, &rcP);
    GetWindowRect(hDlg, &rcD);
    int ww = rcD.right - rcD.left, hh = rcD.bottom - rcD.top;
    int x = rcP.left + (rcP.right - rcP.left - ww) / 2;
    int y = rcP.top + (rcP.bottom - rcP.top - hh) / 2;
    SetWindowPos(hDlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    EnableWindow(parent, FALSE);
    MSG msg = {0};
    while (true) {
        BOOL ret = GetMessage(&msg, nullptr, 0, 0);
        if (ret <= 0) break;
        if (g_inputDone) break;
        if (!IsDialogMessage(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    if (IsWindow(parent)) { EnableWindow(parent, TRUE); SetForegroundWindow(parent); }
    if (msg.message == WM_QUIT) PostQuitMessage((int)msg.wParam);
    if (!g_inputCancelled) { out = AnsiToUtf8(g_inputBuf); return true; }
    return false;
}

// -----------------------------------------------------------------------
// Add-Key dialog (with category selector)
// -----------------------------------------------------------------------
static char g_keyNameBuf[1024];
static int  g_keyCatIdx;

static LRESULT CALLBACK AddKeyDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            CreateWindowW(L"STATIC", L"Key name:", WS_CHILD | WS_VISIBLE, 10, 12, 70, 20, hwnd, nullptr, nullptr, nullptr);
            CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 85, 10, 200, 22, hwnd, (HMENU)100, nullptr, nullptr);
            CreateWindowW(L"STATIC", L"Category:", WS_CHILD | WS_VISIBLE, 10, 40, 70, 20, hwnd, nullptr, nullptr, nullptr);
            HWND hCbo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 85, 38, 200, 200, hwnd, (HMENU)101, nullptr, nullptr);
            for (int c = 0; c < g_table.getCategoryCount(); ++c) {
                std::string ansi = Utf8ToAnsi(g_table.getCategory(c));
                SendMessage(hCbo, CB_ADDSTRING, 0, (LPARAM)ansi.c_str());
            }
            SendMessage(hCbo, CB_SETCURSEL, 0, 0);
            CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP, 130, 80, 75, 26, hwnd, (HMENU)IDOK, nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 215, 80, 75, 26, hwnd, (HMENU)IDCANCEL, nullptr, nullptr);
            SendMessage(hwnd, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
            SetFocus(GetDlgItem(hwnd, 100));
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                GetWindowTextA(GetDlgItem(hwnd, 100), g_keyNameBuf, 1024);
                g_keyCatIdx = (int)SendMessage(GetDlgItem(hwnd, 101), CB_GETCURSEL, 0, 0);
                if (g_keyCatIdx < 0) g_keyCatIdx = 0;
                g_inputCancelled = false;
                g_inputDone = true;
                DestroyWindow(hwnd);
            } else if (LOWORD(wParam) == IDCANCEL) {
                g_inputCancelled = true;
                g_inputDone = true;
                DestroyWindow(hwnd);
            }
            return 0;
        case WM_CLOSE:
            g_inputCancelled = true;
            g_inputDone = true;
            DestroyWindow(hwnd);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static bool AddKeyDialog(HWND parent, std::string& keyName, int& catIdx) {
    static bool reg = false;
    if (!reg) {
        WNDCLASS wc = {};
        wc.lpfnWndProc = AddKeyDlgProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "AddKeyDlgClass";
        RegisterClass(&wc);
        reg = true;
    }
    g_keyNameBuf[0] = 0;
    g_keyCatIdx = 0;
    g_inputCancelled = true;
    g_inputDone = false;
    HWND hDlg = CreateWindowEx(0, "AddKeyDlgClass", "Add Key",
        WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 310, 150,
        parent, nullptr, GetModuleHandle(nullptr), nullptr);
    if (!hDlg) return false;
    RECT rcP, rcD;
    GetWindowRect(parent, &rcP);
    GetWindowRect(hDlg, &rcD);
    int ww = rcD.right - rcD.left, hh = rcD.bottom - rcD.top;
    int x = rcP.left + (rcP.right - rcP.left - ww) / 2;
    int y = rcP.top + (rcP.bottom - rcP.top - hh) / 2;
    SetWindowPos(hDlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    EnableWindow(parent, FALSE);
    MSG msg = {0};
    while (true) {
        BOOL ret = GetMessage(&msg, nullptr, 0, 0);
        if (ret <= 0) break;
        if (g_inputDone) break;
        if (!IsDialogMessage(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    if (IsWindow(parent)) { EnableWindow(parent, TRUE); SetForegroundWindow(parent); }
    if (msg.message == WM_QUIT) PostQuitMessage((int)msg.wParam);
    if (!g_inputCancelled && g_keyNameBuf[0]) {
        keyName = AnsiToUtf8(g_keyNameBuf);
        catIdx = g_keyCatIdx;
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------
// ListView helpers
// -----------------------------------------------------------------------
static void RefreshListView() {
    if (!g_hListView) return;

    ListView_DeleteAllItems(g_hListView);

    int oldCols = Header_GetItemCount(ListView_GetHeader(g_hListView));
    for (int i = oldCols - 1; i >= 0; --i)
        ListView_DeleteColumn(g_hListView, i);

    LVCOLUMNA lvc = {};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    lvc.fmt = LVCFMT_LEFT;
    lvc.cx = 150;
    lvc.pszText = const_cast<char*>("Key Name");
    ListView_InsertColumn(g_hListView, 0, &lvc);

    lvc.cx = 120;
    for (int r = 0; r < g_table.getRegionCount(); ++r) {
        std::string ansi = Utf8ToAnsi(g_table.getRegion(r));
        lvc.pszText = &ansi[0];
        ListView_InsertColumn(g_hListView, r + 1, &lvc);
    }

    // Enable groups
    ListView_EnableGroupView(g_hListView, TRUE);

    // Insert category groups (use wide strings for group headers)
    for (int c = 0; c < g_table.getCategoryCount(); ++c) {
        std::string u8 = g_table.getCategory(c);
        int wn = MultiByteToWideChar(CP_UTF8, 0, u8.data(), (int)u8.size(), nullptr, 0);
        std::wstring headerW(wn, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, u8.data(), (int)u8.size(), &headerW[0], wn);
        LVGROUP lg = {};
        lg.cbSize = sizeof(lg);
        lg.mask = LVGF_HEADER | LVGF_GROUPID | LVGF_STATE;
        lg.iGroupId = c;
        lg.stateMask = LVGS_NORMAL;
        lg.state = LVGS_NORMAL;
        lg.pszHeader = &headerW[0];
        ListView_InsertGroup(g_hListView, -1, &lg);
    }

    for (int k = 0; k < g_table.getKeyCount(); ++k) {
        std::string keyAnsi = Utf8ToAnsi(g_table.getKey(k));
        LVITEMA lvi = {};
        lvi.mask = LVIF_TEXT | LVIF_GROUPID;
        lvi.iItem = k;
        lvi.pszText = &keyAnsi[0];
        lvi.iGroupId = g_table.getKeyCategory(k);
        ListView_InsertItem(g_hListView, &lvi);

        for (int r = 0; r < g_table.getRegionCount(); ++r) {
            std::string cellAnsi = Utf8ToAnsi(g_table.getText(r, k));
            lvi.mask = LVIF_TEXT;
            lvi.pszText = &cellAnsi[0];
            lvi.iSubItem = r + 1;
            ListView_SetItem(g_hListView, &lvi);
        }
    }

    // Remove debug read-back
}

// -----------------------------------------------------------------------
// In-place cell editing
// -----------------------------------------------------------------------
static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_KEYDOWN:
            if (wParam == VK_RETURN) {
                char buf[1024];
                GetWindowTextA(hwnd, buf, 1024);
                g_table.setText(g_editRegionIdx, g_editKeyIdx, AnsiToUtf8(buf));
                RefreshListView();
                DestroyWindow(hwnd);
                g_hEdit = nullptr;
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                DestroyWindow(hwnd);
                g_hEdit = nullptr;
                return 0;
            }
            break;
        case WM_KILLFOCUS: {
            if (g_hEdit != hwnd) return 0;
            char buf[1024];
            GetWindowTextA(hwnd, buf, 1024);
            g_table.setText(g_editRegionIdx, g_editKeyIdx, AnsiToUtf8(buf));
            RefreshListView();
            DestroyWindow(hwnd);
            g_hEdit = nullptr;
            return 0;
        }
        case WM_NCDESTROY:
            g_hEdit = nullptr;
            g_editKeyIdx = -1;
            g_editRegionIdx = -1;
            break;
    }
    return CallWindowProc(g_oldEditProc, hwnd, msg, wParam, lParam);
}

static void BeginEditCell(int keyIdx, int regionIdx) {
    if (g_hEdit) {
        char buf[1024];
        GetWindowTextA(g_hEdit, buf, 1024);
        g_table.setText(g_editRegionIdx, g_editKeyIdx, AnsiToUtf8(buf));
        HWND hOld = g_hEdit;
        g_hEdit = nullptr;
        DestroyWindow(hOld);
    }
    g_editKeyIdx = keyIdx;
    g_editRegionIdx = regionIdx;
    RECT rc;
    ListView_GetSubItemRect(g_hListView, keyIdx, regionIdx + 1, LVIR_BOUNDS, &rc);
    const std::string& text = g_table.getText(regionIdx, keyIdx);
    HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
        g_hListView, (HMENU)IDC_CELL_EDIT, GetModuleHandle(nullptr), nullptr);
    if (!hEdit) return;
    SetWindowTextA(hEdit, Utf8ToAnsi(text).c_str());
    HFONT hFont = (HFONT)SendMessage(g_hListView, WM_GETFONT, 0, 0);
    SendMessage(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    g_hEdit = hEdit;
    g_oldEditProc = (WNDPROC)SetWindowLongPtr(hEdit, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
    SetFocus(hEdit);
    SendMessage(hEdit, EM_SETSEL, 0, -1);
}

// -----------------------------------------------------------------------
// Button handlers
// -----------------------------------------------------------------------
static void AddRegion() {
    std::string name;
    if (InputBox(g_hWnd, "Add Region", "Enter region name:", name)) {
        g_table.addRegion(name);
        RefreshListView();
    }
}

static void DeleteRegion() {
    int col = g_lastSubItem - 1;
    if (col < 0 || col >= g_table.getRegionCount()) {
        MessageBox(g_hWnd, "Please click on a cell in the region column first.", "No Region Selected", MB_OK | MB_ICONWARNING);
        return;
    }
    std::string ansi = Utf8ToAnsi(g_table.getRegion(col));
    char msg[256];
    snprintf(msg, sizeof(msg), "Delete region \"%s\"?", ansi.c_str());
    if (MessageBox(g_hWnd, msg, "Confirm", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        g_table.removeRegion(col);
        RefreshListView();
    }
}

static void AddKey() {
    std::string name;
    int catIdx = 0;
    if (AddKeyDialog(g_hWnd, name, catIdx)) {
        g_table.addKey(name, catIdx);
        RefreshListView();
    }
}

static void DeleteKey() {
    int selIndex = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
    if (selIndex < 0 || selIndex >= g_table.getKeyCount()) {
        MessageBox(g_hWnd, "Please select a key row first.", "No Key Selected", MB_OK | MB_ICONWARNING);
        return;
    }
    std::string ansi = Utf8ToAnsi(g_table.getKey(selIndex));
    char msg[256];
    snprintf(msg, sizeof(msg), "Delete key \"%s\"?", ansi.c_str());
    if (MessageBox(g_hWnd, msg, "Confirm", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        g_table.removeKey(selIndex);
        RefreshListView();
    }
}

static void AddCategory() {
    std::string name;
    if (InputBox(g_hWnd, "Add Category", "Enter category name:", name)) {
        g_table.addCategory(name);
        RefreshListView();
    }
}

static void DeleteCategory() {
    int col = g_lastSubItem - 1;
    // Use the category of the row the user clicked on
    int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
    if (sel < 0) sel = g_lastItem;
    if (sel < 0 || sel >= g_table.getKeyCount()) {
        MessageBox(g_hWnd, "Please select a key row first, or click a cell.", "No Key Selected", MB_OK | MB_ICONWARNING);
        return;
    }
    int catIdx = g_table.getKeyCategory(sel);
    if (catIdx == 0) {
        MessageBox(g_hWnd, "Cannot delete the 'default' category.", "Error", MB_OK | MB_ICONWARNING);
        return;
    }
    std::string ansi = Utf8ToAnsi(g_table.getCategory(catIdx));
    char msg[256];
    snprintf(msg, sizeof(msg), "Delete category \"%s\"?\nKeys in this category will move to 'default'.", ansi.c_str());
    if (MessageBox(g_hWnd, msg, "Confirm", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        g_table.removeCategory(catIdx);
        RefreshListView();
    }
}

static void ImportTable() {
    OPENFILENAMEA ofn = {};
    char path[MAX_PATH] = "";
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hWnd;
    ofn.lpstrFilter = "Text Files\0*.txt\0All Files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = "txt";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameA(&ofn)) return;
    if (!g_table.importFromFile(path)) {
        MessageBox(g_hWnd, "Failed to import file.", "Error", MB_OK | MB_ICONERROR);
        return;
    }
    g_lastFilePath = path;
    SaveLastPath();
    RefreshListView();
    MessageBox(g_hWnd, "Import successful!", "Import", MB_OK | MB_ICONINFORMATION);
}

static void ExportTable() {
    OPENFILENAMEA ofn = {};
    char path[MAX_PATH] = "stringtable_export.txt";
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hWnd;
    ofn.lpstrFilter = "Text Files\0*.txt\0All Files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = "txt";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
    if (!GetSaveFileNameA(&ofn)) return;
    if (g_table.exportToFile(path)) {
        g_lastFilePath = path;
        SaveLastPath();
        MessageBox(g_hWnd, "Export successful!", "Export", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBox(g_hWnd, "Failed to export file.", "Error", MB_OK | MB_ICONERROR);
    }
}

// -----------------------------------------------------------------------
// Main window procedure
// -----------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hWnd = hwnd;
            HINSTANCE hInst = ((LPCREATESTRUCT)lParam)->hInstance;
            int y = 5, x = 5;
            auto btn = [&](const wchar_t* text, int id, int w = 88) {
                CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    x, y, w, 28, hwnd, (HMENU)(INT_PTR)id, hInst, nullptr);
                x += w + 4;
            };
            btn(L"Add Region", IDC_BTN_ADD_REGION, 95);
            btn(L"Del Region", IDC_BTN_DEL_REGION, 90);
            btn(L"Add Key", IDC_BTN_ADD_KEY, 80);
            btn(L"Del Key", IDC_BTN_DEL_KEY, 75);
            btn(L"Add Cat", IDC_BTN_ADD_CAT, 75);
            btn(L"Del Cat", IDC_BTN_DEL_CAT, 75);
            btn(L"Import", IDC_BTN_IMPORT, 75);
            btn(L"Export", IDC_BTN_EXPORT, 80);

            g_hListView = CreateWindowW(WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_EDITLABELS | LVS_SHOWSELALWAYS,
                0, 38, 0, 0, hwnd, (HMENU)IDC_LIST, hInst, nullptr);

            NONCLIENTMETRICSW ncm = { sizeof(ncm) };
            if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
                ncm.cbSize = sizeof(NONCLIENTMETRICSW) - sizeof(ncm.lfMessageFont.lfPitchAndFamily);
                SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            }
            g_hFont = CreateFontIndirectW(&ncm.lfMessageFont);

            // Apply font to buttons and ListView
            auto applyFont = [](HWND parent, int id) {
                HWND ctrl = GetDlgItem(parent, id);
                if (ctrl) SendMessage(ctrl, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            };
            for (int id = IDC_BTN_ADD_REGION; id <= IDC_BTN_EXPORT; ++id)
                applyFont(hwnd, id);

            ListView_SetExtendedListViewStyle(g_hListView,
                LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            ListView_SetTextColor(g_hListView, RGB(0, 0, 0));
            ListView_SetTextBkColor(g_hListView, RGB(255, 255, 255));
            ListView_SetBkColor(g_hListView, RGB(255, 255, 255));
            SetWindowTheme(g_hListView, L"", L"");
            SendMessage(g_hListView, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            RefreshListView();
            break;
        }
        case WM_SIZE: {
            int w = LOWORD(lParam), h = HIWORD(lParam);
            if (g_hListView)
                SetWindowPos(g_hListView, nullptr, 0, 38, w, h - 38, SWP_NOZORDER);
            break;
        }
        case WM_CONTEXTMENU: {
            if ((HWND)wParam == g_hListView) {
                int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
                if (sel >= 0 && sel < g_table.getKeyCount()) {
                    int curCat = g_table.getKeyCategory(sel);
                    HMENU hMenu = CreatePopupMenu();
                    for (int c = 0; c < g_table.getCategoryCount(); ++c) {
                        if (c == curCat) continue;
                        std::string a = Utf8ToAnsi(g_table.getCategory(c));
                        AppendMenuA(hMenu, MF_STRING, 100 + c, a.c_str());
                    }
                    POINT pt;
                    GetCursorPos(&pt);
                    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
                    DestroyMenu(hMenu);
                    if (cmd >= 100) {
                        g_table.setKeyCategory(sel, cmd - 100);
                        RefreshListView();
                    }
                }
            }
            break;
        }
        case WM_SETFOCUS:
            if (g_hListView) SetFocus(g_hListView);
            break;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_BTN_ADD_REGION: AddRegion(); break;
                case IDC_BTN_DEL_REGION: DeleteRegion(); break;
                case IDC_BTN_ADD_KEY:    AddKey();    break;
                case IDC_BTN_DEL_KEY:    DeleteKey(); break;
                case IDC_BTN_ADD_CAT:    AddCategory(); break;
                case IDC_BTN_DEL_CAT:    DeleteCategory(); break;
                case IDC_BTN_IMPORT:     ImportTable(); break;
                case IDC_BTN_EXPORT:     ExportTable(); break;
            }
            break;
        case WM_NOTIFY: {
            LPNMHDR hdr = (LPNMHDR)lParam;
            if (hdr->hwndFrom == g_hListView) {
                switch (hdr->code) {
                    case NM_CLICK: {
                        NMITEMACTIVATE* ia = (NMITEMACTIVATE*)lParam;
                        g_lastItem = ia->iItem;
                        g_lastSubItem = ia->iSubItem;
                        break;
                    }
                    case NM_DBLCLK: {
                        NMITEMACTIVATE* ia = (NMITEMACTIVATE*)lParam;
                        if (ia->iItem >= 0 && ia->iSubItem > 0)
                            BeginEditCell(ia->iItem, ia->iSubItem - 1);
                        break;
                    }
                    case NM_RCLICK: {
                        NMITEMACTIVATE* ia = (NMITEMACTIVATE*)lParam;
                        if (ia->iItem >= 0 && ia->iItem < g_table.getKeyCount()) {
                            int curCat = g_table.getKeyCategory(ia->iItem);
                            HMENU hMenu = CreatePopupMenu();
                            for (int c = 0; c < g_table.getCategoryCount(); ++c) {
                                if (c == curCat) continue;
                                std::string a = Utf8ToAnsi(g_table.getCategory(c));
                                AppendMenuA(hMenu, MF_STRING, 100 + c, a.c_str());
                            }
                            POINT pt;
                            GetCursorPos(&pt);
                            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
                            DestroyMenu(hMenu);
                            if (cmd >= 100) {
                                g_table.setKeyCategory(ia->iItem, cmd - 100);
                                RefreshListView();
                            }
                        }
                        break;
                    }

                    case LVN_ENDLABELEDIT: {
                        NMLVDISPINFO* di = (NMLVDISPINFO*)lParam;
                        if (di->item.pszText && di->item.iItem >= 0) {
                            g_table.renameKey(di->item.iItem, AnsiToUtf8(di->item.pszText));
                            RefreshListView();
                        }
                        break;
                    }
                }
            }
            break;
        }
        case WM_DESTROY:
            if (g_hFont) DeleteObject(g_hFont);
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// -----------------------------------------------------------------------
// Entry point
// -----------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icex);
    WNDCLASSEX wc = { sizeof(wc) };
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = WINDOW_CLASS;
    if (!RegisterClassEx(&wc)) return 1;
    HWND hwnd = CreateWindowEx(0, WINDOW_CLASS, "String Table Editor v2",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 850, 500,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    LoadLastPath();
    if (!g_lastFilePath.empty()) {
        std::ifstream f(g_lastFilePath);
        if (f.is_open()) {
            f.close();
            g_table.clear();
            g_table.importFromFile(g_lastFilePath);
            RefreshListView();
        }
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
