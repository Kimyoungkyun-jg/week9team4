# Techlab4_Week07_Team1

## Windows team setup

Install Git and Visual Studio 2022 or 2026 with C++ build tools, then run `SlnGenerator.bat` from the repository root. It reuses `VCPKG_ROOT` when set, or installs vcpkg in `%USERPROFILE%\vcpkg`; installs LuaJIT and sol2, generates the Visual Studio solution, and builds the Release Editor and Game. Run `SlnGenerator.bat -Configuration Debug` for a Debug build. The first run needs internet access.
