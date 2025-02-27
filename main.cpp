#define UNICDOE
#include <windows.h>
#include <strsafe.h>
#define CLASS_NAME		TEXT("ColorFromPoint")
#define WM_CHANGEFOCUS	WM_USER+1
#define WM_MOUSEHOOK	WM_USER+321
#define WM_KEYBOARDHOOK	WM_USER+123
#define min(a,b)		(((a) < (b)) ? (a) : (b))
#define max(a,b)		(((a) < (b)) ? (b) : (a))
#define abs(a)			(((a) < 0) ? -(a) : (a))

#define EDGEFRAME		2
#define IDC_EDSTART		1025
#define IDC_LBSTART		2049
#define IDM_PROGRAM		4097

LRESULT CALLBACK WndProc(HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK EditProc(HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lParam);

POINT GetWindowCenter(HWND hWnd);
BOOL SetWindowCenter(HWND hParent, HWND hWnd, LPRECT lpRect);
void GetRealDpi(HMONITOR hMonitor, float *XScale, float *YScale);
COLORREF GetAverageColor(HDC hdc, int x, int y, int rad);
bool IsColorDark(COLORREF color);
BOOL DrawBitmap(HDC hdc, int x, int y, HBITMAP hBitmap);
void ErrorMessage(LPCTSTR msg, ...);

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow){
	HANDLE hMutex;
	hMutex = CreateMutex(NULL, FALSE, TEXT("MyColorFromPointMutex"));

	if(GetLastError() == ERROR_ALREADY_EXISTS){
		CloseHandle(hMutex);
		HWND hOnce = FindWindow(CLASS_NAME, NULL);
		if(hOnce){
			ShowWindowAsync(hOnce, SW_SHOWNORMAL);
			SetForegroundWindow(hOnce);
		}
		return 0;
	}

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
			WS_EX_CLIENTEDGE,
			CLASS_NAME,
			CLASS_NAME,
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

typedef struct tag_MyRGB{
	float R;
	float G;
	float B;
}MyRGB;

typedef struct tag_MyCMY{
	float C;
	float M;
	float Y;
}MyCMY;

typedef struct tag_MyCMYK{
	float C;
	float M;
	float Y;
	float K;
}MyCMYK;

void ToHex(COLORREF color, LPTSTR ret, int Size);
void ToHex(int Value, LPTSTR ret, int Size);
COLORREF ToCOLORREF(LPCTSTR HexCode);
MyRGB Normalize(COLORREF color);
MyRGB Normalize(int r, int g, int b);
float MyGetKValue(MyRGB rgb);
MyCMY GetCMY(MyRGB rgb, float K);
MyCMYK ToCMYK(COLORREF color);
MyCMYK ToCMYK(int r, int g, int b);
MyRGB ToRGB(MyCMYK cmyk);
COLORREF ToCOLORREF(MyCMYK cmyk);
HBRUSH CreateCMYKBrush(MyCMYK cmyk);
void ToHex(MyCMYK cmyk, LPTSTR ret, int Size);

LRESULT CALLBACK WndProc(HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lParam){
	const char		*MyDll				= "MyApiDll.dll",
					*MyMouseProc		= "MyMouseProc",
					*MyKeyboardProc		= "MyKeyboardProc",
					*MyUtil				= "MyInit";
	static HDC		g_hScreenDC			= NULL;
	static RECT		g_rcMagnify			= {0,},
					g_rcRed				= {0,},
					g_rcGreen			= {0,},
					g_rcBlue			= {0,},
					g_rcBlack			= {0,};
	static HDC		g_hMemDC			= NULL,
					g_hDrawMemDC		= NULL,
					g_hScreenMemDC		= NULL,
					g_hCaptureMemDC		= NULL;
	static HBITMAP	g_hBitmap			= NULL,
					g_hDrawBitmap		= NULL,
					g_hScreenBitmap		= NULL,
					g_hMagnifyCaptureBitmap = NULL;
	static HHOOK	g_hMouse			= NULL,
					g_hKeyboard			= NULL;
	static HMODULE	g_hModule			= NULL;
	static HOOKPROC	g_lpfnMouseProc		= NULL,
					g_lpfnKeyboardProc	= NULL;
	static float	g_Rate				= 2.0,
					g_XScale			= 1.0,
					g_YScale			= 1.0;
	static int		g_X					= 0,
					g_Y					= 0,
					g_iRadius			= 0;
	static HMONITOR	g_hCurrentMonitor	= NULL;

	static const int	nEdit			= 10,
						nList			= 1,
						nControls		= nList + nEdit,
						Padding			= 20;

	static HWND		hControls[nControls];
	static WNDPROC	OldEditProc;

	static HBRUSH hRedBrush, hGreenBrush, hBlueBrush, hBlackBrush;
	static COLORREF SelectColor, EllipseColor;
	static POINT Mouse, EllipseOrigin;
	static HPEN hWhitePen, hBlackPen;

	void (*pInit)(HWND, HHOOK, HHOOK)	= NULL;

	RECT	crt, wrt, srt;
	BITMAP	bmp;
	DWORD	dwStyle, dwExStyle;

	POINT		Origin;
	COLORREF	color;
	TCHAR		HexCode[6];
	HMONITOR	hCurrentMonitor;
	int x, y, Width, iWidth, Height, iHeight, iRadius;

	WNDCLASS wc;
	HDC hdc;

	HMENU hMenu, hPopupMenu;

	switch(iMessage){
		case WM_CREATE:
			try{
				g_hModule = LoadLibrary(MyDll);
				if(g_hModule == NULL){ throw 1; }

				g_lpfnMouseProc = (HOOKPROC)GetProcAddress(g_hModule, MyMouseProc);
				if(g_lpfnMouseProc == NULL){ throw 2; }

				g_hMouse = SetWindowsHookEx(WH_MOUSE_LL, g_lpfnMouseProc,g_hModule, 0);
				if(g_hMouse == NULL){ throw 3; }

				g_lpfnKeyboardProc = (HOOKPROC)GetProcAddress(g_hModule, MyKeyboardProc);
				if(g_lpfnKeyboardProc == NULL){ throw 4; }

				g_hKeyboard = SetWindowsHookEx(WH_KEYBOARD_LL, g_lpfnKeyboardProc,g_hModule, 0);
				if(g_hKeyboard == NULL){ throw 5; }

				pInit = (void (*)(HWND, HHOOK, HHOOK))GetProcAddress(g_hModule, MyUtil);
				if(pInit == NULL){ throw 6; }
				(*pInit)(hWnd, g_hMouse, g_hKeyboard);

			} catch (const int err){
				ErrorMessage(TEXT("Init Failed"));
				if(err != 1){
					FreeLibrary(g_hModule);
				}
				return -1;
			}

			SetRect(&g_rcMagnify, 0,0, 100, 100);

			color = ToCOLORREF(TEXT("#c92519"));
			hRedBrush = CreateSolidBrush(color);
			color = ToCOLORREF(TEXT("#00A86B"));
			hGreenBrush = CreateSolidBrush(color);
			color = ToCOLORREF(TEXT("#0080ff"));
			hBlueBrush = CreateSolidBrush(color);
			hBlackBrush = CreateSolidBrush(RGB(54, 69, 79));

			GetClassInfo(NULL, TEXT("edit"), &wc);
			wc.hInstance		= GetModuleHandle(NULL);
			wc.lpszClassName	= TEXT("MyEditClass");
			OldEditProc			= wc.lpfnWndProc;
			wc.lpfnWndProc		= (WNDPROC)EditProc;
			RegisterClass(&wc);
			SetProp(hWnd, TEXT("MyEditClassProc"), (HANDLE)OldEditProc);
			for(int i=0; i<nEdit; i++){
				hControls[i] = CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("MyEditClass"), TEXT(""), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_RIGHT | ES_READONLY, 0,0,0,0, hWnd, (HMENU)(INT_PTR)(IDC_EDSTART + i), GetModuleHandle(NULL), NULL);
			}

			hControls[nControls - 1]= CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("listbox"), NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED, 0,0,0,0, hWnd, (HMENU)IDC_LBSTART, GetModuleHandle(NULL), NULL);

			g_hScreenDC = GetDC(NULL);
			g_hScreenMemDC = CreateCompatibleDC(g_hScreenDC);
			hdc = GetDC(hWnd);
			g_hMemDC = CreateCompatibleDC(hdc);

			hWhitePen = CreatePen(PS_SOLID, 1, RGB(255,255,255));
			hBlackPen = CreatePen(PS_SOLID, 1, RGB(0,0,0));
			ReleaseDC(hWnd, hdc);

			hMenu                   = CreateMenu();
			hPopupMenu              = CreatePopupMenu();
			AppendMenu(hMenu, MF_STRING | MF_POPUP, (UINT_PTR)hPopupMenu, TEXT("메뉴(&Menu)"));
			AppendMenu(hPopupMenu, MF_STRING, IDM_PROGRAM, TEXT("프로그램 소개"));
			SetMenu(hWnd, hMenu);

			SetTimer(hWnd, 1, 10, NULL);
			return 0;

		case WM_SIZE:
			if(wParam != SIZE_MINIMIZED){
				GetClientRect(hWnd, &crt);
				SetRect(&g_rcMagnify, 0,0, crt.right / 5, crt.bottom / 4);

				Width = Height = 24;
				x = Padding * 3  + g_rcMagnify.right * 2;

				y = Padding;
				SetRect(&g_rcRed, x, y, x + Width, y + Height);
				y = Padding + (g_rcMagnify.bottom - Height) / 2;
				SetRect(&g_rcGreen, x, y, x + Width, y + Height);
				y = Padding + g_rcMagnify.bottom - Height;
				SetRect(&g_rcBlue, x, y, x + Width, y + Height);
				int Gap = (g_rcMagnify.bottom - (Height * 3)) / 2;
				y = Padding + g_rcMagnify.bottom + Gap;
				SetRect(&g_rcBlack, x, y, x + Width, y + Height);

				x += Padding * 2;
				Width = (crt.right - x - (Padding * 4))/3;
				SetRect(&srt, x, y, Width, Height);
				SetWindowPos(hControls[nEdit - 1], NULL, x, y, Width, Height, SWP_NOZORDER);

				iWidth = (LOWORD(lParam) - (Padding * 2 + g_rcMagnify.right)) / 2;
				iHeight = (HIWORD(lParam) - (y + Height + Padding)) / 2;
				g_iRadius = min(iWidth, iHeight) - Padding;
				EllipseOrigin.x = LOWORD(lParam) - iWidth;
				EllipseOrigin.y = HIWORD(lParam) - iHeight;

				for(int i=0; i<3; i++){
					y = Padding;
					SetRect(&srt, x, y, Width, Height);
					SetWindowPos(hControls[0 + (i*3)], NULL, x, y, Width, Height, SWP_NOZORDER);

					y = Padding + (g_rcMagnify.bottom - Height) / 2;
					SetRect(&srt, x, y, Width, Height);
					SetWindowPos(hControls[1 + (i*3)], NULL, x, y, Width, Height, SWP_NOZORDER);

					y = Padding + g_rcMagnify.bottom - Height;
					SetRect(&srt, x, y, Width, Height);
					SetWindowPos(hControls[2 + (i*3)], NULL, x, y, Width, Height, SWP_NOZORDER);

					x += Width + Padding;
				}

				x = Padding;
				y = Padding * 2 + g_rcMagnify.bottom;
				Width = g_rcMagnify.right;
				Height = HIWORD(lParam) - y - Padding;
				SetWindowPos(hControls[nControls-1], NULL, x, y, Width, Height, SWP_NOZORDER);

				if(g_hMagnifyCaptureBitmap != NULL){
					DeleteObject(g_hMagnifyCaptureBitmap);
					g_hMagnifyCaptureBitmap = NULL;
				}

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

		case WM_SETFOCUS:
			SetFocus(hControls[0]);
			return 0;

		case WM_GETMINMAXINFO:
			{
				LPMINMAXINFO lpmmi = (LPMINMAXINFO)lParam;

				SetRect(&crt, 0,0, 550, 420);
				dwStyle = GetWindowLongPtr(hWnd, GWL_STYLE);
				dwExStyle = GetWindowLongPtr(hWnd, GWL_EXSTYLE);
				AdjustWindowRectEx(&crt, dwStyle, GetMenu(hWnd) != NULL, dwExStyle);
				lpmmi->ptMinTrackSize.x = crt.right;
				lpmmi->ptMinTrackSize.y = crt.bottom;
			}
			return 0;

		case WM_MEASUREITEM:
			{
				LPMEASUREITEMSTRUCT lpmis = (LPMEASUREITEMSTRUCT)lParam;
				lpmis->itemHeight = 16;
			}
			return TRUE;

		case WM_DRAWITEM:
			{
				HBRUSH hBrush, hColorBrush, hColorOldBrush;

				LPDRAWITEMSTRUCT lpdis = (LPDRAWITEMSTRUCT)lParam;
				if(lpdis->itemState & ODS_SELECTED){
					hBrush = GetSysColorBrush(COLOR_HIGHLIGHT);
				}else{
					hBrush = GetSysColorBrush(COLOR_BTNFACE);
				}

				FillRect(lpdis->hDC, &lpdis->rcItem, hBrush);

				color = (COLORREF)lpdis->itemData;
				hColorBrush = CreateSolidBrush(color);
				hColorOldBrush = (HBRUSH)SelectObject(lpdis->hDC, hColorBrush);

				Rectangle(lpdis->hDC, lpdis->rcItem.left + 5, lpdis->rcItem.top + 2, lpdis->rcItem.right - 5, lpdis->rcItem.bottom - 2);
				SelectObject(lpdis->hDC, hColorOldBrush);
				DeleteObject(hColorBrush);
			}
			return TRUE;

		case WM_COMMAND:
			switch(LOWORD(wParam)){
				case IDC_LBSTART:
					switch(HIWORD(wParam)){
						case LBN_SELCHANGE:
							{
								int idx	= SendMessage(hControls[nControls - 1], LB_GETCURSEL, 0,0);
								EllipseColor = (COLORREF)SendMessage(hControls[nControls - 1], LB_GETITEMDATA, idx, 0);
								InvalidateRect(hWnd, NULL, FALSE);
							}
							break;
					}
					break;

				case IDM_PROGRAM:
					MessageBox(hWnd, TEXT("프로그램 소개\r\n\r\n위 프로그램은 색상값 조사에 사용되는 도구입니다.\r\n마우스 커서가 위치한 지점에서 일정한 크기의 영역을 조사하여 이미지 정보를 가져옵니다.\r\n주로, 이미지에 대한 색상 정보를 조사할 때 사용되며 추후 버전이 업데이트 되어 기능이 추가될 수 있습니다.\r\n\r\n단축키\r\n1. Ctrl + Alt + 3 : 마우스 주변 영역을 캡처합니다. \r\n2. Ctrl + Alt + 4 : 마우스 커서가 위치한 지점의 색상값을 CMYK, RGB, HEX로 변환합니다.\r\n3. Alt + 마우스 휠 : 이미지를 확대하거나 축소할 수 있습니다.\r\n\r\n※ 참고\r\n색상값을 변환할 때 최근 변환한 색상을 리스트에 기록합니다.\r\n이는 저장되지 않으며, 색상을 선택하면 이미지에 색상을 적용하여 보여줍니다."), TEXT("ColorFromPoint"), MB_OK);
					break;
			}
			return 0;

		case WM_TIMER:
			switch(wParam){
				case 1:
					{
						GetClientRect(hWnd, &crt);
						hdc = GetDC(hWnd);
						if(g_hMemDC == NULL){
							g_hMemDC = CreateCompatibleDC(hdc);
						}

						if(g_hBitmap == NULL){
							g_hBitmap = CreateCompatibleBitmap(hdc, crt.right, crt.bottom);
						}

						HGDIOBJ hOld = SelectObject(g_hMemDC, g_hBitmap);
						FillRect(g_hMemDC, &crt, GetSysColorBrush(COLOR_BTNFACE));

						if(g_hScreenDC == NULL){
							g_hScreenDC = GetDC(NULL);
						}

						if(g_hScreenMemDC == NULL){
							g_hScreenMemDC = CreateCompatibleDC(g_hScreenDC);
						}

						if(g_hScreenBitmap == NULL){
							g_hScreenBitmap = CreateCompatibleBitmap(g_hScreenDC, g_rcMagnify.right, g_rcMagnify.bottom);
						}

						HGDIOBJ hScreenOld = SelectObject(g_hScreenMemDC, g_hScreenBitmap);
						GetObject(g_hScreenBitmap, sizeof(BITMAP), &bmp);
						BitBlt(
								g_hScreenMemDC,
								0, 0, bmp.bmWidth * g_XScale, bmp.bmHeight * g_YScale,
								g_hScreenDC,
								g_X - (bmp.bmWidth / g_Rate / 2), g_Y - (bmp.bmHeight / g_Rate / 2),
								SRCCOPY
							  );

						if(g_hDrawMemDC == NULL){
							g_hDrawMemDC = CreateCompatibleDC(hdc);
						}

						if(g_hDrawBitmap == NULL){
							g_hDrawBitmap = CreateCompatibleBitmap(hdc, g_rcMagnify.right, g_rcMagnify.bottom);
						}

						HGDIOBJ hDrawOld = SelectObject(g_hDrawMemDC, g_hDrawBitmap);
						GetObject(g_hDrawBitmap, sizeof(BITMAP), &bmp);
						SetStretchBltMode(g_hDrawMemDC, HALFTONE);
						StretchBlt(
								g_hDrawMemDC,
								0, 0, bmp.bmWidth * g_XScale, bmp.bmHeight * g_YScale,
								g_hScreenMemDC,
								0, 0, (bmp.bmWidth / g_Rate) * g_XScale, (bmp.bmHeight / g_Rate) * g_YScale,
								SRCCOPY
								);

						iWidth	= bmp.bmWidth;
						iHeight	= bmp.bmHeight;
						iRadius	= 3;

						Origin.x = iWidth / 2;
						Origin.y = iHeight / 2;

						color = GetAverageColor(g_hDrawMemDC, Origin.x, Origin.y, iRadius);

						HPEN hOldPen;
						if(IsColorDark(color)){
							hOldPen	= (HPEN)SelectObject(g_hDrawMemDC, hWhitePen);
						}else{
							hOldPen	= (HPEN)SelectObject(g_hDrawMemDC, hBlackPen);
						}
						HBRUSH hOldBrush = (HBRUSH)SelectObject(g_hDrawMemDC, (HBRUSH)GetStockObject(NULL_BRUSH));
						Ellipse(g_hDrawMemDC, Origin.x - iRadius, Origin.y - iRadius, Origin.x + iRadius, Origin.y + iRadius);
						SelectObject(g_hDrawMemDC, hOldBrush);
						SelectObject(g_hDrawMemDC, hOldPen);

						SelectColor = GetPixel(g_hDrawMemDC, Origin.x, Origin.y);

						SelectObject(g_hDrawMemDC, hDrawOld);
						SelectObject(g_hScreenMemDC, hScreenOld);

						SetRect(&srt, Padding, Padding, Padding + g_rcMagnify.right, Padding + g_rcMagnify.bottom);
						InflateRect(&srt, EDGEFRAME, EDGEFRAME);
						DrawEdge(g_hMemDC, &srt, EDGE_SUNKEN, BF_RECT);

						InflateRect(&srt, -EDGEFRAME, -EDGEFRAME);
						if(g_hDrawBitmap != NULL){
							DrawBitmap(g_hMemDC, srt.left, srt.top, g_hDrawBitmap);
						}

						SetRect(&srt, Padding + srt.right, srt.top, Padding + srt.right + g_rcMagnify.right, srt.bottom);
						InflateRect(&srt, EDGEFRAME, EDGEFRAME);
						DrawEdge(g_hMemDC, &srt, EDGE_SUNKEN, BF_RECT);

						InflateRect(&srt, -EDGEFRAME, -EDGEFRAME);
						if(g_hMagnifyCaptureBitmap != NULL){
							DrawBitmap(g_hMemDC, srt.left, srt.top, g_hMagnifyCaptureBitmap);
						}

						DrawEdge(g_hMemDC, &g_rcRed, EDGE_SUNKEN, BF_RECT);
						DrawEdge(g_hMemDC, &g_rcGreen, EDGE_SUNKEN, BF_RECT);
						DrawEdge(g_hMemDC, &g_rcBlue, EDGE_SUNKEN, BF_RECT);
						DrawEdge(g_hMemDC, &g_rcBlack, EDGE_SUNKEN, BF_RECT);

						CopyRect(&srt, &g_rcRed);
						InflateRect(&srt, -EDGEFRAME, -EDGEFRAME);
						FillRect(g_hMemDC, &srt, hRedBrush);

						CopyRect(&srt, &g_rcGreen);
						InflateRect(&srt, -EDGEFRAME, -EDGEFRAME);
						FillRect(g_hMemDC, &srt, hGreenBrush);

						CopyRect(&srt, &g_rcBlue);
						InflateRect(&srt, -EDGEFRAME, -EDGEFRAME);
						FillRect(g_hMemDC, &srt, hBlueBrush);

						CopyRect(&srt, &g_rcBlack);
						InflateRect(&srt, -EDGEFRAME, -EDGEFRAME);
						FillRect(g_hMemDC, &srt, hBlackBrush);

						HBRUSH hEllipseBrush = CreateSolidBrush(EllipseColor),
							   hEllipseOldBrush	= (HBRUSH)SelectObject(g_hMemDC, hEllipseBrush);
						Ellipse(g_hMemDC, EllipseOrigin.x - g_iRadius, EllipseOrigin.y - g_iRadius, EllipseOrigin.x + g_iRadius, EllipseOrigin.y + g_iRadius);
						SelectObject(g_hMemDC, hEllipseOldBrush);
						DeleteObject(hEllipseBrush);

						GetObject(g_hBitmap, sizeof(BITMAP), &bmp);
						BitBlt(hdc, 0,0, bmp.bmWidth, bmp.bmHeight, g_hMemDC, 0,0, SRCCOPY);
						SelectObject(g_hMemDC, hOld);
						ReleaseDC(hWnd, hdc);
					}
					break;
			}
			return 0;
		case WM_PAINT:
			{
				PAINTSTRUCT ps;
				hdc = BeginPaint(hWnd, &ps);
				EndPaint(hWnd, &ps);
			}
			return 0;

		case WM_KEYBOARDHOOK:
			{
				KBDLLHOOKSTRUCT *ptr = (KBDLLHOOKSTRUCT*)lParam;

				switch(wParam){
					case WM_KEYUP:
					case WM_KEYDOWN:
						{
							WORD VKCode = ptr->vkCode,
								 KeyFlags = ptr->flags,
								 ScanCode = ptr->scanCode;

							BOOL bExtended,
								 bWasKeyDown,
								 bKeyReleased;

							// 확장 키(Numpad 등) 플래그 있을 시 0xE0이 접두(HIWORD)로 붙는다
							bExtended	= ((KeyFlags&& LLKHF_EXTENDED) == LLKHF_EXTENDED);
							if(bExtended){ ScanCode = MAKEWORD(ScanCode, 0xE0); }
							bWasKeyDown	= !(KeyFlags & LLKHF_UP);

							if(bWasKeyDown){
								switch(VKCode){
									case 0x33:
										if(GetKeyState(VK_CONTROL) & 0x8000 && GetKeyState(VK_MENU) & 0x8000){
											if(g_hMagnifyCaptureBitmap != NULL){
												DeleteObject(g_hMagnifyCaptureBitmap);
												g_hMagnifyCaptureBitmap = NULL;
											}

											hdc = GetDC(hWnd);
											if(g_hCaptureMemDC == NULL){
												g_hCaptureMemDC = CreateCompatibleDC(hdc);
											}

											GetObject(g_hScreenBitmap, sizeof(BITMAP), &bmp);
											g_hMagnifyCaptureBitmap = CreateCompatibleBitmap(hdc, bmp.bmWidth, bmp.bmHeight);
											HGDIOBJ hOld = SelectObject(g_hMemDC, g_hScreenBitmap);
											HGDIOBJ hTempOld = SelectObject(g_hCaptureMemDC, g_hMagnifyCaptureBitmap);

											SetStretchBltMode(g_hCaptureMemDC, HALFTONE);
											StretchBlt(
													g_hCaptureMemDC,
													0, 0, bmp.bmWidth * g_XScale, bmp.bmHeight * g_YScale,
													g_hMemDC,
													0, 0, (bmp.bmWidth / g_Rate) * g_XScale, (bmp.bmHeight / g_Rate) * g_YScale,
													SRCCOPY
													);

											SelectObject(g_hCaptureMemDC, hTempOld);
											SelectObject(g_hMemDC, hOld);
											ReleaseDC(hWnd, hdc);
										}
										break;

									case 0x34:
										if(GetKeyState(VK_CONTROL) & 0x8000 && GetKeyState(VK_MENU) & 0x8000){
											if(g_hDrawBitmap){
												int r = GetRValue(SelectColor),
													g = GetGValue(SelectColor),
													b = GetBValue(SelectColor);

												memset(HexCode, 0, sizeof(HexCode));
												MyCMYK cmyk = ToCMYK(r,g,b);
												StringCbPrintf(HexCode, sizeof(HexCode), TEXT("%.2f"), cmyk.C);
												SetDlgItemText(hWnd, IDC_EDSTART, HexCode);

												StringCbPrintf(HexCode, sizeof(HexCode), TEXT("%.2f"), cmyk.M);
												SetDlgItemText(hWnd, IDC_EDSTART+1, HexCode);

												StringCbPrintf(HexCode, sizeof(HexCode), TEXT("%.2f"), cmyk.Y);
												SetDlgItemText(hWnd, IDC_EDSTART+2, HexCode);

												StringCbPrintf(HexCode, sizeof(HexCode), TEXT("%.2f"), cmyk.K);
												SetDlgItemText(hWnd, IDC_EDSTART+9, HexCode);
												
												SetDlgItemInt(hWnd, IDC_EDSTART+3, r, FALSE);
												SetDlgItemInt(hWnd, IDC_EDSTART+4, g, FALSE);
												SetDlgItemInt(hWnd, IDC_EDSTART+5, b, FALSE);

												ToHex(r, HexCode, sizeof(HexCode));
												SetDlgItemText(hWnd, IDC_EDSTART+6, HexCode); 
												ToHex(g, HexCode, sizeof(HexCode));
												SetDlgItemText(hWnd, IDC_EDSTART+7, HexCode); 
												ToHex(b, HexCode, sizeof(HexCode));
												SetDlgItemText(hWnd, IDC_EDSTART+8, HexCode); 

												SendMessage(hControls[nControls-1], LB_INSERTSTRING, 0, (LPARAM)SelectColor);
											}
										}
										break;

									default:
										break;
								}
							}
						}
						break;
				}
			}
			InvalidateRect(hWnd, NULL, FALSE);
			return 0;

		case WM_MOUSEHOOK:
			{
				MSLLHOOKSTRUCT*ptr = (MSLLHOOKSTRUCT*)lParam;
				Mouse.x = g_X = (int)(ptr->pt.x);
				Mouse.y = g_Y = (int)(ptr->pt.y);

				SHORT WheelDelta,
					  XButton;

				int	Lines		= 0,
					nScroll		= 0,
					WheelUnit	= 0;
				static int	SumDelta	= 0;

				switch(wParam){
					case WM_MOUSEMOVE:
						hCurrentMonitor = MonitorFromPoint(Mouse, MONITOR_DEFAULTTONEAREST);

						if(g_hCurrentMonitor != hCurrentMonitor){
							g_hCurrentMonitor = hCurrentMonitor;
							GetRealDpi(g_hCurrentMonitor, &g_XScale, &g_YScale);
						}
						break;

					case WM_MOUSEWHEEL:
						if(GetKeyState(VK_MENU) & 0x8000){
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

							nScroll			= 0;
							WheelDelta		= HIWORD(ptr->mouseData);

							SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &Lines, 0);
							// WHEEL_DELTA(120)
							WheelUnit		= WHEEL_DELTA / Lines;

							SumDelta		+= WheelDelta;
							nScroll			= SumDelta / WheelUnit;

							// 부호 상관없이 나머지 계산
							SumDelta		%= WheelUnit;

							int steps		= abs(nScroll);
							float factor	= 0.1f;
							if(nScroll > 0){
								g_Rate = max(1.f, min(5.f, g_Rate + factor * steps));
							}else{
								g_Rate = max(1.f, min(5.f, g_Rate - factor * steps));
							}
						}
						break;

					case WM_XBUTTONDOWN:
					case WM_XBUTTONUP:
						XButton = HIWORD(ptr->mouseData);
						if(XButton == XBUTTON1){

						}

						if(XButton == XBUTTON2){

						}
						break;

					default:
						break;
				}
			}
			InvalidateRect(hWnd, NULL, FALSE);
			return 0;

		case WM_CHANGEFOCUS:
			{
				HWND hPrevFocus		= (HWND)lParam;
				WPARAM KeyCode		= wParam;

				int	Next,
					Half = nEdit / 2,
					adjusted = nEdit - 1;
					
				for(int i=0; i<nEdit; i++){
					if(hControls[i] == hPrevFocus){
						if(KeyCode == 0 || KeyCode == 2){
							if(i == 3){
								Next = 9;
							}else if(i == 9){
								Next = 2;
							}else{
								Next = (i - 1 + adjusted) % adjusted;
							}
						}else{
							if(i == 2){
								Next = 9;
							}else if(i == 9){
								Next = 3;
							}else{
								Next = (i + 1) % adjusted;
							}
						}

						SetFocus(hControls[Next]);
					}
				}
			}
			return 0;

		case WM_DESTROY:
			KillTimer(hWnd, 1);
			if(g_hMagnifyCaptureBitmap){ DeleteObject(g_hMagnifyCaptureBitmap); }
			if(g_hScreenBitmap){ DeleteObject(g_hScreenBitmap); }
			if(g_hDrawBitmap){ DeleteObject(g_hDrawBitmap); }
			if(g_hBitmap){ DeleteObject(g_hBitmap); }
			if(g_hCaptureMemDC){ DeleteDC(g_hCaptureMemDC); }
			if(g_hScreenMemDC){ DeleteDC(g_hScreenMemDC); }
			if(g_hDrawMemDC){ DeleteDC(g_hDrawMemDC); }
			if(g_hMemDC){ DeleteDC(g_hMemDC); }
			if(g_hMouse){ UnhookWindowsHookEx(g_hMouse); }
			if(g_hKeyboard){ UnhookWindowsHookEx(g_hKeyboard); }
			if(g_hModule){ FreeLibrary(g_hModule); }
			if(OldEditProc){
				for(int i=0; i<nEdit; i++){
					SetClassLongPtr(hControls[i], GCLP_WNDPROC, (LONG_PTR)OldEditProc);
				}
			}
			if(GetProp(hWnd, TEXT("MyEditClassProc")) != NULL){
				RemoveProp(hWnd, TEXT("MyEditClassProc"));
			}
			if(hRedBrush){ DeleteObject(hRedBrush); }
			if(hGreenBrush){ DeleteObject(hGreenBrush); }
			if(hBlueBrush){ DeleteObject(hBlueBrush); }
			if(hBlackBrush){ DeleteObject(hBlackBrush); }
			if(hWhitePen){ DeleteObject(hWhitePen); }
			if(hBlackPen){ DeleteObject(hBlackPen); }
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
	int	 r  = 0,
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
	double brightness = (r * 0.299f + g * 0.587f + b * 0.114f) / 255.f;

	return brightness < 0.56f;
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

	TCHAR buf[256];
	StringCbPrintf(buf, sizeof(buf), TEXT("[%s(%d)]%s"), msg, dw, lpMsgBuf);
	MessageBox(HWND_DESKTOP, (LPCTSTR)buf, TEXT("Error"), MB_ICONWARNING | MB_OK);
	LocalFree(lpMsgBuf);
}

