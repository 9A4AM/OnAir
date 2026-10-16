// Shared by app_state.cpp and prefs.cpp: the few mode helpers the settings loader needs.
#pragma once
#include "../app.h"

void setFamilyFlags(App& a, int f);   // app_state.cpp: the booleans (fmMode, dabMode ...) that follow family f
int firstSubOf(int f);                // app_state.cpp: the first protocol entry of a family that is only listed as its protocols, else 0
std::string slotKey(int slot);        // app_state.cpp: the suffix of the per-mode settings: the family number, or "." and the protocol's id
