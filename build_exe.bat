g++ -c -o main.o main.cpp -mwindows
g++ -o ColorFromPoint.exe -s main.o -mwindows -L. -lMyApiDll
