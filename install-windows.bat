@echo off
rem Copies the built Prism Rack.vst3 into the standard Windows VST3 folder.
rem Right-click this file and choose "Run as administrator".
set SRC=%~dp0build\PrismRack_artefacts\Release\VST3\Prism Rack.vst3
set DST=%CommonProgramFiles%\VST3
if not exist "%SRC%" (
  echo Could not find "%SRC%". Build the project first ^(see README^).
  pause
  exit /b 1
)
if not exist "%DST%" mkdir "%DST%"
xcopy /E /I /Y "%SRC%" "%DST%\Prism Rack.vst3"
echo.
echo Installed to %DST%\Prism Rack.vst3
echo In Ableton: Preferences ^> Plug-Ins ^> make sure "Use VST3 Plug-in System Folders" is on, then Rescan.
pause
