@echo off
setlocal
cd /d "%~dp0"
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /D_CRT_SECURE_NO_WARNINGS src\main.cpp src\compiler.cpp src\runtime.cpp src\artifact.cpp src\native.cpp src\lsp.cpp /Fe:tacqor.exe ws2_32.lib
exit /b %errorlevel%
