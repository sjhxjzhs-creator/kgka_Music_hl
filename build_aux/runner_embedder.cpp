// runner_embedder.cpp — Minimal Win32 Flutter Embedder C API Runner
// Cross-compiled on Linux ARM64 with x86_64-w64-mingw32-g++
// Uses Software Rendering (no OpenGL/ANGLE dependency)
//
// Runtime layout:
//   kgka_music.exe
//   flutter_engine.dll
//   data/
//     icudtl.dat
//     flutter_assets/
//       kernel_blob.bin, AssetManifest.json, fonts/, ...

#include <windows.h>
#include "flutter_embedder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ─── Configuration ───────────────────────────────────────────────
static const int    kDefaultWidth  = 1280;
static const int    kDefaultHeight = 720;
static const double kDpiScale     = 1.0;
static const wchar_t kWindowClass[]  = L"FlutterEmbedderRunner";
static const wchar_t kWindowTitle[]  = L"kgka_Music";

// ─── Global State ────────────────────────────────────────────────
static HWND         g_hwnd       = NULL;
static FlutterEngine g_engine    = NULL;
static int          g_width      = kDefaultWidth;
static int          g_height     = kDefaultHeight;
static HANDLE       g_engine_thread = NULL;
static volatile int g_engine_running = 0;

// Software render buffer (accessed from raster thread + main thread)
static CRITICAL_SECTION g_buffer_lock;
static BITMAPINFOHEADER g_bmih   = {0};
static void*            g_pixels = NULL;
static size_t           g_pixel_size = 0;

// Engine DLL handle
static HMODULE g_engine_dll = NULL;

// ─── Dynamically Resolved Engine Functions ────────────────────────
// We resolve all required functions at startup via GetProcAddress
// so we don't need any import library.

typedef FlutterEngineResult (*FnRun)(
    size_t, const FlutterRendererConfig*, const FlutterProjectArgs*,
    void*, FLUTTER_API_SYMBOL(FlutterEngine)*);
typedef FlutterEngineResult (*FnShutdown)(FLUTTER_API_SYMBOL(FlutterEngine));
typedef FlutterEngineResult (*FnSendWindowMetricsEvent)(
    FLUTTER_API_SYMBOL(FlutterEngine), const FlutterWindowMetricsEvent*);
typedef FlutterEngineResult (*FnSendPointerEvent)(
    FLUTTER_API_SYMBOL(FlutterEngine), const FlutterPointerEvent*, size_t);
typedef FlutterEngineResult (*FnSendKeyEvent)(
    FLUTTER_API_SYMBOL(FlutterEngine), const FlutterKeyEvent*,
    FlutterKeyEventCallback, void*);
typedef uint64_t (*FnGetCurrentTime)(void);

static FnRun                          pRun                    = NULL;
static FnShutdown                     pShutdown               = NULL;
static FnSendWindowMetricsEvent       pSendWindowMetricsEvent = NULL;
static FnSendPointerEvent             pSendPointerEvent       = NULL;
static FnSendKeyEvent                 pSendKeyEvent           = NULL;
static FnGetCurrentTime               pGetCurrentTime         = NULL;

static int ResolveEngineFunctions(void) {
    g_engine_dll = LoadLibraryW(L"flutter_engine.dll");
    if (!g_engine_dll) {
        fprintf(stderr, "ERROR: Cannot load flutter_engine.dll (err=%lu)\n",
                GetLastError());
        return -1;
    }

    #define RESOLVE(name) do { \
        p##name = (Fn##name)GetProcAddress(g_engine_dll, #name); \
        if (!p##name) { \
            fprintf(stderr, "ERROR: Cannot resolve " #name "\n"); \
            return -1; \
        } \
    } while(0)

    RESOLVE(Run);
    RESOLVE(Shutdown);
    RESOLVE(SendWindowMetricsEvent);
    RESOLVE(SendPointerEvent);
    RESOLVE(SendKeyEvent);
    RESOLVE(GetCurrentTime);

    #undef RESOLVE
    return 0;
}

