rmdir build /s /q 
mkdir build

@REM debug build
@REM g++ -g src/main.cpp src/miniaudio.c src/AudioFFT.cpp -o build/main.exe
@REM g++ -g src/main.cpp src/miniaudio.c src/AudioFFT.cpp -lgdi32 -lwinmm -o build/main.exe
@REM g++ -D MYDEBUG=0 -g src/main.cpp src/miniaudio.c src/AudioFFT.cpp -lgdi32 -lwinmm -o build/main.exe
g++ -D MYDEBUG=0 -g src/main.cpp src/miniaudio.c src/AudioFFT.cpp src/Yin.c -lgdi32 -lwinmm -o build/main.exe

@REM release build
@REM g++ src/main.cpp src/miniaudio.c src/AudioFFT.cpp src/Yin.c -lgdi32 -lwinmm -o build/main.exe