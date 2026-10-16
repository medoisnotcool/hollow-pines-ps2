@echo off
title Hollow Pines Relay
where node >nul 2>nul || (echo Node.js is not installed. Use hollow-pines-relay.exe instead.& pause & exit /b)
node "%~dp0relay.js"
pause
