# Techlab4_Week07_Team1

## Windows team setup

Install Git and Visual Studio 2022 or 2026 with C++ build tools, then run `SlnGenerator.bat` from the repository root to generate the solution. It reuses `VCPKG_ROOT` when set, or installs vcpkg in `%USERPROFILE%\vcpkg`, then installs LuaJIT and sol2 if needed. To also build the Release Editor and Game, run `SlnGenerator.bat -Build`; add `-Configuration Debug` for a Debug build. The first run needs internet access.
