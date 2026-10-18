// Render-only bridge. The device caches immutable domain/view identities; no map mutation.
#pragma once
#include "Platform/WaterCoastalView.h"
#ifndef _WIN32
class IDirect3DDevice9;
bool PosixWaterCoastalBind(IDirect3DDevice9 *,uint64_t,const WaterCoastalDomain::Domain &,
    const WaterShoreHistory::Mapping &,double,const std::vector<WaterCoastalSurf::Lookup> &);
void PosixWaterCoastalEnd(IDirect3DDevice9 *);
#endif