void ToHex(COLORREF color, LPTSTR ret, int Size){
	StringCbPrintf(ret, Size, TEXT("%02X%02X%02X"), GetRValue(color), GetGValue(color), GetBValue(color));
}

void ToHex(int Value, LPTSTR ret, int Size){
	StringCbPrintf(ret, Size, TEXT("%02X"), Value);
}

COLORREF ToCOLORREF(LPCTSTR HexCode){
	TCHAR* ptr = (TCHAR*)HexCode;
	if(ptr[0] == '#'){ ptr++; }

	int i = 0,
		r = 0,
		g = 0,
		b = 0,
		Value = 0;

	for(ptr; *ptr && i<6; ptr++){
		if(*ptr >= '0' && *ptr <= '9'){
			Value = *ptr -'0';
		}
		if(*ptr >= 'A' && *ptr <= 'F'){
			Value = *ptr -'A' + 10;
		}
		if(*ptr >= 'a' && *ptr <= 'f'){
			Value = *ptr -'a' + 10;
		}

		if(i < 2){
			r = (r << 4) | Value;
		}else if(i <4){
			g = (g << 4) | Value;
		}else{
			b = (b << 4) | Value;
		}

		i++;
	}

	return RGB(r, g, b);
}

MyRGB Normalize(COLORREF color){
	// 0 ~ 1 : Normalization

	MyRGB rgb;
	rgb.R = GetRValue(color) / 255.f;
	rgb.G = GetGValue(color) / 255.f;
	rgb.B = GetBValue(color) / 255.f;
	return rgb;
}

