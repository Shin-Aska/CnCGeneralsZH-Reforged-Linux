#include "W3DDevice/GameClient/WeatherParticleMath.h"
#include <cstdio>
#include <cstdlib>
int main(int argc,char **argv) {
    if (argc!=4) return 2;
    const long first=std::strtol(argv[1],nullptr,10),last=std::strtol(argv[2],nullptr,10);
    const std::uint32_t seed=std::uint32_t(std::strtoul(argv[3],nullptr,0));
    if (first<1 || last<first || last>10000000) return 2;
    std::puts("logic_frame,seconds,envelope,phase");
    for (long frame=first;frame<=last;++frame) {
        const double seconds=std::floor((frame-1)*1000.0/60.0)/1000.0;
        const auto spell=WeatherParticleMath::rainSpell(seconds,seed);
        const char *phase=spell.envelope<=0 ? "dry" : seconds<spell.plateauStart ? "fade_in" : seconds>spell.plateauEnd ? "fade_out" : "wet";
        std::printf("%ld,%.9f,%.9g,%s\n",frame,seconds,spell.envelope,phase);
    }
}
