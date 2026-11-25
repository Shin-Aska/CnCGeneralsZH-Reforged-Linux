// Conservative broad phase for the existing ordered terrain CaptureScope walk.
#pragma once
#include "Platform/WaterPathTracingScene.h"
namespace WaterPathTracing {
class TerrainGroups {
public:
    struct Span {
        size_t First, End;
        bool operator==(const Span &other) const { return First==other.First && End==other.End; }
    };
    static void Append(std::vector<Span> &spans,size_t first,size_t end) {
        if (!spans.empty() && spans.back().End==first) spans.back().End=end;
        else spans.push_back({first,end});
    }
    static constexpr size_t TrianglesPerGroup=64;
    void Build(const std::vector<Triangle> &triangles) {
        Groups.clear();Groups.reserve((triangles.size()+TrianglesPerGroup-1)/TrianglesPerGroup);
        for(size_t first=0;first<triangles.size();first+=TrianglesPerGroup) {
            Group group;group.First=first;group.End=std::min(first+TrianglesPerGroup,triangles.size());
            group.Low=group.High=triangles[first].a;
            for(size_t i=first;i<group.End;++i)for(Vec3 p:{triangles[i].a,triangles[i].b,triangles[i].c}) {
                group.Low=Minimum(group.Low,p);group.High=Maximum(group.High,p);
            }
            // Allow for bounds reductions and CaptureScope's float centre/radius arithmetic.
            const Vec3 magnitude=Maximum(Maximum(Scale(group.Low,-1),group.Low),Maximum(Scale(group.High,-1),group.High));
            const Vec3 padding=Add(Scale(magnitude,32*std::numeric_limits<float>::epsilon()),{.01f,.01f,.01f});
            group.Low=Sub(group.Low,padding);group.High=Add(group.High,padding);
            Groups.push_back(group);
        }
    }
    template<class Scope> void Select(const std::vector<Triangle> &triangles,const Scope &scope,
        std::vector<Span> &spans,size_t &tested,size_t &skipped,size_t &accepted) const {
        spans.clear();tested=skipped=accepted=0;
        for(const Group &group:Groups) {
            if(!scope.Matches(group.Low,group.High)){skipped+=group.End-group.First;continue;}
            if(scope.Contains(group.Low,group.High)) {
                Append(spans,group.First,group.End);
                accepted+=group.End-group.First;
                continue;
            }
            for(size_t i=group.First;i<group.End;++i) {
                ++tested;const Triangle &t=triangles[i];
                if(scope.Matches(Minimum(t.a,Minimum(t.b,t.c)),Maximum(t.a,Maximum(t.b,t.c))))Append(spans,i,i+1);
            }
        }
    }
private:
    struct Group {Vec3 Low{},High{};size_t First=0,End=0;};
    std::vector<Group> Groups;
};
}
