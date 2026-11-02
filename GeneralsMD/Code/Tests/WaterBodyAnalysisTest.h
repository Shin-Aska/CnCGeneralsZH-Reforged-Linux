/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once
#include "Platform/WaterBodyAnalysis.h"

namespace WaterBodyAnalysisTest {
using namespace WaterBodyAnalysis;
struct Grid {
    int w, h;
    std::vector<uint8_t> wet, links;
    Grid(int width, int height) : w(width), h(height), wet(size_t(w) * h, 0), links(size_t(w) * h, 0) {}
    void Rect(int x0, int y0, int x1, int y1) {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                wet[y * w + x] = 255;
    }
    void Link() {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const int i = y * w + x;
                links[i] = 0;
                if (!wet[i])
                    continue;
                if (x + 1 < w && wet[i + 1])
                    links[i] |= PositiveX;
                if (y + 1 < h && wet[i + w])
                    links[i] |= PositiveY;
            }
    }
    bool Build(Field &f, int border = 0, const std::vector<VisualConnection> &c = {}) {
        return WaterBodyAnalysis::Build(wet.data(), links.data(), w, h, border, 10, f, c);
    }
};

inline void Check() {
    Field f, again;
    // An unknown crop edge does not form a synthetic bank or erase open sea.
    Grid sea(81, 81);
    sea.Rect(0, 0, 80, 80);
    sea.Link();
    CHECK(sea.Build(f));
    CHECK(f.sourceKind == SourceKind::BroadSea &&f.sourceCells.size() == sea.wet.size());
    for (size_t i = 0; i < sea.wet.size(); ++i)
        CHECK(f.sea[i] == 1 && f.energy[i] == 1 && f.clearance[i] == 10000);
    CHECK(sea.Build(again));
    CHECK(f.sea == again.sea && f.distance == again.distance && f.energy == again.energy);
    CHECK(f.broadCoreArea >= CoreArea &&f.selectedCoreFirstCell >= 0 && f.firstOpeningPosition == 0);
    CHECK(WaterBodyAnalysis::Build(sea.wet.data(), sea.links.data(), sea.w, sea.h, 0, 10, again, {}, false));
    CHECK(again.sourceKind == SourceKind::None &&again.sourceCells.empty()
        && again.broadCoreArea == f.broadCoreArea);
    for (size_t i = 0; i < sea.wet.size(); ++i)
        CHECK(again.sea[i] == 0 && again.energy[i] == CalmEnergy);
    // Subcell authored gaps can leave every sampled node wet. Each 100-world
    // strip still has real banks, and may never acquire a broad-sea core.
    // Only the two outer strips have a long exposed side; the interior strips
    // have short perpendicular mouths and remain passive local water.
    Grid strips(201, 201);
    strips.Rect(0, 0, 200, 200);
    strips.Link();
    for (int y = 0; y < strips.h; ++y)
        for (int x = 9; x + 1 < strips.w; x += 10)
            strips.links[y * strips.w + x] &= ~PositiveX;
    CHECK(strips.Build(again));
    CHECK(again.sourceKind == SourceKind::BoundarySea &&again.qualifyingCores == 0);
    for (float clearance : again.clearance)
        CHECK(clearance <= 95);
    CHECK(again.sea[100 * strips.w] > 0 && again.sea[100 * strips.w + 100] == 0);
    CHECK(again.exterior[100 * strips.w + 200] > 0 && again.sea[100 * strips.w + 200] > 0);
    CHECK(again.sources.size() == 2);
    CHECK(again.energy[100 * strips.w + 100] == CalmEnergy);
    // A shallow coastline need not contain broad-clearance water. Its one
    // selected opening supplies transport; the opposite long exterior opening
    // is diagnostic only, even though both belong to the same physical body.
    Grid shallow(151, 151);
    shallow.Rect(10, 138, 140, 150);
    shallow.Rect(0, 10, 12, 140);
    shallow.Link();
    CHECK(shallow.Build(f));
    const int selected = 150 * shallow.w + 70, passive = 70 * shallow.w, turn = 139 * shallow.w + 11;
    CHECK(f.sourceKind == SourceKind::BoundarySea &&f.validOpenings == 2 && f.qualifyingCores == 0);
    CHECK(f.sourceFrontage == 1300 && f.broadCoreArea == 0 && f.sourceCells.size() == 131);
    for (int i : f.sourceCells)
        CHECK(i / shallow.w == 150 && i % shallow.w >= 10 && i % shallow.w <= 140);
    for (float clearance : f.clearance)
        CHECK(clearance < BroadClearance);
    CHECK(f.component[selected] == f.component[passive] && f.component[turn] == f.component[selected]);
    CHECK(f.sea[selected] == 1 && f.exterior[selected] > 0 && f.distance[selected] == 0);
    CHECK(f.exterior[passive] > 0 && f.sea[passive] == 0 && f.distance[passive] > 0 && f.energy[passive] < 1);
    CHECK(f.sea[turn] >= .5f && f.distance[turn] > 0 && f.energy[turn] < 1); // No reset inside the sea blend.
    CHECK(shallow.Build(again));
    CHECK(f.sea == again.sea && f.exterior == again.exterior && f.sourceCells == again.sourceCells);
    CHECK(f.distance == again.distance && f.energy == again.energy && f.sourceFrontage == again.sourceFrontage);
    CHECK(WaterBodyAnalysis::Build(shallow.wet.data(), shallow.links.data(), shallow.w, shallow.h, 0, 10,
        again, {}, false));
    CHECK(again.sourceKind == SourceKind::None &&again.sourceCells.empty() && again.exterior == f.exterior);
    for (size_t i = 0; i < shallow.wet.size(); ++i)
        if (shallow.wet[i])
            CHECK(again.sea[i] == 0 && again.energy[i] == CalmEnergy);
    // A large western sea cannot suppress independent eastern and northern
    // coastlines. Short mouths and a closed inland body remain unsourced.
    Grid independent(401, 301);
    independent.Rect(0, 20, 120, 280);
    independent.Rect(110, 145, 220, 157);
    independent.Link();
    CHECK(independent.Build(f));
    CHECK(f.sourceKind == SourceKind::BroadSea && f.sources.size() == 1);
    independent.Rect(388, 20, 400, 150);
    independent.Rect(180, 288, 310, 300);
    independent.Rect(200, 0, 212, 12);
    independent.Rect(250, 80, 290, 120);
    independent.Link();
    CHECK(independent.Build(again));
    const int east = 80 * independent.w + 400, northCoast = 300 * independent.w + 240;
    const int shortMouth = 206, closed = 100 * independent.w + 270;
    CHECK(again.sources.size() == 3 && again.sourceKind == f.sourceKind);
    CHECK(again.sourceComponent == again.component[f.selectedCoreFirstCell] && again.broadCoreArea == f.broadCoreArea
        && again.sourceFrontage == f.sourceFrontage && again.selectedCoreFirstCell == f.selectedCoreFirstCell);
    CHECK(again.sourceHeading.x == f.sourceHeading.x && again.sourceHeading.y == f.sourceHeading.y);
    CHECK(again.sources[0].cellCount == f.sourceCells.size());
    for (size_t k = 0; k < f.sourceCells.size(); ++k)
        CHECK(again.sourceCells[k] == f.sourceCells[k]);
    for (size_t i = 0; i < f.component.size(); ++i)
        if (f.component[i] >= 0) {
            CHECK(again.sea[i] == f.sea[i] && again.energy[i] == f.energy[i]
                && again.distance[i] == f.distance[i] && again.downstreamArea[i] == f.downstreamArea[i]);
            CHECK(again.direction[i].x == f.direction[i].x && again.direction[i].y == f.direction[i].y);
        }
    for (int i : {east, northCoast}) {
        const auto &region = again.sources[again.componentSource[again.component[i]]];
        CHECK(region.kind == SourceKind::BoundarySea && again.sea[i] == 1 && again.distance[i] == 0);
        CHECK(again.energy[i] > CalmEnergy && again.energy[i] < 1); // Boundary shelter is retained.
    }
    for (int i : {shortMouth, closed})
        CHECK(again.componentSource[again.component[i]] == -1 && again.sea[i] == 0
            && !std::isfinite(again.distance[i]) && again.energy[i] == CalmEnergy);
    CHECK(WaterBodyAnalysis::Build(independent.wet.data(), independent.links.data(), independent.w,
        independent.h, 0, 10, again, {}, false));
    CHECK(again.sources.empty() && again.sourceCells.empty() && again.sourceKind == SourceKind::None);
    for (int id : again.componentSource)
        CHECK(id == -1);
    for (size_t i = 0; i < independent.wet.size(); ++i)
        if (independent.wet[i])
            CHECK(again.sea[i] == 0 && again.energy[i] == CalmEnergy && !std::isfinite(again.distance[i]));
    // Independently qualifying deep seas each preserve their full source gain.
    Grid twoSeas(301, 181);
    twoSeas.Rect(0, 10, 110, 170);
    twoSeas.Rect(190, 10, 300, 170);
    twoSeas.Link();
    CHECK(twoSeas.Build(f));
    CHECK(f.sources.size() == 2 && f.sources[0].kind == SourceKind::BroadSea
        && f.sources[1].kind == SourceKind::BroadSea);
    const int westSea = 90 * twoSeas.w + 30, eastSea = 90 * twoSeas.w + 270;
    CHECK(f.sea[westSea] == 1 && f.sea[eastSea] == 1 && f.energy[westSea] == 1 && f.energy[eastSea] == 1);
    // Joining those independent candidates by an authored underpass elects one
    // source for transport, while physical exterior evidence stays unchanged.
    CHECK(twoSeas.Build(again, 0, {{westSea, eastSea, .65f}}));
    CHECK(again.sources.size() == 1 && again.exterior == f.exterior);
    const int passiveSea = again.sourceComponent == again.component[westSea] ? eastSea : westSea;
    CHECK(again.sea[passiveSea] == 0 && again.distance[passiveSea] > 0 && again.energy[passiveSea] < 1);
    // Frontage measures covered straight segments in world space, including
    // the exact threshold. Sample count, corner wrapping and inland length
    // cannot convert a subthreshold opening into sea.
    Grid threshold(111, 31);
    threshold.Rect(10, 18, 100, 30);
    threshold.Link();
    CHECK(threshold.Build(f));
    CHECK(f.sourceKind == SourceKind::BoundarySea &&f.sourceFrontage == 900);
    threshold.wet[30 * threshold.w + 100] = 0;
    threshold.Link();
    CHECK(threshold.Build(f));
    CHECK(f.sourceKind == SourceKind::ClockwisePerimeter);
    for (float value : f.sea)
        CHECK(value == 0);
    Grid bentFrontage(101, 101);
    bentFrontage.Rect(0, 88, 50, 100);
    bentFrontage.Rect(0, 50, 12, 100);
    bentFrontage.Link();
    CHECK(bentFrontage.Build(f));
    CHECK(f.sourceKind == SourceKind::ClockwisePerimeter &&f.validOpenings == 1);
    for (float value : f.sea)
        CHECK(value == 0);
    Grid brokenFrontage(121, 41);
    brokenFrontage.Rect(10, 28, 110, 40);
    brokenFrontage.Link();
    brokenFrontage.links[40 * brokenFrontage.w + 60] &= ~PositiveX;
    CHECK(brokenFrontage.Build(f));
    CHECK(f.sourceKind == SourceKind::ClockwisePerimeter &&f.validOpenings == 2);
    CHECK(f.component[40 * brokenFrontage.w + 30] == f.component[40 * brokenFrontage.w + 90]);
    for (float value : f.sea)
        CHECK(value == 0);
    Grid perpendicular(121, 51);
    perpendicular.Rect(0, 18, 110, 30);
    perpendicular.Link();
    CHECK(perpendicular.Build(f));
    CHECK(f.sourceKind == SourceKind::ClockwisePerimeter);
    for (float value : f.sea)
        CHECK(value == 0);
    // A short opening that comes first clockwise must not outrank a later
    // long coastline; selection still uses only the qualifying opening.
    Grid laterCoast(151, 151);
    laterCoast.Rect(20, 138, 32, 150);
    laterCoast.Rect(138, 10, 150, 130);
    laterCoast.Link();
    CHECK(laterCoast.Build(f));
    CHECK(f.sourceKind == SourceKind::BoundarySea &&f.sourceFrontage == 1200);
    for (int i : f.sourceCells)
        CHECK(i % laterCoast.w == 150);
    CHECK(f.sea[150 * laterCoast.w + 26] == 0 && !std::isfinite(f.distance[150 * laterCoast.w + 26]));
    // The same 900-world coastline qualifies at two grid spacings.
    Grid fineCoast(201, 41);
    fineCoast.Rect(10, 16, 190, 40);
    fineCoast.Link();
    CHECK(WaterBodyAnalysis::Build(fineCoast.wet.data(), fineCoast.links.data(), fineCoast.w, fineCoast.h, 0,
        5, again));
    CHECK(again.sourceKind == SourceKind::BoundarySea &&again.sourceFrontage == 900);
    // Authored elevation selection uses the same exterior evidence ordering.
    CHECK(PreferSource(again, SourceKind::ClockwisePerimeter, 0, 0, 0));
    CHECK(PreferSource(again, SourceKind::BroadSea, 890, 1000000, 0));
    CHECK(!PreferSource(again, SourceKind::BroadSea, 910, 0, 0));
    CHECK(!PreferSource(again, SourceKind::BoundarySea, 900, 0, 1)); // Exact tie keeps the incumbent.
    Field broadCandidate = again;
    broadCandidate.sourceKind = SourceKind::BroadSea;
    broadCandidate.broadCoreArea = CoreArea;
    CHECK(PreferSource(broadCandidate, SourceKind::BoundarySea, 900, 0, 0));
    Field fallback;
    fallback.sourceKind = SourceKind::ClockwisePerimeter;
    fallback.firstOpeningPosition = .1f;
    CHECK(!PreferSource(fallback, SourceKind::BoundarySea, 900, 0, .9f));
    CHECK(PreferSource(fallback, SourceKind::ClockwisePerimeter, 0, 0, .2f));
    // Two long openings may touch one continuous broad region without a
    // qualifying deep core. That shared region is exterior geometry, but must
    // not turn a distant passive opening into original-spectrum sea or seeds.
    Grid sharedBroad(401, 401);
    sharedBroad.Rect(150, 350, 250, 400);
    sharedBroad.Rect(175, 0, 225, 400);
    sharedBroad.Rect(150, 0, 250, 50);
    sharedBroad.Link();
    CHECK(sharedBroad.Build(f));
    const int farBroad = 200, midBroad = 200 * sharedBroad.w + 200;
    CHECK(f.sourceKind == SourceKind::BoundarySea &&f.validOpenings == 2 && f.qualifyingCores == 0);
    CHECK(f.sourceCells.size() == 101 && f.sourceFrontage == 1000);
    for (int i : f.sourceCells)
        CHECK(i / sharedBroad.w == 400);
    CHECK(f.exterior[farBroad] > 0 && f.sea[farBroad] == 0 && f.distance[farBroad] > 0 && f.energy[farBroad] < 1);
    CHECK(f.sea[midBroad] == 0 && f.distance[midBroad] > 0 && f.energy[midBroad] < 1);
    // A long blind continuation in the same broad region also retains its
    // transported local field once it is far from the incoming coastline.
    Grid blindBroad(151, 251);
    blindBroad.Rect(25, 200, 125, 250);
    blindBroad.Rect(50, 10, 100, 220);
    blindBroad.Link();
    CHECK(blindBroad.Build(f));
    const int blindEnd = 20 * blindBroad.w + 75;
    CHECK(f.sourceKind == SourceKind::BoundarySea &&f.validOpenings == 1 && f.qualifyingCores == 0);
    CHECK(f.exterior[blindEnd] > 0 && f.sea[blindEnd] == 0 && f.distance[blindEnd] > 0 && f.energy[blindEnd] < 1);
    // A large closed lake has a core, but no exposed broad region. It is calm.
    Grid lake(181, 181);
    lake.Rect(10, 10, 170, 170);
    lake.Link();
    CHECK(lake.Build(f));
    CHECK(f.sourceKind == SourceKind::None &&f.sourceCells.empty());
    for (size_t i = 0; i < lake.wet.size(); ++i) {
        CHECK(f.sea[i] == 0);
        if (lake.wet[i])
            CHECK(f.energy[i] == CalmEnergy);
    }
    // A large lake connected to the edge through a thin canal cannot claim sea.
    lake.Rect(0, 85, 10, 96);
    lake.Link();
    CHECK(lake.Build(f));
    CHECK(f.sourceKind == SourceKind::ClockwisePerimeter &&f.qualifyingCores == 0);
    for (float value : f.sea)
        CHECK(value == 0);
    // An island stops short of one contiguous wet opening, splitting exterior
    // water into two broad lobes. Their eligible cores (146000 and 144400)
    // qualify together, although neither connected core reaches CoreArea.
    Grid coast(273, 281);
    coast.Rect(10, 190, 130, 280);
    coast.Rect(142, 190, 262, 280);
    coast.Rect(131, 272, 141, 280);
    coast.Rect(50, 10, 220, 170);
    coast.Rect(67, 170, 78, 190);
    coast.Link();
    CHECK(coast.Build(f));
    const int leftSea = 250 * coast.w + 70, rightSea = 250 * coast.w + 202;
    const int canal = 180 * coast.w + 72, inlandLake = 90 * coast.w + 135;
    CHECK(f.sourceKind == SourceKind::BroadSea &&f.validOpenings == 1 && f.broadCoreArea == 290400);
    CHECK(f.sea[leftSea] == 1 && f.sea[rightSea] == 1 && f.energy[leftSea] == 1 && f.energy[rightSea] == 1);
    // The larger inland core (575100) shares the physical body through a thin
    // canal, but supplies no exterior evidence and receives no sea seeds.
    CHECK(f.component[leftSea] == f.component[inlandLake]);
    CHECK(f.sea[canal] == 0 && f.sea[inlandLake] == 0 && f.energy[inlandLake] < 1);
    CHECK(coast.Build(again));
    CHECK(f.sea == again.sea && f.sourceCells == again.sourceCells && f.energy == again.energy);
    CHECK(f.selectedCoreFirstCell == again.selectedCoreFirstCell && f.broadCoreArea == again.broadCoreArea);
    // A wider gap joins the broad region, but still splits its deep cores.
    // All eligible cells count, not merely the largest connected core piece.
    Grid splitCore(273, 91);
    splitCore.Rect(10, 10, 130, 90);
    splitCore.Rect(142, 10, 262, 90);
    splitCore.Rect(131, 50, 141, 90);
    splitCore.Link();
    CHECK(splitCore.Build(f));
    CHECK(f.sourceKind == SourceKind::BroadSea &&f.broadCoreArea == 310800);
    CHECK(f.sea[70 * splitCore.w + 70] == 1 && f.sea[70 * splitCore.w + 202] == 1);
    // An uncovered subcell edge splits an otherwise wet perimeter into two
    // openings. Its subthreshold cores cannot pool through the inland detour,
    // even though that narrow passage keeps one physical component.
    Grid separate(273, 101);
    separate.Rect(10, 10, 262, 100);
    separate.Link();
    for (int y = 40; y <= 100; ++y)
        separate.links[y * separate.w + 136] &= ~PositiveX;
    CHECK(separate.Build(f));
    CHECK(f.component[70 * separate.w + 70] == f.component[70 * separate.w + 202]);
    CHECK(f.sourceKind == SourceKind::BoundarySea &&f.validOpenings == 2 && f.qualifyingCores == 0);
    CHECK(f.sea[70 * separate.w + 70] > 0 && f.sea[70 * separate.w + 202] == 0);
    CHECK(f.exterior[100 * separate.w + 202] > 0 && f.distance[100 * separate.w + 202] > 0
        && f.energy[100 * separate.w + 202] < 1);
    // Clockwise perimeter selection emits from one opening only. The other
    // opening remains passive even where it touches the stored map boundary.
    Grid forks(61, 61);
    forks.Rect(0, 40, 60, 52);
    forks.Rect(26, 0, 38, 52);
    forks.Link();
    CHECK(forks.Build(f));
    CHECK(f.sourceKind == SourceKind::ClockwisePerimeter &&f.validOpenings == 3);
    CHECK(f.sourceHeading.x < 0 && std::abs(std::hypot(f.sourceHeading.x, f.sourceHeading.y) - 1) < 1e-6f);
    for (int i : f.sourceCells)
        CHECK(i % forks.w == forks.w - 1); // Right opening precedes bottom and left.
    CHECK(f.distance[6 * forks.w + 32] > 0 && f.distance[46 * forks.w] > 0);
    // A corner-spanning opening is one source; both sides survive scan wrapping.
    Grid corner(31, 31);
    corner.Rect(0, 24, 6, 30);
    corner.Link();
    CHECK(corner.Build(f));
    CHECK(f.sourceKind == SourceKind::ClockwisePerimeter &&f.validOpenings == 1);
    bool north = false, west = false;
    for (int i : f.sourceCells) {
        north = north || i / corner.w == 30;
        west = west || i % corner.w == 0;
    }
    CHECK(north && west);
    // Adjacent wet nodes with an uncovered subcell edge are separate bodies.
    // Source transport cannot tunnel through the visually tiny authored gap.
    Grid gap(61, 31);
    gap.Rect(0, 8, 60, 22);
    gap.Link();
    for (int y = 8; y <= 22; ++y)
        gap.links[y * gap.w + 29] &= ~PositiveX;
    CHECK(gap.Build(f));
    CHECK(f.component[15 * gap.w + 20] != f.component[15 * gap.w + 40]);
    const int first = f.sourceCells.front();
    const int other = first % gap.w == 60 ? 15 * gap.w + 10 : 15 * gap.w + 50;
    CHECK(!std::isfinite(f.distance[other]) && f.energy[other] == CalmEnergy &&f.direction[other].x == 0
        && f.direction[other].y == 0);
    // The explicit visual connection transmits into a separate wet body while
    // leaving its physical identity and the intervening dry cells intact.
    Grid bridge(61, 41);
    bridge.Rect(0, 8, 20, 22);
    bridge.Rect(26, 8, 50, 22);
    bridge.Link();
    CHECK(bridge.Build(f));
    const int a = 15 * bridge.w + 20, b = 15 * bridge.w + 26;
    const float physicalSea = f.sea[b];
    CHECK(bridge.Build(f, 0, {{a, b, .65f}}));
    CHECK(f.component[a] != f.component[b] && std::isfinite(f.distance[b]));
    CHECK(f.sea[b] == physicalSea && f.energy[b] > CalmEnergy &&f.energy[b] < f.energy[a]);
    for (int x = 21; x <= 25; ++x)
        CHECK(f.energy[15 * bridge.w + x] == 0 && f.component[15 * bridge.w + x] == -1);
    CHECK(!bridge.Build(f, 0, {{a, b, 0}}));
    CHECK(f.energy.empty());
    // Equal bank geometry and fully supported continuation cancel the local
    // gain/shelter terms: the visual span retains its exact transmission budget.
    Grid budget(281, 31);
    budget.Rect(0, 8, 160, 22);
    budget.Rect(166, 8, 270, 22);
    budget.Link();
    const int pa = 15 * budget.w + 160, pb = 15 * budget.w + 166;
    CHECK(budget.Build(f, 0, {{pa, pb, .65f}}));
    CHECK(f.energy[pa] > CalmEnergy &&f.energy[pb] > CalmEnergy);
    CHECK(std::abs(f.energy[pb] / f.energy[pa] - .65f * std::exp(-60.0f / AttenuationLength)) < 1e-5f);
    // Equal-width, equal-distance necks: a long continuing route carries more
    // downstream support than a short blind bay. No boundary bonus is involved.
    Grid routes(121, 121);
    routes.Rect(0, 70, 30, 90);
    routes.Rect(25, 60, 35, 100);
    routes.Rect(30, 90, 110, 100);
    routes.Rect(30, 60, 55, 70);
    routes.Link();
    CHECK(routes.Build(f));
    const int through = 95 * routes.w + 48, bay = 65 * routes.w + 48;
    CHECK(f.downstreamArea[through] > f.downstreamArea[bay]);
    CHECK(f.energy[through] > f.energy[bay]);
    auto reviewedEnergy = [&](int i) {
        return std::exp(-f.distance[i] / AttenuationLength) * (.35f
            + .65f * Clamp01(f.clearance[i] / ShelterClearance));
    };
    CHECK(f.energy[through] > reviewedEnergy(through));
    CHECK(f.energy[bay] < reviewedEnergy(bay));
    // Padding neither supplies a source nor changes the playable crop's banks.
    Grid apron(61, 61);
    apron.Rect(0, 0, 60, 60);
    apron.Link();
    CHECK(apron.Build(f, 8));
    CHECK(f.sourceKind == SourceKind::BroadSea);
    CHECK(f.energy[0] == 0 && f.clearance[0] == 0);
    CHECK(f.clearance[30 * apron.w + 8] == 10000);
    CHECK(!WaterBodyAnalysis::Build(nullptr, apron.links.data(), 61, 61, 0, 10, f));
    CHECK(!WaterBodyAnalysis::Build(apron.wet.data(), nullptr, 61, 61, 0, 10, f));
    CHECK(!WaterBodyAnalysis::Build(apron.wet.data(), apron.links.data(), 61, 61, 0, 0, f));
}
}
