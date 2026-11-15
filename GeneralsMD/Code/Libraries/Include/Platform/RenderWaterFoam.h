// Classic-water foam admission. Hardware support remains independent of this view's readiness.
#pragma once
namespace RenderWaterFoam { bool Ready(); }
#if !defined(_WIN32)
struct IDirect3DDevice9;
namespace RenderWaterFoam { bool Ready(IDirect3DDevice9 *); }
#endif
