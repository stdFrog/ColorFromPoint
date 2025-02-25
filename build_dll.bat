g++ -c -o MyMouseDll.o MyMouseDll.cpp -mwindows -D MOUSEDLL_EXPORTS
g++ -o MyMouseDll.dll MyMouseDll.o -mwindows -s -shared -Wl,--subsystem,windows
