#define UNICDOE
#include <windows.h>
#include <strsafe.h>
#define CLASS_NAME		TEXT("ColorFromPoint")
#define WM_MOUSEHOOK	WM_USER+321

LRESULT CALLBACK WndProc(HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lParam);

POINT GetWindowCenter(HWND hWnd);
BOOL SetWindowCenter(HWND hParent, HWND hWnd, LPRECT lpRect);
void GetRealDpi(HMONITOR hMonitor, float *XScale, float *YScale);
COLORREF GetAverageColor(HDC hdc, int x, int y, int rad);
bool IsColorDark(COLORREF color);
BOOL DrawBitmap(HDC hdc, int x, int y, HBITMAP hBitmap);
void ErrorMessage(LPCTSTR msg, ...);

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow){
	WNDCLASS wc = {
		CS_HREDRAW | CS_VREDRAW,
		WndProc,
		0,0,
		hInst,
		NULL, LoadCursor(NULL, IDC_ARROW),
		NULL,
		NULL,
		CLASS_NAME
	};
	RegisterClass(&wc);

	DWORD	dwStyle		= WS_OVERLAPPED,
			dwExStyle	= WS_EX_CLIENTEDGE;

	RECT crt;
	SetRect(&crt, 0,0, 500, 400);
	AdjustWindowRectEx(&crt, dwStyle, FALSE, dwExStyle);

	SetWindowCenter(NULL, NULL, &crt);

	HWND hWnd = CreateWindowEx(
			WS_EX_CLIENTEDGE | WS_EX_TOPMOST,
			CLASS_NAME,
			TEXT("MyUtility"),
			WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
			crt.left, crt.top, crt.right, crt.bottom,
			NULL,
			(HMENU)NULL,
			hInst,
			NULL
			);

	ShowWindow(hWnd, nCmdShow);

	MSG msg;
	while(GetMessage(&msg, nullptr, 0,0)){
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lParam){
	const char		*MyDll				= "MyMouseDll.dll",
		  *MyProc				= "MyMouseProc",
		  *MyUtil				= "Init";
	static HDC		g_hScreenDC			= NULL;
	static RECT		g_rcMagnify			= {0,};
	static HBITMAP	g_hBitmap			= NULL,
					g_hDrawBitmap		= NULL,
					g_hScreenBitmap		= NULL,
					g_hMagnifyCaptureBitmap = NULL;
	static HHOOK	g_hHook				= NULL;
	static HMODULE	g_hModule			= NULL;
	static HOOKPROC	g_lpfnHookProc		= NULL;
	static float	g_XScale			= 0,
					g_YScale			= 0;
	static int		g_iRate				= 2,
					g_X					= 0,
					g_Y					= 0;
	static BOOL		g_bMagnifyCaputre	= FALSE,
					g_bDown				= FALSE,
					g_bReset			= FALSE;
	static HMONITOR	g_hCurrentMonitor	= NULL;

	void (*pInit)(HWND, HHOOK) = NULL;

	RECT	crt;
	BITMAP	bmp;
	DWORD	dwStyle, dwExStyle;
	int		EditX, EditY, EditWidth, EditHeight;

	POINT		Mouse, Origin;
	COLORREF	color;
	TCHAR		ColorText[256];

	switch(iMessage){
		case WM_CREATE:
			// TODO: DLL 명시적 호출 및 프로시저 핸들 얻어오기
			try{
				g_hModule = LoadLibrary(MyDll);
				if(g_hModule == NULL){ throw 1; }

				g_lpfnHookProc = (HOOKPROC)GetProcAddress(g_hModule, MyProc);
				if(g_lpfnHookProc == NULL){ throw 2; }

				g_hHook = SetWindowsHookEx(WH_MOUSE_LL, g_lpfnHookProc,g_hModule, 0);
				if(g_hHook == NULL){ throw 3; }

				pInit = (void (*)(HWND, HHOOK))GetProcAddress(g_hModule, MyUtil);
				if(pInit == NULL){ throw 4; }
				(*pInit)(hWnd, g_hHook);

			} catch (const int err){
				ErrorMessage(TEXT("Init Failed"));
				if(err != 1){
					FreeLibrary(g_hModule);
				}
				return -1;
			}

			SetRect(&g_rcMagnify, 0,0, 100, 100);
			return 0;

		case WM_SIZE:
			if(wParam != SIZE_MINIMIZED){
				GetClientRect(hWnd, &crt);
				SetRect(&g_rcMagnify, 0,0, crt.right / 5, crt.bottom / 4);

				if(g_hScreenBitmap != NULL){
					DeleteObject(g_hScreenBitmap);
					g_hScreenBitmap = NULL;
				}

				if(g_hDrawBitmap != NULL){
					DeleteObject(g_hDrawBitmap);
					g_hDrawBitmap = NULL;
				}

				if(g_hBitmap != NULL){
					DeleteObject(g_hBitmap);
					g_hBitmap = NULL;
				}

			}
			return 0;

		case WM_GETMINMAXINFO:
			{
				LPMINMAXINFO lpmmi = (LPMINMAXINFO)lParam;

				SetRect(&crt, 0,0, 500, 400);
				dwStyle = GetWindowLongPtr(hWnd, GWL_STYLE);
				dwExStyle = GetWindowLongPtr(hWnd, GWL_EXSTYLE);
				AdjustWindowRectEx(&crt, dwStyle, GetMenu(hWnd) != NULL, dwExStyle);
				lpmmi->ptMinTrackSize.x = crt.right;
				lpmmi->ptMinTrackSize.y = crt.bottom;
			}
			return 0;

		case WM_PAINT:
			{
				PAINTSTRUCT ps;
				HDC hdc = BeginPaint(hWnd, &ps);
				HDC hMemDC = CreateCompatibleDC(hdc);
				GetClientRect(hWnd, &crt);
				if(g_hBitmap == NULL){
					g_hBitmap = CreateCompatibleBitmap(hdc, crt.right, crt.bottom);
				}
				HGDIOBJ hOld = SelectObject(hMemDC, g_hBitmap);
				FillRect(hMemDC, &crt, GetSysColorBrush(COLOR_BTNFACE));

				if(g_hScreenDC == NULL){ g_hScreenDC = GetDC(NULL); }
				if(1){
					HDC	hScreenMemDC		= CreateCompatibleDC(g_hScreenDC);
					if(g_hScreenBitmap == NULL){
						g_hScreenBitmap		= CreateCompatibleBitmap(g_hScreenDC, g_rcMagnify.right, g_rcMagnify.bottom);
					}
					HGDIOBJ hScreenOld		= SelectObject(hScreenMemDC, g_hScreenBitmap);

					GetObject(g_hScreenBitmap, sizeof(BITMAP), &bmp);
					BitBlt(
							hScreenMemDC,
							0, 0, bmp.bmWidth, bmp.bmHeight,
							g_hScreenDC,
							g_X - (bmp.bmWidth / (g_iRate * 2)), g_Y - (bmp.bmHeight / (g_iRate * 2)),
							SRCCOPY
						  );

					HDC hDrawMemDC		= CreateCompatibleDC(hdc);
					if(g_hDrawBitmap == NULL){
						g_hDrawBitmap	= CreateCompatibleBitmap(hdc, g_rcMagnify.right, g_rcMagnify.bottom);
					}
					HGDIOBJ hDrawOld	= SelectObject(hDrawMemDC, g_hDrawBitmap);

					GetObject(g_hDrawBitmap, sizeof(BITMAP), &bmp);
					SetStretchBltMode(hDrawMemDC, HALFTONE);
					StretchBlt(
							hDrawMemDC,
							0, 0, bmp.bmWidth, bmp.bmHeight,
							hScreenMemDC,
							0, 0, bmp.bmWidth / g_iRate, bmp.bmHeight / g_iRate,
							SRCCOPY
							);

					int	iWidth	= bmp.bmWidth,
						iHeight	= bmp.bmHeight,
						iRadius	= 5;

					Origin.x	= iWidth / 2;
					Origin.y	= iHeight / 2;

					color		= GetAverageColor(hDrawMemDC, Origin.x, Origin.y, iRadius);

					HPEN hPen;
					if(IsColorDark(color)){
						hPen = CreatePen(PS_SOLID, 1, RGB(255,255,255));
					}else{
						hPen = CreatePen(PS_SOLID, 1, RGB(0,0,0));
					}

					HPEN hOldPen		= (HPEN)SelectObject(hDrawMemDC, hPen);
					HBRUSH hOldBrush	= (HBRUSH)SelectObject(hDrawMemDC, (HBRUSH)GetStockObject(NULL_BRUSH));
					Ellipse(hDrawMemDC, Origin.x - iRadius, Origin.y - iRadius, Origin.x + iRadius, Origin.y + iRadius);
					SelectObject(hDrawMemDC, hOldBrush);
					SelectObject(hDrawMemDC, hOldPen);
					DeleteObject(hPen);

					SelectObject(hDrawMemDC, hDrawOld);
					SelectObject(hScreenMemDC, hScreenOld);
					DeleteDC(hDrawMemDC);
					DeleteDC(hScreenMemDC);
				}

				// Draw Objects
				if(g_hDrawBitmap != NULL){
					DrawBitmap(hMemDC, 10, 10, g_hDrawBitmap);
				}

				if(g_bDown){
					DrawBitmap(hMemDC, 20 + g_rcMagnify.right, 10, g_hMagnifyCaptureBitmap);
				}

				GetObject(g_hBitmap, sizeof(BITMAP), &bmp);
				BitBlt(hdc, 0,0, bmp.bmWidth, bmp.bmHeight, hMemDC, 0,0, SRCCOPY);
				SelectObject(hMemDC, hOld);
				DeleteDC(hMemDC);
				EndPaint(hWnd, &ps);
			}
			return 0;

		case WM_MOUSEHOOK:
			switch(wParam){
				case WM_LBUTTONDOWN:
					{
						/* TODO: 되긴 하는데 속도가 느려지므로 다른 방법 생각해볼 것 */
						g_bDown = TRUE;
						HDC hdc = GetDC(hWnd);
						HDC hMemDC = CreateCompatibleDC(hdc);
						HDC hTempDC = CreateCompatibleDC(hdc);
						GetObject(g_hScreenBitmap, sizeof(BITMAP), &bmp);
						if(g_hMagnifyCaptureBitmap == NULL){
							g_hMagnifyCaptureBitmap = CreateCompatibleBitmap(hdc, bmp.bmWidth, bmp.bmHeight);
						}
						HGDIOBJ hOld = SelectObject(hMemDC,	g_hMagnifyCaptureBitmap);
						HGDIOBJ hTempOld = SelectObject(hTempDC, g_hScreenBitmap);

						SetStretchBltMode(hMemDC, HALFTONE);
						StretchBlt(
							hMemDC,
							0, 0, bmp.bmWidth, bmp.bmHeight,
							hTempDC,
							0, 0, bmp.bmWidth / g_iRate, bmp.bmHeight / g_iRate,
							SRCCOPY
							);

						SelectObject(hTempDC, hTempOld);
						SelectObject(hMemDC, hOld);
						DeleteDC(hTempDC);
						DeleteDC(hMemDC);
						ReleaseDC(hWnd, hdc);
					}
					break;

				case WM_MOUSEMOVE:
					{
						GetCursorPos(&Mouse);
						HMONITOR hCurrentMonitor = MonitorFromPoint(Mouse, MONITOR_DEFAULTTONEAREST);
						if(g_hCurrentMonitor != hCurrentMonitor){
							g_hCurrentMonitor = hCurrentMonitor;
							GetRealDpi(g_hCurrentMonitor, &g_XScale, &g_YScale);
						}
						g_X = (int)(Mouse.x * g_XScale), g_Y = (int)(Mouse.y * g_YScale);
					}
					break;
			}
			InvalidateRect(hWnd, NULL, FALSE);
			return 0;

		case WM_DESTROY:
			if(g_hMagnifyCaptureBitmap){ DeleteObject(g_hMagnifyCaptureBitmap); }
			if(g_hScreenBitmap){ DeleteObject(g_hScreenBitmap); }
			if(g_hDrawBitmap){ DeleteObject(g_hDrawBitmap); }
			if(g_hBitmap){ DeleteObject(g_hBitmap); }
			if(g_hHook){ UnhookWindowsHookEx(g_hHook); }
			if(g_hModule){ FreeLibrary(g_hModule); }
			PostQuitMessage(0);
			return 0;
	}

	return (DefWindowProc(hWnd, iMessage, wParam, lParam));
}

POINT GetWindowCenter(HWND hWnd){
	RECT wrt;
	if(hWnd == NULL){ GetWindowRect(GetDesktopWindow(), &wrt); }
	else{ GetWindowRect(hWnd, &wrt); }

	int iWidth	= wrt.right - wrt.left;
	int iHeight	= wrt.bottom - wrt.top;

	iWidth /= 2;
	iHeight /=2;

	POINT Center = {iWidth, iHeight};

	return Center;
}

BOOL SetWindowCenter(HWND hParent, HWND hWnd, LPRECT lpRect){
	if(lpRect == NULL){ return FALSE; }
	if(hWnd != NULL){ GetWindowRect(hWnd, lpRect); }

	POINT Center = GetWindowCenter(hParent);

	int TargetWndWidth	= lpRect->right - lpRect->left;
	int TargetWndHeight = lpRect->bottom - lpRect->top;

	lpRect->left	= Center.x - (TargetWndWidth / 2);
	lpRect->top		= Center.y - (TargetWndHeight / 2);
	lpRect->right	= TargetWndWidth;
	lpRect->bottom	= TargetWndHeight;

	return TRUE;
}

void GetRealDpi(HMONITOR hMonitor, float *XScale, float *YScale){
	MONITORINFOEX Info = { sizeof(MONITORINFOEX) };
	GetMonitorInfo(hMonitor, &Info);

	DEVMODE DevMode = {.dmSize = sizeof(DEVMODE) };
	EnumDisplaySettings(Info.szDevice, ENUM_CURRENT_SETTINGS, &DevMode);

	RECT rt = Info.rcMonitor;

	float CurrentDpi = GetDpiForSystem() / USER_DEFAULT_SCREEN_DPI;
	*XScale = CurrentDpi / ((rt.right - rt.left) / (float)DevMode.dmPelsWidth);
	*YScale = CurrentDpi / ((rt.bottom - rt.top) / (float)DevMode.dmPelsHeight);
}

COLORREF GetAverageColor(HDC hdc, int x, int y, int rad){
	int	 r	= 0,
		 g	= 0,
		 b	= 0;

	int cnt = 0,
		SampleX[] = {x, x - rad, x + rad},
		SampleY[] = {y, y - rad, y + rad};

	COLORREF color;
	for (int i=0; i<3; i++){
		for (int j=0; j<3; j++){
			color = GetPixel(hdc, SampleX[i], SampleY[j]);
			r += GetRValue(color);
			g += GetGValue(color);
			b += GetBValue(color);
			++cnt;
		}
	}

	r /= cnt;
	g /= cnt;
	b /= cnt;

	return RGB(r, g, b);
}

// 0.5 미만 == 어두운 계열
bool IsColorDark(COLORREF color){
	int  r = GetRValue(color),
		 g = GetGValue(color),
		 b = GetBValue(color);

	// 가중 평균
	double brightness = (r * 0.299 + g * 0.587 + b * 0.114) / 255.0;

	return brightness < 0.56;
}

BOOL DrawBitmap(HDC hdc, int x, int y, HBITMAP hBitmap){
	if(hBitmap == NULL){return FALSE;}

	BITMAP	bmp;
	HDC		hMemDC = CreateCompatibleDC(hdc);
	GetObject(hBitmap, sizeof(BITMAP), &bmp);

	HGDIOBJ hOld = SelectObject(hMemDC, hBitmap);
	BitBlt(hdc, x, y, bmp.bmWidth, bmp.bmHeight, hMemDC, 0,0, SRCCOPY);

	SelectObject(hMemDC, hOld);
	DeleteDC(hMemDC);

	return TRUE;
}

void ErrorMessage(LPCTSTR msg, ...){
	LPVOID lpMsgBuf;
	DWORD dw = GetLastError(); 

	if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, dw, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &lpMsgBuf, 0, NULL) == 0) {
		MessageBox(HWND_DESKTOP, TEXT("DisplayText Error"), TEXT("Warning"), MB_OK);
	}

	TCHAR buf[0x1000];
	StringCbPrintf(buf, sizeof(buf), TEXT("[%s(%d)]%s"), msg, dw, lpMsgBuf);
	MessageBox(HWND_DESKTOP, (LPCTSTR)buf, TEXT("Error"), MB_ICONWARNING | MB_OK);
	LocalFree(lpMsgBuf);
}