MyRGB Normalize(int r, int g, int b){
	// 0 ~ 1 : Normalization

	MyRGB rgb;
	rgb.R = r / 255.f;
	rgb.G = g / 255.f;
	rgb.B = b / 255.f;
	return rgb;
}

float MyGetKValue(MyRGB rgb) {
	// K = 1 - max(R',G',B')

	float K = 1.f - max(rgb.R, max(rgb.G, rgb.B));
	return K;
}

MyCMY GetCMY(MyRGB rgb, float K) {
	// C = (1 - R' - K) / (1 - K)
	// M = (1 - G' - K) / (1 - K)
	// Y = (1 - B' - K) / (1 - K)

	MyCMY cmy = {0,};
	if (K < 1.f) {
		cmy.C = (1.f - rgb.R - K) / (1.f - K);
		cmy.M = (1.f - rgb.G - K) / (1.f - K);
		cmy.Y = (1.f - rgb.B - K) / (1.f - K);
	}

	return cmy;
}

MyCMYK ToCMYK(COLORREF color){
	MyRGB rgb = Normalize(color);
	float K = MyGetKValue(rgb);
	MyCMY cmy = GetCMY(rgb, K);

	MyCMYK cmyk;
	cmyk.C = cmy.C * 100.f;
	cmyk.M = cmy.M * 100.f;
	cmyk.Y = cmy.Y * 100.f;
	cmyk.K = K * 100.f;

	return cmyk;
}

