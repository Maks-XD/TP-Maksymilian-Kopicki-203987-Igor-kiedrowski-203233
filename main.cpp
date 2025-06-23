#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <string>
#include <queue>
#include <sstream>
#include <iomanip>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

//---------------------------------- KONFIG --------------------------------------------
constexpr int FLOOR_COUNT     = 6;
constexpr double AVG_WEIGHT   = 70.0;   // kg
constexpr double MAX_LOAD     = 600.0;  // kg
constexpr int CAB_SPEED_PX    = 3;      // px per tick (40 ms)
constexpr UINT TIMER_MOVE_ID  = 1;
constexpr UINT TIMER_EMPTY_ID = 2;
constexpr UINT TIMER_INT_MS   = 40;     // 25 FPS

//---------------------------------- ID KONTROLEK --------------------------------------
#define IDC_BASE_CALL   1000
#define IDC_BASE_CAR    2000
#define IDC_BASE_IN     3000
#define IDC_BASE_OUT    4000
#define IDC_STATIC_MASS 5000
#define IDC_STATIC_PAX  5001

//---------------------------------- STRUKTURY -----------------------------------------
struct Elevator
{
    int curFloor = 0;
    double yPosPx = 0;
    bool moving   = false;
    bool dirUp    = true;
    std::queue<int> queue;
    int paxCnt    = 0;

    double loadKg()  const { return paxCnt * AVG_WEIGHT; }
    bool   empty()   const { return paxCnt == 0; }
    bool   overload()const { return loadKg() > MAX_LOAD + 1e-3; }
};

//---------------------------------- GLOBAL --------------------------------------------
HINSTANCE g_hInst;
HWND      g_hWnd;
HWND      g_hMass;
HWND      g_hPax;
Elevator  g_elv;
int       g_floorHpx = 0;
ULONG_PTR g_gdiToken;

//---------------------------------- POMOCNICZE ----------------------------------------
inline int FloorToY(int f) { return (FLOOR_COUNT - 1 - f) * g_floorHpx; }

void UpdateInfo()
{
    std::wstringstream ss;
    ss << L"Ladunek: " << std::fixed << std::setprecision(1) << g_elv.loadKg() << L" kg";
    SetWindowTextW(g_hMass, ss.str().c_str());

    std::wstring pax = L"Osoby: " + std::to_wstring(g_elv.paxCnt);
    SetWindowTextW(g_hPax, pax.c_str());
}

inline void Repaint() { InvalidateRect(g_hWnd, nullptr, FALSE); }

void EnqueueFloor(int f)
{
    if (f < 0 || f >= FLOOR_COUNT) return;
    for (auto q = g_elv.queue; !q.empty(); q.pop())
        if (q.front() == f) return;                // już w kolejce
    if (!g_elv.moving && f == g_elv.curFloor) return; // już stoi

    g_elv.queue.push(f);
    if (!g_elv.moving) {
        g_elv.dirUp  = f > g_elv.curFloor;
        g_elv.moving = true;
        SetTimer(g_hWnd, TIMER_MOVE_ID, TIMER_INT_MS, nullptr);
    }
}

void Arrive(int floor)
{
    g_elv.curFloor = floor;
    g_elv.yPosPx   = FloorToY(floor);
    g_elv.moving   = false;
    KillTimer(g_hWnd, TIMER_MOVE_ID);

    wchar_t buf[16];
    GetWindowTextW(GetDlgItem(g_hWnd, IDC_BASE_OUT + floor), buf, 15);
    int outCnt = std::min(_wtoi(buf), g_elv.paxCnt);
    g_elv.paxCnt -= outCnt;

    GetWindowTextW(GetDlgItem(g_hWnd, IDC_BASE_IN + floor), buf, 15);
    int inCnt = _wtoi(buf);
    if ((g_elv.paxCnt + inCnt) * AVG_WEIGHT > MAX_LOAD) inCnt = 0; // przeciążenie
    g_elv.paxCnt += inCnt;

    UpdateInfo();

    if (!g_elv.queue.empty() && g_elv.queue.front() == floor) g_elv.queue.pop();
    Repaint();

    if (!g_elv.queue.empty()) {
        g_elv.dirUp  = g_elv.queue.front() > g_elv.curFloor;
        g_elv.moving = true;
        SetTimer(g_hWnd, TIMER_MOVE_ID, TIMER_INT_MS, nullptr);
    } else if (g_elv.empty() && floor != 0) {
        SetTimer(g_hWnd, TIMER_EMPTY_ID, 5000, nullptr);
    }
}

