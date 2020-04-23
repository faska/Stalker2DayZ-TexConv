#include "stdafx.h"
#pragma comment(lib, "Dwmapi.lib")
#include "myNuklear.h"
extern "C"
{
struct IUnknown;
#include "nuklear_gdip.h"
}
#include "globals.h"


namespace
{
void my_gdip_clipboard_paste(nk_handle usr, struct nk_text_edit* edit)
{
    HGLOBAL mem;
    SIZE_T size;
    LPCWSTR wstr;
    int utf8size;
    char* utf8;
    (void)usr;

    if (!OpenClipboard(NULL)) //the only difference from nk_gdip_clipboard_paste is here
        return;

    mem = (HGLOBAL)GetClipboardData(CF_UNICODETEXT);
    if (!mem) {
        CloseClipboard();
        return;
    }

    size = GlobalSize(mem) - 1;
    if (!size) {
        CloseClipboard();
        return;
    }

    wstr = (LPCWSTR)GlobalLock(mem);
    if (!wstr) {
        CloseClipboard();
        return;
    }

    utf8size = WideCharToMultiByte(CP_UTF8, 0, wstr, (int)(size / sizeof(wchar_t)), NULL, 0, NULL, NULL);
    if (!utf8size) {
        GlobalUnlock(mem);
        CloseClipboard();
        return;
    }

    utf8 = (char*)malloc(utf8size);
    if (!utf8) {
        GlobalUnlock(mem);
        CloseClipboard();
        return;
    }

    WideCharToMultiByte(CP_UTF8, 0, wstr, (int)(size / sizeof(wchar_t)), utf8, utf8size, NULL, NULL);
    nk_textedit_paste(edit, utf8, utf8size);
    free(utf8);
    GlobalUnlock(mem);
    CloseClipboard();
}

void UpdateFrame(HWND wnd)
{
    RECT currentWndSIze;
    GetClientRect(wnd, &currentWndSIze);
    if (currentWndSIze.right != wndWidth || currentWndSIze.bottom != wndHeight)
    {
        SetWindowPos(GetActiveWindow(), HWND_TOP, -1, -1, wndWidth, wndHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED); //make the window wider

    }

    /* GUI */
    if (nk_begin(ctx, titleName, nk_rect(0, 0, wndWidth, wndHeight),
        NK_WINDOW_CLOSABLE | NK_WINDOW_MINIMIZABLE | NK_WINDOW_NO_SCROLLBAR))
    {
        void Frame(); //Front-end update
        Frame();
    }
    nk_end(ctx);

    /* Draw */
    {
        nk_gdip_render(NK_ANTI_ALIASING_ON, nk_rgba(0, 0, 0, 0));
    }
}



static LRESULT CALLBACK
WindowProc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
        case WM_SIZE:
            if (ctx)
            {
                ::UpdateFrame(wnd);
            }
            break;
        case WM_NCCALCSIZE:
                return 0; //it makes the app borderless, but also with minimizing animation
        case WM_NCHITTEST:
        {
            

            POINT currentCursorPosition;
            GetCursorPos(&currentCursorPosition);
            RECT headerZone;
            GetWindowRect(wnd, &headerZone);
            const long headerPaddingY = ctx->style.window.header.padding.y;
            headerZone.bottom         = headerZone.top + ctx->style.font->height + 2 * headerPaddingY + 2 * ctx->style.window.header.label_padding.y;

            if (PtInRect(&headerZone, currentCursorPosition))
            {//OK cursor is in header zone, we can drag the window, but lets check first it is not on 'x' or '_' buttons!
                const long headerPaddingX = ctx->style.window.header.padding.x;

                RECT closeButton;
                closeButton.top = headerZone.top + headerPaddingY;
                closeButton.bottom = headerZone.bottom - headerPaddingY;
                closeButton.right = headerZone.right - headerPaddingX;

                const long buttonSize = (closeButton.bottom - closeButton.top);
                closeButton.left = closeButton.right - buttonSize;

                if (!PtInRect(&closeButton, currentCursorPosition)) 
                {// 'x' is not pressed
                    RECT minimizeButton;
                    minimizeButton.top = closeButton.top;
                    minimizeButton.bottom = closeButton.bottom;

                    const long offsetX = headerPaddingX + buttonSize;
                    minimizeButton.right = closeButton.right - offsetX;
                    minimizeButton.left = closeButton.left - offsetX;
                    if (!PtInRect(&minimizeButton, currentCursorPosition)) 
                    {// '_' is not pressed too, so drag it!
                        return HTCAPTION;
                    }
                    else
                    {// '_' is pressed, so minimize the app
                        return HTMINBUTTON;
                    }    
                }
                else
                {// 'x' is pressed, so close the app
                    return HTCLOSE;
                }
            }
            return DefWindowProc(wnd, msg, wparam, lparam);
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_NCLBUTTONDOWN:
            if (wparam == HTCLOSE)
            {
                isRunning = false;
                return 0;
            }
            else if (wparam == HTMINBUTTON)
            {
                ShowWindow(wnd, SW_MINIMIZE);
                return 0;
            }
            break;
    }

    if (nk_gdip_handle_event(wnd, msg, wparam, lparam))
        return 0;

    return DefWindowProc(wnd, msg, wparam, lparam);
}
}

int __stdcall WinMain(HINSTANCE,HINSTANCE,LPSTR lpCmdLine,int)
{
    

    WNDCLASS wc;
    RECT rect = { 0, 0, wndWidth, wndHeight };
    DWORD style = WS_CAPTION | WS_MINIMIZEBOX;
    DWORD exstyle = WS_EX_LAYERED;
    HWND wnd;


    /* Win32 */
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(0);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "NuklearWindowClass";
    wc.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));
    RegisterClass(&wc);

    //AdjustWindowRectEx(&rect, style, FALSE, exstyle);

    wnd = CreateWindowEx(
        exstyle, 
        wc.lpszClassName, 
        wndName,
        style|WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, wc.hInstance, NULL);


    /* GUI */
    ctx               = nk_gdip_init(wnd,wndWidth, wndHeight);
    ctx->clip.paste   = my_gdip_clipboard_paste; //overloading Ctrl-V callback, because default callback has some weird bugs
    GdipFont* gdiFont = nk_gdipfont_create(font, fontSize);
    nk_gdip_set_font(gdiFont);
    

    /* style.c */
#ifdef INCLUDE_STYLE
/*set_style(ctx, THEME_WHITE);*/
/*set_style(ctx, THEME_RED);*/
/*set_style(ctx, THEME_BLUE);*/
/*set_style(ctx, THEME_DARK);*/
#endif
    //SetWindowPos(wnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    isRunning = true;

    {
        void OnStart();
        OnStart();
    }
    { //add shadow
        SetLayeredWindowAttributes(wnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
        MARGINS borderless = { 1 };
        DwmExtendFrameIntoClientArea(wnd, &borderless);
    }
    while (isRunning)
    {
        const auto FrameBeginTime = std::chrono::steady_clock::now();
        /* Input */
        MSG msg;
        nk_input_begin(ctx);
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                isRunning = 0;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        nk_input_end(ctx);
        ::UpdateFrame(wnd);
        std::this_thread::sleep_until(FrameBeginTime + std::chrono::milliseconds(33));
    }
    {
        void Finish();
        Finish();
    }
    nk_gdipfont_del(gdiFont);
    nk_gdip_shutdown();
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    return 0;
}