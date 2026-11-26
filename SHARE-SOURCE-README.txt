RR Studio External - source archive

Build on Windows x64 with Visual Studio C++ tools:
  build_port.bat
The resulting binary is RobRunPort.exe in this directory.
WebView2SDK is included as a build dependency; see its LICENSE.txt.

This is source code, not an installer. The separate runtime ZIP includes the
EXE and WebView2Loader.dll, but not a kernel driver. Building the EXE or
running --selftest does not verify compatibility with the current game.

No saved CoreRR.cfg, driver mapper, personal logs or compiled test artifacts
are included in this source ZIP.