//---------------------------------- RYSOWANIE -----------------------------------------
void DrawScene(HDC hdc, const RECT& rc)
{
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    SolidBrush bg(Color(255,255,255));
    g.FillRectangle(&bg,
        (INT)rc.left, (INT)rc.top,
        (INT)(rc.right - rc.left), (INT)(rc.bottom - rc.top));

    Pen line(Color(255,0,0,0),1);
    for (int f = 0; f <= FLOOR_COUNT; ++f)
        g.DrawLine(&line, rc.left + 10, FloorToY(f), rc.right - 10, FloorToY(f));

    int shaftX = rc.right / 2 - 40;
    Pen shaft(Color(255,40,40,40),2);
    g.DrawRectangle(&shaft, shaftX, rc.top + 2, 80, rc.bottom - rc.top - 4);

    Rect cab(shaftX + 2, static_cast<int>(g_elv.yPosPx) + 2, 76, g_floorHpx - 4);
    SolidBrush cabBr(g_elv.overload() ? Color(255,255,0,0) : Color(255,90,120,255));
    g.FillRectangle(&cabBr, cab);

    std::wstring num = std::to_wstring(g_elv.curFloor);
    FontFamily ff(L"Segoe UI");
    Font font(&ff, 10);
    SolidBrush txtBr(Color(255,0,0,0));
    RectF box((REAL)cab.X, (REAL)cab.Y, (REAL)cab.Width, (REAL)cab.Height);
    StringFormat fmt; fmt.SetAlignment(StringAlignmentCenter); fmt.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(num.c_str(), -1, &font, box, &fmt, &txtBr);
}

//---------------------------------- WndProc -------------------------------------------
LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_ERASEBKGND:
        return 1; // zapobiega migotaniu

    case WM_CREATE:
    {
        RECT rc; GetClientRect(h, &rc);
        g_floorHpx  = (rc.bottom - rc.top) / FLOOR_COUNT;
        g_elv.yPosPx = FloorToY(0);

        INITCOMMONCONTROLSEX ic{sizeof(ic), ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&ic);

        for (int f = 0; f < FLOOR_COUNT; ++f) {
            int y = FloorToY(f) + 5;
            CreateWindowW(L"BUTTON", (L"CALL " + std::to_wstring(f)).c_str(), WS_CHILD|WS_VISIBLE,
                5, y, 60, 22, h, (HMENU)(INT_PTR)(IDC_BASE_CALL + f), g_hInst, nullptr);
            CreateWindowW(L"EDIT", L"0", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,
                75, y, 30, 20, h, (HMENU)(INT_PTR)(IDC_BASE_IN + f), g_hInst, nullptr);
            CreateWindowW(L"EDIT", L"0", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,
                110, y, 30, 20, h, (HMENU)(INT_PTR)(IDC_BASE_OUT + f), g_hInst, nullptr);
        }

        for (int f = 0; f < FLOOR_COUNT; ++f) {
            CreateWindowW(L"BUTTON", std::to_wstring(f).c_str(), WS_CHILD|WS_VISIBLE,
                rc.right - 35, 10+50 + f*26, 25, 20, h, (HMENU)(INT_PTR)(IDC_BASE_CAR + f), g_hInst, nullptr);
        }

        g_hMass = CreateWindowW(L"STATIC", L"Ladunek: 0 kg", WS_CHILD|WS_VISIBLE,
            rc.right - 140, 10, 130, 20, h, (HMENU)IDC_STATIC_MASS, g_hInst, nullptr);
        g_hPax = CreateWindowW(L"STATIC", L"Osoby: 0", WS_CHILD|WS_VISIBLE,
            rc.right - 140, 34, 130, 20, h, (HMENU)IDC_STATIC_PAX,  g_hInst, nullptr);
        return 0;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(w);
        if (id >= IDC_BASE_CALL && id < IDC_BASE_CALL + FLOOR_COUNT)
            EnqueueFloor(id - IDC_BASE_CALL);
        else if (id >= IDC_BASE_CAR && id < IDC_BASE_CAR + FLOOR_COUNT)
            EnqueueFloor(id - IDC_BASE_CAR);
        return 0;
    }

    case WM_TIMER:
        if (w == TIMER_MOVE_ID) {
            g_elv.yPosPx += g_elv.dirUp ? -CAB_SPEED_PX : CAB_SPEED_PX;
            Repaint();

            int destY = FloorToY(g_elv.dirUp ? g_elv.curFloor + 1 : g_elv.curFloor - 1);
            if (!g_elv.queue.empty()) destY = FloorToY(g_elv.queue.front());

            if ((g_elv.dirUp && g_elv.yPosPx <= destY) ||
                (!g_elv.dirUp && g_elv.yPosPx >= destY))
            {
                int fl = g_elv.queue.empty() ? g_elv.curFloor : g_elv.queue.front();
                Arrive(fl);
            }
        }
        else if (w == TIMER_EMPTY_ID) {
            KillTimer(h, TIMER_EMPTY_ID);
            if (g_elv.empty()) EnqueueFloor(0);
        }
        return 0;

    case WM_SIZE:
        {
            RECT rc; GetClientRect(h, &rc);
            g_floorHpx = (rc.bottom - rc.top) / FLOOR_COUNT;
        }
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(h, &ps);
        DrawScene(hdc, ps.rcPaint);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

//---------------------------------- WinMain -------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow)
{
    g_hInst = hInst;

    GdiplusStartupInput gsi; GdiplusStartup(&g_gdiToken, &gsi, nullptr);

    const wchar_t CLS_NAME[] = L"ElevatorGDICls";
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = CLS_NAME;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);

    g_hWnd = CreateWindowExW(0, CLS_NAME, L"Symulator windy - GDI+",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 500, 650,
        nullptr, nullptr, hInst, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    GdiplusShutdown(g_gdiToken);
    return 0;
}
