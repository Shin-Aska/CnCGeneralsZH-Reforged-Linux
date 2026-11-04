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
#include "Platform/WaterOpenSeaSplit.h"
#include <limits>

namespace WaterOpenSeaSplitTest {
using namespace WaterOpenSeaSplit;
inline void MaskChecks() {
    CHECK(!Mask().Valid());CHECK(Mask().Any());CHECK(Mask().Touches(0,0,1,1));
    CHECK(Mask({},4,4).Touches(-100,-100,100,100));
    CHECK(Mask(std::vector<uint32_t>(4),1,4).Touches(0,0,0,0));
    const int w=15,h=12;
    std::vector<uint32_t> words(size_t(w)*h,0xffffff00u);
    Mask zero(words,w,h);
    CHECK(zero.Valid());CHECK(!zero.Any());CHECK(!zero.Touches(-1000,-1000,1000,1000));
    // Only the low byte is influence. One corner belongs to four cells, then dilates two cells.
    words[6*w+7]|=1;
    Mask one(words,w,h);
    CHECK(one.Any());
    for(int y=0;y<h-1;++y)for(int x=0;x<w-1;++x)
        CHECK(one.Touches(x+.25f,y+.25f,x+.75f,y+.75f)==(x>=4 && x<=9 && y>=3 && y<=8));
    CHECK(one.Touches(0,6,14,6));CHECK(!one.Touches(0,0,3.99f,2.99f));
    CHECK(!one.Touches(-100,-100,-1,-1));CHECK(!one.Touches(100,100,200,200));
    words.assign(size_t(w)*h,0);words[0]=1;words.back()=1;
    Mask borders(words,w,h);
    CHECK(borders.Touches(-100,-100,-1,-1));CHECK(borders.Touches(100,100,200,200));
    CHECK(!borders.Touches(-100,5,-1,6));CHECK(!borders.Touches(100,5,200,6));
    CHECK(borders.Touches(0,0,0,0));CHECK(borders.Touches(w-1,h-1,w-1,h-1));
    CHECK(zero.Touches(std::numeric_limits<float>::quiet_NaN(),0,1,1));
    CHECK(zero.Touches(2,0,1,1));
    // Exhaustive independent cell/dilation oracle, including degenerate and clamped boxes.
    for(int cornerY=0;cornerY<h;++cornerY)for(int cornerX=0;cornerX<w;++cornerX) {
        words.assign(size_t(w)*h,0);words[cornerY*w+cornerX]=255;
        Mask mask(words,w,h);
        for(int y=-2;y<=h+1;++y)for(int x=-2;x<=w+1;++x) {
            const int cx=(std::max)(0,(std::min)(w-2,x)),cy=(std::max)(0,(std::min)(h-2,y));
            bool expected=false;
            for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx) {
                const int sx=cx+dx,sy=cy+dy;
                if(sx>=0 && sy>=0 && sx<w-1 && sy<h-1 && cornerX>=sx && cornerX<=sx+1
                    && cornerY>=sy && cornerY<=sy+1)expected=true;
            }
            CHECK(mask.Touches(float(x),float(y),float(x),float(y))==expected);
        }
    }
}
inline void CacheChecks();
inline void PartitionChecks() {
    std::vector<uint32_t> words(32*24,0);words[12*32+16]=1;
    Mask mask(words,32,24);
    const float spacing=10,border=3;
    std::vector<Point> vertices;
    std::vector<uint16_t> indices;
    // Alternating local/sea triangles. Indices are distinct, so loss/duplication is detectable.
    for(unsigned i=0;i<8;++i) {
        const float gx=i%2?5.0f:16.0f,gy=i%2?4.0f:12.0f;
        for(Point p: {Point{gx,gy},Point{gx+.5f,gy},Point{gx,gy+.5f}}) {
            indices.push_back(uint16_t(vertices.size()));
            vertices.push_back({(p.x-border)*spacing,(p.y-border)*spacing});
        }
    }
    auto position=[&](uint16_t i){return vertices[i];};
    std::vector<uint16_t> open,local;
    std::vector<Disc> discs;
    Partition(indices,position,mask,spacing,border,discs,open,local);
    CHECK(open.size()==12 && local.size()==12);
    for(size_t i=0;i<4;++i)for(size_t c=0;c<3;++c) {
        CHECK(open[i*3+c]==indices[(i*2+1)*3+c]);CHECK(local[i*3+c]==indices[i*6+c]);
    }
    auto all=open;all.insert(all.end(),local.begin(),local.end());std::sort(all.begin(),all.end());
    CHECK(all==indices);
    const size_t openCapacity=open.capacity(),localCapacity=local.capacity();
    open.clear();local.clear();
    Partition(indices,position,mask,spacing,border,discs,open,local);
    CHECK(open.capacity()==openCapacity && local.capacity()==localCapacity);
    // Appending preserves the caller's prefix too.
    Partition(indices,position,mask,spacing,border,discs,open,local);
    CHECK(open.size()==24 && local.size()==24);
    CHECK(std::equal(open.begin(),open.begin()+12,open.begin()+12));
    discs.push_back({20,10,0});open.clear();local.clear(); // on the sea triangles' bounding box
    Partition(indices,position,mask,spacing,border,discs,open,local);
    CHECK(open.empty() && local==indices);
    discs[0]={0,0,1};open.clear();local.clear();
    Partition(indices,position,mask,spacing,border,discs,open,local);
    CHECK(open.size()==12 && local.size()==12);
    open.clear();local.clear();
    Partition(indices,position,Mask(),spacing,border,discs,open,local);
    CHECK(open.empty() && local==indices);
    // A large triangle crossing a needed cell is retained even with all vertices outside it.
    std::vector<Point> spanning={{-30,90},{290,90},{-30,100}};
    std::vector<uint16_t> triangle={0,1,2};open.clear();local.clear();
    Partition(triangle,[&](uint16_t i){return spanning[i];},mask,spacing,border,{},open,local);
    CHECK(open.empty() && local==triangle);
    // A margin can retain a triangle just outside a disc, without expanding the disc itself.
    open.clear();local.clear();discs={{18,10,1}};
    Partition(indices,position,Mask(std::vector<uint32_t>(32*24),32,24),spacing,border,discs,open,local,1);
    CHECK(local.size()==12 && open.size()==12);
    // A hull beside open water keeps it open unless the shoreline is within its reflection's reach.
    open.clear();local.clear();discs={{20,10,1000,5}};
    Partition(indices,position,mask,spacing,border,discs,open,local);
    CHECK(open.size()==12 && local.size()==12);
    open.clear();local.clear();discs={{20,10,1000,200}};
    Partition(indices,position,mask,spacing,border,discs,open,local);
    CHECK(open.empty() && local==indices);
    open.clear();local.clear();discs={{1000,1000,1,200}};
    Partition(indices,position,mask,spacing,border,discs,open,local);
    CHECK(open.size()==12 && local.size()==12);
    CacheChecks();
}
inline void CacheChecks() {
    std::vector<uint32_t> words(32*24,0);words[12*32+16]=1;
    Mask mask(words,32,24);
    const float spacing=10,border=3;
    std::vector<Point> vertices;std::vector<uint16_t> indices;
    for(unsigned i=0;i<8;++i) {
        const float gx=i%2?5.0f:16.0f,gy=i%2?4.0f:12.0f;
        for(Point p: {Point{gx,gy},Point{gx+.5f,gy},Point{gx,gy+.5f}}) {
            indices.push_back(uint16_t(vertices.size()));
            vertices.push_back({(p.x-border)*spacing,(p.y-border)*spacing});
        }
    }
    Cache cache;const std::vector<Disc> none;
    const auto first=cache.Partitioned(indices,vertices,mask,spacing,border,none,0);
    CHECK(first.open->size()==12 && first.local->size()==12);
    const uint16_t *kept=first.open->data();
    const auto again=cache.Partitioned(indices,vertices,mask,spacing,border,none,0);
    CHECK(again.open->data()==kept && *again.local==*first.local); // the same split, not rebuilt
    // Moving a sea triangle onto the shoreline is seen, however small the change.
    auto moved=vertices;for(size_t i=3;i<6;++i)moved[i]=vertices[i-3];
    const auto changed=cache.Partitioned(indices,moved,mask,spacing,border,none,0);
    CHECK(changed.open->size()==9 && changed.local->size()==15);
    // Discs only ever move triangles to the full program, and leave the kept split alone.
    const std::vector<Disc> hull={{20,10,1000,200}};
    const auto near=cache.Partitioned(indices,vertices,mask,spacing,border,hull,0);
    CHECK(near.open->empty() && near.local->size()==24);
    const auto after=cache.Partitioned(indices,vertices,mask,spacing,border,none,0);
    CHECK(after.open->size()==12 && after.local->size()==12);
    // A rebuilt mask never reuses the old field's split.
    Mask clear(std::vector<uint32_t>(32*24,0),32,24);
    const auto rebuilt=cache.Partitioned(indices,vertices,clear,spacing,border,none,0);
    CHECK(rebuilt.open->size()==24 && rebuilt.local->empty());
    // More meshes than entries still answer correctly.
    for(unsigned round=0;round<20;++round) {
        auto shifted=vertices;for(Point &p:shifted)p.y+=float(round)*1000;
        const auto split=cache.Partitioned(indices,shifted,clear,spacing,border,none,0);
        CHECK(split.open->size()==24 && split.local->empty());
    }
    // An index past the positions proves nothing, so it stays with the full program.
    std::vector<uint16_t> beyond={0,1,uint16_t(vertices.size())};
    const auto outside=cache.Partitioned(beyond,vertices,clear,spacing,border,none,0);
    CHECK(outside.open->empty() && outside.local->size()==3);
}
} // namespace WaterOpenSeaSplitTest