// ─── Software Renderer Callback ──────────────────────────────────
// Called on engine's raster thread when a frame is ready.
// Receives RGBA pixels in |allocation|, |row_bytes| per row, |height| rows.
static bool OnSoftwarePresent(void* user_data,
                               const void* allocation,
                               size_t row_bytes,
                               size_t height) {
    size_t required = row_bytes * height;
    void* local_copy = malloc(required);
    if (!local_copy) return false;
    memcpy(local_copy, allocation, required);

    int w = (int)(row_bytes / 4);  // BGRA = 4 bytes per pixel
    int h = (int)height;

    EnterCriticalSection(&g_buffer_lock);
    // Free old buffer if size changed
    if (g_pixels) free(g_pixels);
    g_pixels = local_copy;
    g_pixel_size = required;
    g_bmih.biSize      = sizeof(BITMAPINFOHEADER);
    g_bmih.biWidth     = w;
    g_bmih.biHeight    = -h;  // negative = top-down
    g_bmih.biPlanes    = 1;
    g_bmih.biBitCount  = 32;
    g_bmih.biCompression = BI_RGB;
    g_bmih.biSizeImage  = (DWORD)required;
    LeaveCriticalSection(&g_buffer_lock);

    // Trigger repaint on main thread (thread-safe)
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
    return true;
}

// ─── Engine Thread Procedure ─────────────────────────────────────
typedef struct {
    const char* assets_path;
    const char* icu_data_path;
} EngineArgs;

static DWORD WINAPI EngineThreadProc(LPVOID param) {
    EngineArgs* args = (EngineArgs*)param;

    // Configure software renderer
    FlutterSoftwareRendererConfig software_config = {};
    software_config.struct_size = sizeof(FlutterSoftwareRendererConfig);
    software_config.surface_present_callback = OnSoftwarePresent;

    FlutterRendererConfig renderer_config = {};
    renderer_config.type = kSoftware;
    renderer_config.software = software_config;

    // Configure project args
    FlutterProjectArgs project_args = {};
    project_args.struct_size = sizeof(FlutterProjectArgs);
    project_args.assets_path   = args->assets_path;
    project_args.icu_data_path = args->icu_data_path;
    project_args.command_line_argc = 0;
    project_args.command_line_argv = NULL;

    printf("[Embedder] Starting Flutter engine...\n");
    printf("[Embedder]   assets: %s\n", args->assets_path);
    printf("[Embedder]   icu:    %s\n", args->icu_data_path);

    FlutterEngine engine = NULL;
    FlutterEngineResult result = pRun(
        FLUTTER_ENGINE_VERSION,
        &renderer_config,
        &project_args,
        NULL,  // user_data
        &engine
    );

    if (result != kSuccess) {
        fprintf(stderr, "ERROR: FlutterEngineRun failed with result %d\n", result);
        g_engine_running = 0;
        if (g_hwnd) PostMessage(g_hwnd, WM_DESTROY, 0, 0);
        return 1;
    }

    g_engine = engine;
    g_engine_running = 1;

    // Send initial window metrics
    FlutterWindowMetricsEvent metrics = {};
    metrics.struct_size  = sizeof(FlutterWindowMetricsEvent);
    metrics.width        = (size_t)g_width;
    metrics.height       = (size_t)g_height;
    metrics.pixel_ratio  = kDpiScale;
    pSendWindowMetricsEvent(engine, &metrics);

    // Send pointer add event
    FlutterPointerEvent pointer_add = {};
    pointer_add.struct_size   = sizeof(FlutterPointerEvent);
    pointer_add.phase         = kAdd;
    pointer_add.timestamp     = pGetCurrentTime();
    pointer_add.x             = 0.0;
    pointer_add.y             = 0.0;
    pointer_add.device        = 0;
    pointer_add.device_kind   = kFlutterPointerDeviceKindMouse;
    pSendPointerEvent(engine, &pointer_add, 1);

    printf("[Embedder] Flutter engine running.\n");

    // NOTE: FlutterEngineRun does NOT block — it creates internal threads
    // and returns. The engine runs on those threads.
    // Wait here until shutdown is signaled.
    while (g_engine_running) {
        Sleep(100);
    }

    printf("[Embedder] Shutting down...\n");
    pShutdown(engine);
    g_engine = NULL;

    return 0;
}