MyCMYK ToCMYK(int r, int g, int b){
	MyRGB rgb = Normalize(r,g,b);
	float K = MyGetKValue(rgb);
	MyCMY cmy = GetCMY(rgb, K);

	MyCMYK cmyk;
	cmyk.C = cmy.C * 100.f;
	cmyk.M = cmy.M * 100.f;
	cmyk.Y = cmy.Y * 100.f;
	cmyk.K = K * 100.f;

	return cmyk;
}

MyRGB ToRGB(MyCMYK cmyk){
	MyRGB rgb;

	// C' = C / 100
	// M' = M / 100
	// Y' = Y / 100
	// K' = K / 100
	float C = cmyk.C /  100.f,
		  M = cmyk.M /  100.f,
		  Y = cmyk.Y /  100.f,
		  K = cmyk.K /  100.f;

	// R = (1 - C')(1 - K') * 255
	// G = (1 - C')(1 - K') * 255
	// B = (1 - C')(1 - K') * 255
	rgb.R = (1.f - C) * (1.f - K);
	rgb.G = (1.f - M) * (1.f - K);
	rgb.B = (1.f - Y) * (1.f - K);

	return rgb;
}

COLORREF ToCOLORREF(MyCMYK cmyk){
	// 0 ~ 1: 정규화 값 확인할 수 있도록 변환 공식 분할
	MyRGB rgb = ToRGB(cmyk);

	int r = (int)(rgb.R * 255.f),
		g = (int)(rgb.R * 255.f),
		b = (int)(rgb.R * 255.f);

	return RGB(r,g,b);
}

