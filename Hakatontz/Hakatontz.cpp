#include <iostream>
#include <Windows.h>
#include <string>
#include <shellapi.h>
#include <fstream>
#include <vector>
#include <Shlwapi.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "Shlwapi.lib")

using namespace std;

HWND TB1 = nullptr;
HWND TB2 = nullptr;
HWND TB3 = nullptr;
HWND Send = nullptr;
WNDPROC OldWndProc1;
WNDPROC OldWndProc2;
WNDPROC OldWndProc3;

NOTIFYICONDATAW g_nid = {};

struct ClientPush {
    wstring client_code;
    wstring name;
    wstring product;
    wstring notification;
};

// ================== UTILS ==================
wstring GetExeFolder() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    PathRemoveFileSpecW(path);
    return wstring(path);
}

std::wstring Utf8ToWstring(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
        (int)str.size(), nullptr, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
        (int)str.size(), &result[0], size);
    return result;
}

vector<string> SplitCSV(const string& line) {
    vector<string> result;
    string field;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (c == '"') {
            if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
                field.push_back('"');
                i++;
            }
            else inQuotes = !inQuotes;
        }
        else if (c == ',' && !inQuotes) {
            result.push_back(move(field));
            field.clear();
        }
        else field.push_back(c);
    }
    result.push_back(move(field));
    return result;
}

