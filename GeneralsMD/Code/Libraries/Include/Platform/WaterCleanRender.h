#pragma once
#include "Platform/WaterCleanPrecompute.h"
#ifndef _WIN32
class IDirect3DDevice9;
bool PosixWaterCleanBind(IDirect3DDevice9 *,const WaterCleanPrecompute::Image *,const WaterCleanPrecompute::Image *,bool mainLevel=false);
void PosixWaterCleanClear(IDirect3DDevice9 *);
#endif
