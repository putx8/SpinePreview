#pragma once
#include <string>
#include <cstdint>
struct SDL_Window;
std::string applicationExecutablePath();
bool launchNewApplication(SDL_Window *sourceWindow, std::string &error);
void installApplicationMenu(uint32_t newInstanceEvent);
std::string takeApplicationLaunchError();