HBRUSH CreateCMYKBrush(MyCMYK cmyk){
	COLORREF color = ToCOLORREF(cmyk);
	return CreateSolidBrush(color);
}

void ToHex(MyCMYK cmyk, LPTSTR ret, int Size){
	COLORREF color = ToCOLORREF(cmyk);
	StringCbPrintf(ret, Size, TEXT("%02X%02X%02X"), GetRValue(color), GetGValue(color), GetBValue(color));
}

LRESULT CALLBACK EditProc(HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lParam){
	static CREATESTRUCT* cs;
	static WNDPROC OldEditProc;

	if(OldEditProc == NULL){
		OldEditProc = (WNDPROC)GetProp(GetParent(hWnd), TEXT("MyEditClassProc"));
	}

	switch(iMessage){
		case WM_LBUTTONDOWN:
			SetFocus(hWnd);
			return 0;

		case WM_SETFOCUS:
			SendMessage(hWnd, EM_SETSEL, 0, -1);
			break;

		case WM_CHAR:
		case WM_KEYUP:
		case WM_KEYDOWN:
			{
				WORD VKCode,
					 KeyFlags,
					 ScanCode,
					 RepeatCount;

				BOOL bExtended,
					 bWasKeyDown,
					 bKeyReleased;

				VKCode		= LOWORD(wParam);
				KeyFlags	= HIWORD(lParam);
				ScanCode	= LOBYTE(KeyFlags);
				bExtended	= ((KeyFlags&& KF_EXTENDED) == KF_EXTENDED);
				if(bExtended){ ScanCode = MAKEWORD(ScanCode, 0xE0); }

				bWasKeyDown = ((KeyFlags & KF_REPEAT) == KF_REPEAT);
				RepeatCount = LOWORD(lParam);
				bKeyReleased = ((KeyFlags & KF_UP) == KF_UP);

				if(!bKeyReleased){
					switch(VKCode){
						case VK_UP:
						case VK_DOWN:
						case VK_TAB:
							if(VKCode == VK_TAB){
								if(GetKeyState(VK_LSHIFT) & 0x8000){
									SendMessage(GetParent(hWnd), WM_CHANGEFOCUS, (WPARAM)0, (LPARAM)hWnd);
								}else{
									SendMessage(GetParent(hWnd), WM_CHANGEFOCUS, (WPARAM)1, (LPARAM)hWnd);
								}
							}else if(VKCode == VK_UP){
								SendMessage(GetParent(hWnd), WM_CHANGEFOCUS, (WPARAM)2, (LPARAM)hWnd);
							}else if(VKCode == VK_DOWN){
								SendMessage(GetParent(hWnd), WM_CHANGEFOCUS, (WPARAM)3, (LPARAM)hWnd);
							}
							return 0;

						default:
							break;
					}
				}
			}
			break;

		case WM_CREATE:
			cs = (CREATESTRUCT*)lParam;
	}

	return CallWindowProc(OldEditProc, hWnd, iMessage, wParam, lParam);
}

