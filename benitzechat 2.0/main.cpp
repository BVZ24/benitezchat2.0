#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ID_INPUT 102
#define ID_SEND  103
#define ID_IP    104
#define ID_HOST  105
#define ID_CONN  106

#define PORT 8888
#define WM_NET_TEXT (WM_APP + 1)

static HWND hMain, hLog, hInput, hIp;
static SOCKET g_sock = INVALID_SOCKET;
static char g_ip[64];
static int g_host;
static volatile int g_busy;

/* ---------- log helpers ---------- */

static void append_log(const char *text) {
    int len = GetWindowTextLengthA(hLog);
    SendMessageA(hLog, EM_SETSEL, len, len);
    SendMessageA(hLog, EM_REPLACESEL, 0, (LPARAM)text);
}

/* Safe to call from the network thread: hands the text to the UI thread. */
static void post_text(const char *s) {
    PostMessageA(hMain, WM_NET_TEXT, 0, (LPARAM)_strdup(s));
}

/* ---------- networking (runs on its own thread) ---------- */

static void net_run(void) {
    WSADATA wsa;
    struct sockaddr_in addr;
    SOCKET s;
    char buf[512], line[600], out[640];
    int n, i, pos = 0;

    WSAStartup(MAKEWORD(2, 2), &wsa);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);

    if (g_host) {
        SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR ||
            listen(listener, 1) == SOCKET_ERROR) {
            post_text("[-] Could not listen. Is the port already in use?\r\n");
            closesocket(listener);
            return;
        }
        post_text("[*] Hosting. Waiting for your coworker to connect...\r\n");
        s = accept(listener, NULL, NULL);
        closesocket(listener);
        if (s == INVALID_SOCKET) {
            post_text("[-] Accept failed.\r\n");
            return;
        }
    } else {
        s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        addr.sin_addr.s_addr = inet_addr(g_ip);
        if (addr.sin_addr.s_addr == INADDR_NONE ||
            connect(s, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
            post_text("[-] Could not connect. Check the IP address and firewall.\r\n");
            closesocket(s);
            return;
        }
    }

    g_sock = s;
    post_text("[+] Connected!\r\n");

    /* Messages are newline-terminated so merged or split TCP reads still work. */
    while ((n = recv(s, buf, sizeof(buf), 0)) > 0) {
        for (i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                line[pos] = '\0';
                snprintf(out, sizeof(out), "Them: %s\r\n", line);
                post_text(out);
                pos = 0;
            } else if (buf[i] != '\r' && pos < (int)sizeof(line) - 1) {
                line[pos++] = buf[i];
            }
        }
    }

    post_text("[-] Disconnected.\r\n");
    g_sock = INVALID_SOCKET;
    closesocket(s);
}

static DWORD WINAPI net_thread(LPVOID param) {
    (void)param;
    net_run();
    g_busy = 0;
    return 0;
}

static void start_net(int host) {
    HANDLE h;
    if (g_busy) {
        append_log("[!] Already hosting or connected.\r\n");
        return;
    }
    g_host = host;
    GetWindowTextA(hIp, g_ip, sizeof(g_ip));
    g_busy = 1;
    h = CreateThread(NULL, 0, net_thread, NULL, 0, NULL);
    if (h) CloseHandle(h);
    else g_busy = 0;
}

static void send_message(void) {
    char buf[512], out[520];
    GetWindowTextA(hInput, buf, sizeof(buf));
    if (buf[0] == '\0') return;

    if (g_sock == INVALID_SOCKET) {
        append_log("[!] Not connected yet. Click Host or Connect first.\r\n");
        return;
    }

    snprintf(out, sizeof(out), "%s\n", buf);
    if (send(g_sock, out, (int)strlen(out), 0) == SOCKET_ERROR) {
        append_log("[-] Send failed.\r\n");
        return;
    }

    append_log("You: ");
    append_log(buf);
    append_log("\r\n");
    SetWindowTextA(hInput, "");
}

/* ---------- window ---------- */

static BOOL CALLBACK set_font(HWND h, LPARAM lp) {
    (void)lp;
    SendMessageA(h, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    return TRUE;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        hMain = hwnd;
        hIp = CreateWindowA("EDIT", "127.0.0.1",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            10, 10, 150, 24, hwnd, (HMENU)ID_IP, NULL, NULL);
        CreateWindowA("BUTTON", "Host",
            WS_CHILD | WS_VISIBLE,
            170, 10, 90, 24, hwnd, (HMENU)ID_HOST, NULL, NULL);
        CreateWindowA("BUTTON", "Connect",
            WS_CHILD | WS_VISIBLE,
            270, 10, 100, 24, hwnd, (HMENU)ID_CONN, NULL, NULL);
        hLog = CreateWindowA("EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            10, 44, 360, 166, hwnd, NULL, NULL, NULL);
        hInput = CreateWindowA("EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            10, 220, 280, 24, hwnd, (HMENU)ID_INPUT, NULL, NULL);
        CreateWindowA("BUTTON", "Send",
            WS_CHILD | WS_VISIBLE,
            300, 220, 70, 24, hwnd, (HMENU)ID_SEND, NULL, NULL);
        EnumChildWindows(hwnd, set_font, 0);
        SetFocus(hInput);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_SEND:
            send_message();
            SetFocus(hInput);
            break;
        case IDOK: /* Enter key */
            if (GetFocus() != hIp) send_message();
            break;
        case ID_HOST:
            start_net(1);
            break;
        case ID_CONN:
            start_net(0);
            break;
        }
        return 0;

    case WM_NET_TEXT:
        append_log((const char *)lp);
        free((void *)lp);
        return 0;

    case WM_DESTROY:
        if (g_sock != INVALID_SOCKET) closesocket(g_sock);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show) {
    WNDCLASSA wc = {0};
    HWND hwnd;
    MSG m;

    (void)hPrev;
    (void)cmd;

    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "ChatWin";
    RegisterClassA(&wc);

    hwnd = CreateWindowA("ChatWin", "Chat",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
        NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, show);

    while (GetMessageA(&m, NULL, 0, 0) > 0) {
        if (!IsDialogMessage(hwnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
    }
    return 0;
}
