#pragma once
#include "dsp/OutputSection.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace disdorktion::measurement::output
{
inline constexpr double pi = std::numbers::pi;
inline constexpr std::array rates {8000., 44100., 48000., 96000., 192000., 384000.};
inline constexpr std::array responses {FilterResponse::butterworth, FilterResponse::chebyshev1, FilterResponse::chebyshev2};
inline constexpr std::array<std::array<float, 2>, 6> cutoffs {{{20,20000},{200,8000},{1000,1000},{8000,200},{20,20},{20000,20000}}};
inline constexpr unsigned impulseFrames = 131072;
inline constexpr unsigned staticCases = 324, orderedCases = 1296;

// Independent double direct-form BLT reference, derived from the analog poles
// and transmission zeros. It shares no coefficients or state with the TPT DSP.
struct Biquad
{
    double b0,b1,b2,a1,a2,x1=0,x2=0,y1=0,y2=0;
    double next(double x) noexcept
    {
        const double y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2;
        x2=x1; x1=x; y2=y1; y1=y; return y;
    }
    std::complex<double> response(double angle) const noexcept
    {
        const auto z=std::polar(1.,-angle);
        return (b0+b1*z+b2*z*z)/(1.+a1*z+a2*z*z);
    }
};
inline Biquad biquad(bool high, FilterResponse mode, double rate, double cutoff)
{
    double n2=0,n0=1,d1=std::sqrt(2.),d0=1;
    if(mode!=FilterResponse::butterworth)
    {
        const double e=mode==FilterResponse::chebyshev1?std::sqrt(std::pow(10.,.1)-1.):1./std::sqrt(9999.);
        const double mu=std::asinh(1./e)/2.;
        auto pole=-std::sinh(std::complex<double>(mu,pi/4.));
        if(mode==FilterResponse::chebyshev2) pole=1./pole;
        const double w3=mode==FilterResponse::chebyshev1?std::sqrt((1.+1./e)/2.):std::sqrt(2./(1.+1./e));
        pole/=w3; d1=-2.*pole.real(); d0=std::norm(pole);
        if(mode==FilterResponse::chebyshev1) n0=d0*std::pow(10.,-.05);
        else {n2=.01; n0=d0;}
    }
    if(high) std::swap(n2,n0);
    const double d2=high?d0:1., dc=high?1.:d0;
    const double g=std::tan(pi*std::min(cutoff,.45*rate)/rate), scale=d2+d1*g+dc*g*g;
    return {(n2+n0*g*g)/scale,2.*(n0*g*g-n2)/scale,(n2+n0*g*g)/scale,
        2.*(dc*g*g-d2)/scale,(d2-d1*g+dc*g*g)/scale};
}
struct Cascade
{
    Biquad high,low;
    double next(double x) {return low.next(static_cast<float>(high.next(x)));}
    std::complex<double> response(double angle) const {return high.response(angle)*low.response(angle);}
};
inline Cascade cascade(const OutputSettings& settings,double rate)
{return {biquad(true,settings.highPass.response,rate,settings.highPass.cutoffHz),biquad(false,settings.lowPass.response,rate,settings.lowPass.cutoffHz)};}
inline std::complex<double> projection(const juce::AudioBuffer<float>& audio,double rate,double frequency)
{
    const auto step=std::polar(1.,-2.*pi*frequency/rate);std::complex<double> phase=1.,sum=0.;
    for(int n=0;n<audio.getNumSamples();++n){sum+=static_cast<double>(audio.getSample(0,n))*phase;phase*=step;}
    return sum;
}
inline std::vector<double> frequencies(const OutputSettings& s,double rate)
{
    const double hp=std::min(double(s.highPass.cutoffHz),.45*rate),lp=std::min(double(s.lowPass.cutoffHz),.45*rate);
    // Fixed normalized points plus both cutoffs and geometric center cover
    // narrow/equal/crossed cases without assuming any child is the section -3dB point.
    std::vector<double> f {0.,rate*.01,rate*.1,rate*.25,rate*.49,rate*.5,hp,lp,std::sqrt(hp*lp)};
    for(bool high:{true,false})
    {
        const auto mode=high?s.highPass.response:s.lowPass.response;
        if(mode!=FilterResponse::chebyshev2) continue;
        const double g=std::tan(pi*(high?hp:lp)/rate);
        const double w3=std::sqrt(2./(1.+std::sqrt(9999.))),factor=std::sqrt(2.)/w3;
        f.push_back(rate/pi*std::atan(g*(high?1./factor:factor)));
    }
    std::sort(f.begin(),f.end());f.erase(std::unique(f.begin(),f.end()),f.end());return f;
}
inline double groupDelay(const Cascade& ref,double rate,double frequency)
{
    const double angle=2*pi*frequency/rate,delta=1e-6;
    const auto a=ref.response(std::max(0.,angle-delta)),b=ref.response(std::min(pi,angle+delta));
    if(std::abs(a)<1e-9 || std::abs(b)<1e-9) return 0.;
    const double span=std::min(pi,angle+delta)-std::max(0.,angle-delta);
    return -std::arg(b*std::conj(a))/span;
}
inline float source(unsigned kind,std::uint64_t n,double rate) noexcept
{
    if(kind==0) return static_cast<float>(std::sin(2*pi*997*double(n)/rate));
    if(kind==1) return (n%static_cast<unsigned>(rate/20)<static_cast<unsigned>(rate/40))?1.f:-1.f;
    if(kind==2) return n%static_cast<unsigned>(rate/10)<static_cast<unsigned>(rate/100)?1.f:0.f;
    // Stateless avalanche hash supplies reproducible broadband noise independent
    // of block boundaries; indexing an LCG directly would instead make a ramp.
    auto v=static_cast<std::uint32_t>(n)+0x9e3779b9u;
    v=(v^(v>>16))*0x85ebca6bu;v=(v^(v>>13))*0xc2b2ae35u;v^=v>>16;
    return static_cast<float>(double(v)/2147483648.-1.);
}
}