// ─── Send Pointer Events ────────────────────────────────────────
static void SendPointerEvent(FlutterPointerPhase phase,
                              double x, double y,
                              FlutterPointerSignalKind signal,
                              double scroll_dx, double scroll_dy,
                              int64_t buttons) {
    if (!g_engine || !g_engine_running) return;

    FlutterPointerEvent event = {};
    event.struct_size    = sizeof(FlutterPointerEvent);
    event.phase          = phase;
    event.timestamp      = pGetCurrentTime();
    event.x              = x;
    event.y              = y;
    event.device         = 0;
    event.signal_kind    = signal;
    event.scroll_delta_x = scroll_dx;
    event.scroll_delta_y = scroll_dy;
    event.device_kind    = kFlutterPointerDeviceKindMouse;
    event.buttons        = buttons;
    pSendPointerEvent(g_engine, &event, 1);
}

// ─── Window Procedure ────────────────────────────────────────────
static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg,
                                 WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            EnterCriticalSection(&g_buffer_lock);
            if (g_pixels && g_pixel_size > 0) {
                BITMAPINFO bmi = {};
                bmi.bmiHeader = g_bmih;
                StretchDIBits(hdc,
                    0, 0, g_width, g_height,         // dest rect
                    0, 0, g_bmih.biWidth, -g_bmih.biHeight, // src rect
                    g_pixels, &bmi,
                    DIB_RGB_COLORS, SRCCOPY);
            } else {
                // Fill gray background before engine starts
                RECT rc;
                GetClientRect(hwnd, &rc);
                FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
            }
            LeaveCriticalSection(&g_buffer_lock);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SIZE: {
            int new_w = LOWORD(lParam);
            int new_h = HIWORD(lParam);
            if (new_w > 0 && new_h > 0) {
                g_width = new_w;
                g_height = new_h;
                if (g_engine && g_engine_running) {
                    FlutterWindowMetricsEvent metrics = {};
                    metrics.struct_size  = sizeof(FlutterWindowMetricsEvent);
                    metrics.width        = (size_t)g_width;
                    metrics.height       = (size_t)g_height;
                    metrics.pixel_ratio  = kDpiScale;
                    pSendWindowMetricsEvent(g_engine, &metrics);
                }
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            double x = (double)(short)LOWORD(lParam);
            double y = (double)(short)HIWORD(lParam);
            SendPointerEvent(kDown, x, y, kFlutterPointerSignalKindNone,
                             0, 0, kFlutterPointerButtonMousePrimary);
            SetCapture(hwnd);
            return 0;
        }
        case WM_LBUTTONUP: {
            double x = (double)(short)LOWORD(lParam);
            double y = (double)(short)HIWORD(lParam);
            SendPointerEvent(kUp, x, y, kFlutterPointerSignalKindNone,
                             0, 0, 0);
            ReleaseCapture();
            return 0;
        }
        case WM_MOUSEMOVE: {
            double x = (double)(short)LOWORD(lParam);
            double y = (double)(short)HIWORD(lParam);
            int64_t buttons = 0;
            if (wParam & MK_LBUTTON) buttons |= kFlutterPointerButtonMousePrimary;
            if (wParam & MK_RBUTTON) buttons |= kFlutterPointerButtonMouseSecondary;
            FlutterPointerPhase phase = (buttons != 0) ? kMove : kHover;
            SendPointerEvent(phase, x, y, kFlutterPointerSignalKindNone,
                             0, 0, buttons);
            return 0;
        }
        case WM_RBUTTONDOWN: {
            double x = (double)(short)LOWORD(lParam);
            double y = (double)(short)HIWORD(lParam);
            SendPointerEvent(kDown, x, y, kFlutterPointerSignalKindNone,
                             0, 0, kFlutterPointerButtonMouseSecondary);
            return 0;
        }
        case WM_RBUTTONUP: {
            double x = (double)(short)LOWORD(lParam);
            double y = (double)(short)HIWORD(lParam);
            SendPointerEvent(kUp, x, y, kFlutterPointerSignalKindNone,
                             0, 0, 0);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            POINT pt;
            pt.x = (short)LOWORD(lParam);
            pt.y = (short)HIWORD(lParam);
            ScreenToClient(hwnd, &pt);
            double delta = (double)(short)HIWORD(wParam) / WHEEL_DELTA * 40.0;
            SendPointerEvent(kMove, (double)pt.x, (double)pt.y,
                             kFlutterPointerSignalKindScroll,
                             0.0, -delta, 0);
            return 0;
        }

        case WM_KEYDOWN:
        case WM_KEYUP: {
            // Send key event to Flutter engine
            if (g_engine && g_engine_running) {
                FlutterKeyEvent event = {};
                event.struct_size = sizeof(FlutterKeyEvent);
                event.timestamp = (double)pGetCurrentTime();
                event.type = (uMsg == WM_KEYDOWN)
                    ? kFlutterKeyEventTypeDown
                    : kFlutterKeyEventTypeUp;
                event.physical = (uint64_t)wParam; // Win32 virtual key → USB HID (simplified)
                event.logical  = (uint64_t)wParam; // logical key ID (simplified)
                event.character = NULL;
                event.synthesized = false;
                event.device_type = kFlutterKeyEventDeviceTypeKeyboard;
                pSendKeyEvent(g_engine, &event, NULL, NULL);
            }
            return 0;
        }

        case WM_DESTROY: {
            g_engine_running = 0;
            if (g_engine_thread) {
                WaitForSingleObject(g_engine_thread, 5000);
                CloseHandle(g_engine_thread);
                g_engine_thread = NULL;
            }
            PostQuitMessage(0);
            return 0;
        }

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

// ─── WinMain ─────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    InitializeCriticalSection(&g_buffer_lock);

    // Resolve engine DLL functions
    if (ResolveEngineFunctions() != 0) {
        MessageBoxW(NULL, L"Cannot load flutter_engine.dll.\n"
                    L"Make sure it is in the same directory as this executable.",
                    L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    // Create window
    g_hwnd = CreateWindowExW(
        0, kWindowClass, kWindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        kDefaultWidth, kDefaultHeight,
        NULL, NULL, hInstance, NULL
    );
    if (!g_hwnd) {
        fprintf(stderr, "ERROR: CreateWindowEx failed (err=%lu)\n", GetLastError());
        return 1;
    }
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    // Determine paths relative to exe directory
    char exe_dir[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
    // Find last backslash
    char* last_slash = strrchr(exe_dir, '\\');
    if (last_slash) *last_slash = '\0';

    char assets_path[MAX_PATH] = {0};
    char icu_path[MAX_PATH]    = {0};
    snprintf(assets_path, MAX_PATH, "%s\\data\\flutter_assets", exe_dir);
    snprintf(icu_path,    MAX_PATH, "%s\\data\\icudtl.dat",     exe_dir);

    // Start engine on separate thread
    static EngineArgs engine_args;  // must outlive the thread
    engine_args.assets_path   = assets_path;
    engine_args.icu_data_path = icu_path;

    g_engine_thread = CreateThread(
        NULL, 0, EngineThreadProc, &engine_args, 0, NULL
    );
    if (!g_engine_thread) {
        fprintf(stderr, "ERROR: CreateThread failed (err=%lu)\n", GetLastError());
        return 1;
    }

    // Run Win32 message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    DeleteCriticalSection(&g_buffer_lock);
    if (g_pixels) free(g_pixels);
    if (g_engine_dll) FreeLibrary(g_engine_dll);

    return (int)msg.wParam;
}
