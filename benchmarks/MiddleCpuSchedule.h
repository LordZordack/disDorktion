#pragma once
#include "dsp/MiddleSection.h"
#include <array>
#include <cstdint>
#include <cmath>

namespace disdorktion::middlecpu
{
constexpr std::array<const char*,10> scenarios {"tanh", "fold", "cascade", "extreme", "driver", "ramps", "off-toggle", "both-off", "mix-reactivate", "oversized"};
constexpr unsigned warmupFrames = 188; // fixed 4x FIR/ADAA prepare: ceil(2*59.625)+65+3.
constexpr std::array<unsigned,7> phaseBits {1,2,4,8,16,32,64};
constexpr std::array<const char*,7> phaseNames {"fade-out","suspended","warm-up","fade-in","settled","automation","steady"};
struct Controls { MiddleSettings middle; float driver=1.f; unsigned phase=64,fixture=0,residue=0; };
inline unsigned intersect(unsigned a,unsigned b,unsigned start,unsigned end,unsigned bit) { return a<end && b>start ? bit : 0; }
inline Controls controls(unsigned scenario,std::uint64_t iteration,double rate,unsigned frames)
{
    Controls c;
    c.middle.tanh.driveDb=6.f; c.middle.folder.driveDb=6.f; c.middle.folder.threshold=.5f;
    c.middle.tanh.outputDb=c.middle.folder.outputDb=0.f;
    c.residue=unsigned(iteration%101);
    const unsigned ramp=unsigned(rate*.01), dwell=(ramp+warmupFrames+frames-1)/frames+8;
    c.fixture=unsigned((iteration/(scenario==8?dwell*3:dwell))%3);
    if(scenario==0)c.middle.folder.bypass=true;
    if(scenario==1)c.middle.tanh.bypass=true;
    if(scenario==3){c.middle.tanh.driveDb=c.middle.folder.driveDb=36.f;c.middle.folder.threshold=.05f;c.driver=4.f;}
    if(scenario==4){c.driver=1.f+3.f*float(c.residue)/100.f;c.phase=32;}
    if(scenario==5)
    {
        const float t=float(c.residue)/100.f;
        c.middle.tanh.driveDb=-24.f+60.f*t;c.middle.folder.driveDb=36.f-60.f*t;
        c.middle.folder.threshold=.05f+.95f*t;c.middle.trimDb=-60.f+72.f*t;c.phase=32;
    }
    if(scenario==6){c.middle.tanh.bypass=(iteration/dwell)%2!=0;c.middle.folder.bypass=!c.middle.tanh.bypass;c.phase=32;}
    if(scenario==7){c.middle.tanh.bypass=c.middle.folder.bypass=true;c.phase=2;}
    if(scenario==8)
    {
        const unsigned epoch=unsigned((iteration/dwell)%3),a=unsigned(iteration%dwell)*frames,b=a+frames;
        c.middle.mix=epoch==0?0.f:epoch==1?.5f:1.f;c.phase=0;
        if(epoch==0){if(iteration<dwell)c.phase=2;else{c.phase|=intersect(a,b,0,ramp,1);c.phase|=intersect(a,b,ramp,dwell*frames,2);}}
        if(epoch==1){c.phase|=intersect(a,b,0,warmupFrames,4);c.phase|=intersect(a,b,warmupFrames,warmupFrames+ramp,8);c.phase|=intersect(a,b,warmupFrames+ramp,dwell*frames,16);}
        if(epoch==2){c.phase|=intersect(a,b,0,ramp,32);c.phase|=intersect(a,b,ramp,dwell*frames,16);}
    }
    return c;
}
inline unsigned requiredPhases(unsigned scenario){return scenario==8?63u:scenario==7?2u:scenario>=4&&scenario<=6?32u:64u;}
}
