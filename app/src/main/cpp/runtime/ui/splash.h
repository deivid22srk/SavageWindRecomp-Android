// The start screen's background (runtime/ui/splash.mm): the player's own picture, splash.jpg / splash.png in the data
// folder, else Savage Wind's own art (a forest clearing with the icon's sword in a stone, drawn, the grass moving).
// Nothing of the game ships for it.
#pragma once
#include "imgui.h"

void splash_load(void);    // after the ImGui renderer backend is up
void splash_unload(void);  // before it shuts down
void splash_draw(ImDrawList *dl, ImVec2 size, float time);
bool splash_is_user(void);  // the player's picture is shown
