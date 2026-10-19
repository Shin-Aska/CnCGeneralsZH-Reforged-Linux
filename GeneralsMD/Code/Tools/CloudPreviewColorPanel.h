// Color controls for the isolated cloud preview, not the production game UI.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#include <SDL3/SDL.h>

namespace CloudPreviewColorPanel {

// Installs a GameWindow overlay, drawn after the 3D scene by the existing UI
// renderer. Call after the preview scene initializes and before engine teardown.
bool initialize();
void shutdown();

// Call before forwarding events to the game. Consumes panel mouse input and P.
bool handle(const SDL_Event &event);
bool visible();
void setVisible(bool value);
unsigned selectedPalette();
void selectPalette(unsigned palette);

// The controller owns scene changes; a selection alone does not restart a scene.
// Returns the requested palette (0 toxin, 1 nuclear, 2 beta, 3 gamma), or -1.
int takePreviewRequest();

} // namespace CloudPreviewColorPanel
