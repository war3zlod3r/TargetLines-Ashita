call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64 -no_logo
if errorlevel 1 exit /b 1
cmake -S "C:\Users\Claude\Documents\Max Projects\TargetLines-Ashita" -B "C:\Users\Claude\Documents\Max Projects\TargetLines-Ashita\out\build\x86-release" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe
if errorlevel 1 exit /b 1
cmake --build "C:\Users\Claude\Documents\Max Projects\TargetLines-Ashita\out\build\x86-release"
