g++ -c -o MyApiDll.o MyApiDll.cpp -mwindows -D MYAPIDLL_EXPORTS
g++ -o MyApiDll.dll MyApiDll.o -mwindows -s -shared -Wl,--subsystem,windows
