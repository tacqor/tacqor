# Tacqor Live Scanner

Open a `.tq` file in a trusted VS Code workspace. The extension checks unsaved code using the standalone compiler and displays findings in **View → Problems**. It includes a compiler for Linux x64, including a Linux x64 WSL workspace. On another platform, build the compiler and set **Tacqor: Compiler Path** to its executable. The scanner analyzes the source without running the program. Imports resolve relative to the file and the bundled `std` library.
