// setenv / unsetenv for the tests: MinGW has neither, so Windows uses _putenv_s (an empty value removes the variable).
#pragma once
#include <cstdlib>

#ifdef _WIN32
static inline int setenv(const char* k, const char* v, int) { return _putenv_s(k, v); }
static inline int unsetenv(const char* k) { return _putenv_s(k, ""); }
#endif
