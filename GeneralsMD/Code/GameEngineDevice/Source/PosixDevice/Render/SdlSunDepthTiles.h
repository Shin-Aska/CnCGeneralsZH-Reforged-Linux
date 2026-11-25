// Private fixed-lattice occupancy/readiness for lazy static sun depth. No GPU ownership.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

class SdlSunDepthTiles
{
public:
    using Rectangle = std::array<uint32_t,4>;
    void Reset(unsigned width,unsigned height,unsigned tile,const std::vector<Rectangle> &occupied)
    {
        Width=width; Height=height; Tile=tile;
        Columns=(width+tile-1)/tile; Rows=(height+tile-1)/tile;
        Occupied.assign(size_t(Columns)*Rows,0); Initialized.assign(Occupied.size(),0);
        for(const auto &rect:occupied) ForTiles(rect,[&](size_t index) { Occupied[index]=1; });
    }
    // Cache texels read by the target's positive-stride pixel-center lattice.
    // Full touched tiles are initialized, giving pan motion a small reusable fringe.
    std::vector<Rectangle> Missing(const double offset[2],const double stride[2],unsigned width,unsigned height) const
    {
        std::vector<unsigned char> needed(Occupied.size(),0);
        if(!width || !height || !Columns || stride[0]<=0 || stride[1]<=0) return {};
        const double left=std::floor(offset[0]+.5), top=std::floor(offset[1]+.5);
        const double right=std::floor(offset[0]+(width-1)*stride[0]+.5)+1;
        const double bottom=std::floor(offset[1]+(height-1)*stride[1]+.5)+1;
        for(unsigned y=0;y<Rows;++y) for(unsigned x=0;x<Columns;++x) {
            const size_t index=size_t(y)*Columns+x;
            if(!Occupied[index] || Initialized[index]) continue;
            if(x*Tile<right && std::min(Width,(x+1)*Tile)>left &&
                y*Tile<bottom && std::min(Height,(y+1)*Tile)>top) needed[index]=1;
        }
        return Rectangles(needed);
    }
    // Call only after the command buffer writing every supplied rectangle was
    // accepted. On failed submission the caller must discard/invalidate the cache.
    void Submitted(const std::vector<Rectangle> &rectangles)
    {
        for(const auto &rect:rectangles) ForTiles(rect,[&](size_t index) { Initialized[index]=Occupied[index]; });
    }
    unsigned TileSize() const { return Tile; }
    void UpdateOccupancy(const std::vector<Rectangle> &occupied, const std::vector<Rectangle> &dirty)
    {
        for (const auto &rect : dirty)
            ForTiles(rect, [&](size_t index) { Initialized[index] = 0; });
        std::fill(Occupied.begin(), Occupied.end(), 0);
        for (const auto &rect : occupied)
            ForTiles(rect, [&](size_t index) { Occupied[index] = 1; });
        for (size_t i = 0; i < Occupied.size(); ++i)
            if (!Occupied[i]) Initialized[i] = 0;
    }
    std::vector<Rectangle> Ready() const { return Rectangles(Initialized); }
    void Invalidate() { std::fill(Initialized.begin(),Initialized.end(),0); }
private:
    unsigned Width=0,Height=0,Tile=1,Columns=0,Rows=0;
    std::vector<unsigned char> Occupied,Initialized;
    template<class F> void ForTiles(const Rectangle &rect,F &&visit)
    {
        const unsigned right=std::min(Columns,(rect[0]+rect[2]+Tile-1)/Tile);
        const unsigned bottom=std::min(Rows,(rect[1]+rect[3]+Tile-1)/Tile);
        for(unsigned y=rect[1]/Tile;y<bottom;++y) for(unsigned x=rect[0]/Tile;x<right;++x)
            visit(size_t(y)*Columns+x);
    }
    std::vector<Rectangle> Rectangles(const std::vector<unsigned char> &cells) const
    {
        std::vector<Rectangle> result;
        std::map<std::pair<unsigned,unsigned>,size_t> previous;
        for(unsigned y=0;y<Rows;++y) {
            std::map<std::pair<unsigned,unsigned>,size_t> current;
            for(unsigned x=0;x<Columns;) {
                if(!cells[size_t(y)*Columns+x]) { ++x; continue; }
                const unsigned left=x; while(x<Columns && cells[size_t(y)*Columns+x]) ++x;
                const unsigned px=left*Tile,pw=std::min(Width,x*Tile)-px;
                const auto key=std::make_pair(px,pw); const auto found=previous.find(key);
                size_t index;
                if(found!=previous.end()) { index=found->second; result[index][3]+=std::min(Tile,Height-y*Tile); }
                else { index=result.size(); result.push_back({px,y*Tile,pw,std::min(Tile,Height-y*Tile)}); }
                current.emplace(key,index);
            }
            previous.swap(current);
        }
        return result;
    }
};
