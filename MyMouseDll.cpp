#define UNICODE
#include <windows.h>
#define SHARED	__attribute__((section(".mhdata"), shared))

#ifdef MOUSEDLL_EXPORTS
#define MOUSE_API __declspec(dllexport)
#else
#define MOUSE_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C"{
#endif

	MOUSE_API void Init(HWND hWnd, HHOOK hHook);
	MOUSE_API LRESULT CALLBACK MyMouseProc(int nCode, WPARAM wParam, LPARAM lParam);

#ifdef __cplusplus
}
#endif

HWND	g_hWnd SHARED	= NULL;
HHOOK	g_Hook SHARED	= NULL;

// TODO: lParam 값 읽고 데이터 가공해서 전달
LRESULT CALLBACK MyMouseProc(int nCode, WPARAM wParam, LPARAM lParam){
	if(nCode == HC_ACTION && g_hWnd != NULL){
		SendMessage(g_hWnd, WM_USER+321, wParam, lParam);
	}

	return CallNextHookEx(g_Hook, nCode, wParam, lParam);
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD fdwReason, LPVOID lpRes){
	switch (fdwReason) {
	case DLL_PROCESS_ATTACH:
		break;
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

void Init(HWND hWnd, HHOOK hHook){
	g_hWnd = hWnd;
	g_Hook = hHook;
}