// читаем файл целиком, детектируем BOM UTF-8 и возвращаем vector<string> строк в UTF-8
static vector<string> ReadFileLinesUtf8(const wstring& filename) {
    vector<string> out;
    ifstream file(filename, ios::binary);
    if (!file.is_open()) return out;

    // прочитать всё в string
    string data((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
    if (data.size() >= 3 &&
        (unsigned char)data[0] == 0xEF &&
        (unsigned char)data[1] == 0xBB &&
        (unsigned char)data[2] == 0xBF) {
        // убираем BOM
        data = data.substr(3);
    }
    // разделяем по \n (удаляем \r)
    string cur;
    for (char ch : data) {
        if (ch == '\r') continue;
        if (ch == '\n') {
            out.push_back(cur);
            cur.clear();
        }
        else cur.push_back(ch);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

vector<ClientPush> ParseCSV(const wstring& filename) {
    vector<ClientPush> results;
    auto lines = ReadFileLinesUtf8(filename);
    if (lines.empty()) return results;
    bool firstLine = true;
    for (auto& line : lines) {
        if (firstLine) { firstLine = false; continue; }
        auto fields = SplitCSV(line);
        if (fields.size() >= 4) {
            results.push_back(ClientPush{
                Utf8ToWstring(fields[0]),
                Utf8ToWstring(fields[1]),
                Utf8ToWstring(fields[2]),
                Utf8ToWstring(fields[3])
                });
        }
    }
    return results;
}

// ================== TRAY ==================
void InitTray(HWND hWnd) {
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"Cpp Push App");
    g_nid.uCallbackMessage = WM_APP + 1;
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void ShowPush(const wstring&, const wstring& text) {
    NOTIFYICONDATAW nid = g_nid;
    nid.uFlags = NIF_INFO;
    wcscpy_s(nid.szInfoTitle, L"Нажмите, чтобы узнать подробнее");
    wcsncpy_s(nid.szInfo, text.c_str(), _TRUNCATE);
    nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void RemoveTray() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

// ================== DRAG&DROP ==================
LRESULT CALLBACK EditObr(HWND Eokno, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_DROPFILES) {
        HDROP wDrop = (HDROP)wParam;
        wchar_t filePath[MAX_PATH];
        if (DragQueryFileW(wDrop, 0, filePath, MAX_PATH)) {
            const wchar_t* newName = nullptr;
            int EditID = GetDlgCtrlID(Eokno);
            if (EditID == 1) newName = L"transactions.csv";
            else if (EditID == 2) newName = L"transfers.csv";
            else if (EditID == 3) newName = L"clients.csv";

            if (newName) {
                wchar_t dstPath[MAX_PATH] = {};
                // корректно используем буфер размер
                wcscpy_s(dstPath, MAX_PATH, GetExeFolder().c_str());
                wcscat_s(dstPath, MAX_PATH, L"\\");
                wcscat_s(dstPath, MAX_PATH, newName);

                if (CopyFileW(filePath, dstPath, FALSE))
                    SetWindowTextW(Eokno, L"Файл успешно скопирован\n(Вы можете вставить другие файлы)");
                else {
                    // для отладки показываем ошибку
                    wchar_t err[256];
                    swprintf_s(err, L"Ошибка при копировании. Код: %u", GetLastError());
                    MessageBoxW(Eokno, err, L"Error", MB_ICONERROR);
                }
            }
        }
        DragFinish(wDrop);
        return 0;
    }
    WNDPROC prev = (WNDPROC)(GetDlgCtrlID(Eokno) == 1 ? OldWndProc1 :
        GetDlgCtrlID(Eokno) == 2 ? OldWndProc2 : OldWndProc3);
    return CallWindowProcW(prev, Eokno, uMsg, wParam, lParam);
}

// ================== MAIN WINDOW ==================
LRESULT CALLBACK Obr(HWND okno, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_DESTROY:
        RemoveTray();
        PostQuitMessage(0);
        return 0;
    case WM_CREATE:
        InitTray(okno);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == 4) {
            wstring exeFolder = GetExeFolder();
            for (auto& f : { L"transactions.csv", L"transfers.csv", L"clients.csv" }) {
                wstring filePath = exeFolder + L"\\" + f;
                if (GetFileAttributesW(filePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                    MessageBoxW(NULL, (L"Файл отсутствует: " + filePath).c_str(), L"Error", MB_ICONERROR);
                    return 0;
                }
            }

            SHELLEXECUTEINFOW sei = { sizeof(sei) };
            sei.fMask = SEE_MASK_NOCLOSEPROCESS;
            sei.lpVerb = L"open";
            wstring aipyPath = exeFolder + L"\\AIpy.exe";
            sei.lpFile = aipyPath.c_str();
            // важная строчка — задаём рабочую директорию
            sei.lpDirectory = exeFolder.c_str();
            sei.nShow = SW_SHOWNORMAL;
            if (!ShellExecuteExW(&sei)) {
                wchar_t err[256];
                swprintf_s(err, L"Не удалось запустить AIpy. Код: %u", GetLastError());
                MessageBoxW(NULL, err, L"Error", MB_ICONERROR);
                return 0;
            }
            if (sei.hProcess) {
                WaitForSingleObject(sei.hProcess, INFINITE);
                CloseHandle(sei.hProcess);
            }

            auto pushes = ParseCSV(exeFolder + L"\\clients_push.csv");
            for (auto& p : pushes) {
                if (p.notification.empty()) continue;
                wstring title = p.name;
                if (!p.product.empty()) title += L" — " + p.product;
                ShowPush(title, p.notification);
            }
            return 0;
        }
        break;
    case WM_APP + 1:
        if (lParam == NIN_BALLOONUSERCLICK) {
            ShellExecuteW(NULL, L"open", L"https://www.bcc.kz", NULL, NULL, SW_SHOWNORMAL);
        }
        break;
    }
    return DefWindowProc(okno, uMsg, wParam, lParam);
}

// ================== WINMAIN ==================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // Удаляем старые файлы в корне папки, где лежит NotA.exe
    wstring exeFolder = GetExeFolder();
    for (auto& f : {
        L"transactions.csv",
        L"transfers.csv",
        L"clients.csv",
        L"clients_push.csv",
        L"clients_top4.csv" })
    {
        wstring path = exeFolder + L"\\" + f;
        // тихое удаление; можно раскомментировать MessageBox при ошибке
        DeleteFileW(path.c_str());
    }

    WNDCLASSW wc = {};
    wc.hInstance = hInstance;
    wc.lpfnWndProc = Obr;
    wc.lpszClassName = L"UI";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);

    HWND Mokno = CreateWindowExW(0, L"UI", L"NotA", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 540, NULL, NULL, hInstance, nullptr);
    if (!Mokno) return 0;
    ShowWindow(Mokno, nCmdShow);

    TB1 = CreateWindowExW(0, L"EDIT", L"Put Transactions File Here",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY | ES_CENTER | ES_MULTILINE,
        20, 30, 200, 150, Mokno, (HMENU)1, hInstance, nullptr);
    DragAcceptFiles(TB1, TRUE);
    OldWndProc1 = (WNDPROC)SetWindowLongPtrW(TB1, GWLP_WNDPROC, (LONG_PTR)EditObr);

    TB2 = CreateWindowExW(0, L"EDIT", L"Put Transfers File Here",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY | ES_CENTER | ES_MULTILINE,
        370, 30, 200, 150, Mokno, (HMENU)2, hInstance, nullptr);
    DragAcceptFiles(TB2, TRUE);
    OldWndProc2 = (WNDPROC)SetWindowLongPtrW(TB2, GWLP_WNDPROC, (LONG_PTR)EditObr);

    TB3 = CreateWindowExW(0, L"EDIT", L"Put Clients File Here",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY | ES_CENTER | ES_MULTILINE,
        725, 30, 200, 150, Mokno, (HMENU)3, hInstance, nullptr);
    DragAcceptFiles(TB3, TRUE);
    OldWndProc3 = (WNDPROC)SetWindowLongPtrW(TB3, GWLP_WNDPROC, (LONG_PTR)EditObr);

    Send = CreateWindowExW(0, L"BUTTON", L"Отправить данные ИИ",
        WS_CHILD | WS_VISIBLE | WS_BORDER | BS_PUSHBUTTON | ES_CENTER,
        382, 375, 175, 75, Mokno, (HMENU)4, hInstance, nullptr);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}




