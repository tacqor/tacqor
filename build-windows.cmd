@echo off
setlocal
cd /d "%~dp0"
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 src\main.cpp src\compiler.cpp src\runtime.cpp src\artifact.cpp src\native.cpp /Fe:tacqor.exe
exit /b %errorlevel%
