// Replacements for the GLFW / Cocoa pieces of app/main.mm in the headless build.
#pragma once
#include "swrender.h"
#include <chrono>
#include <cstring>
struct GLFWwindow;
static inline double glfwGetTime() { using namespace std::chrono; static const auto t0 = steady_clock::now(); return duration<double>(steady_clock::now() - t0).count(); }
#define glfwGetWindowMonitor(w) ((void*)nullptr)
