#define UNICODE
#include <windows.h>
#define SHARED	__attribute__((section(".mhdata"), shared))

#ifdef MYAPIDLL_EXPORTS
#define MY_API __declspec(dllexport)
#else
#define MY_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C"{
#endif

	MY_API void MyInit(HWND hWnd, HHOOK hMouse, HHOOK hKeyboard);
	MY_API LRESULT CALLBACK MyMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
	MY_API LRESULT CALLBACK MyKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

#ifdef __cplusplus
}
#endif

HWND	g_hWnd		SHARED	= NULL;
HHOOK	g_hMouse	SHARED	= NULL;
HHOOK	g_hKeyboard SHARED	= NULL;

LRESULT CALLBACK MyMouseProc(int nCode, WPARAM wParam, LPARAM lParam){
	if(nCode == HC_ACTION){
		if(g_hWnd != NULL){
			SendMessage(g_hWnd, WM_USER+321, wParam, lParam);
		}
	}

	return CallNextHookEx(g_hMouse, nCode, wParam, lParam);
}

LRESULT CALLBACK MyKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam){
	if(nCode == HC_ACTION){
		if(g_hWnd != NULL){
			SendMessage(g_hWnd, WM_USER+123, wParam, lParam);
		}
	}

	return CallNextHookEx(g_hKeyboard, nCode, wParam, lParam);
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

void MyInit(HWND hWnd, HHOOK hMouse, HHOOK hKeyboard){
	g_hWnd		= hWnd;
	g_hMouse	= hMouse;
	g_hKeyboard = hKeyboard;
}
