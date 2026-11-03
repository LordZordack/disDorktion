#pragma once
#include "BlockDispatcher.h"
#include <array>
#include <algorithm>

namespace disdorktion
{
struct FrequencyDispersionSettings
{
    float frequencyHz = 500.f;
    float mix = 1.f;
    bool bypass = false;
};
inline bool isValidFrequencyDispersionSettings(const FrequencyDispersionSettings& s) noexcept
{
    return std::isfinite(s.frequencyHz) && s.frequencyHz>=20.f && s.frequencyHz<=20000.f
        && std::isfinite(s.mix) && s.mix>=0.f && s.mix<=1.f;
}

// A fixed 32-section normalized lattice. Each section obeys
// y=a*x+g*s, s'=g*x-a*s, g=sqrt(1-a*a), and has static
// A(z)=(a+z^-1)/(1+a*z^-1). Its matrix is orthogonal even during
// automation. With |a|<=r=.99, state invariant |s|<=sqrt199*X
// implies |y|<=sqrt200*X, for any bounded per-section input X.
// Cascading gives |wet|<=200^16*X from reset, also under automation.
// Input magnitude <=16 keeps that conservative bound below float maximum.
// No limiter, normalization, allocation or shared channel audio history.
class FrequencyDispersion
{
public:
    static constexpr unsigned stages = 32;
    bool prepare(const juce::dsp::ProcessSpec& spec) noexcept
    {
        if (!isValidProcessSpec(spec)) return false;
        dispatcher.prepare(spec);
        sampleRate=spec.sampleRate;
        prepared=true;
        coefficient.reset(sampleRate,.100);
        mix.reset(sampleRate,.010);
        bypass.reset(sampleRate,.010);
        reset();
        return true;
    }
    void reset() noexcept
    {
        for(auto& channel:history) channel.fill(0.);
        coefficient.setCurrentAndTargetValue(getCoefficient());
        mix.setCurrentAndTargetValue(retained.mix);
        bypass.setCurrentAndTargetValue(retained.bypass?1.:0.);
    }
    bool setParameters(const FrequencyDispersionSettings& s) noexcept
    {
        if (!isValidFrequencyDispersionSettings(s)) return false;
        retained=s;
        coefficient.setTargetValue(getCoefficient());
        mix.setTargetValue(s.mix);
        bypass.setTargetValue(s.bypass?1.:0.);
        return true;
    }
    bool process(juce::dsp::AudioBlock<float> block) noexcept
    {
        const juce::ScopedNoDenormals noDenormals;
        return dispatcher.process(block,[this](juce::dsp::AudioBlock<float> chunk) noexcept
        {
            for(std::size_t n=0;n<chunk.getNumSamples();++n)
            {
                const double a=coefficient.getNextValue();
                const double g=std::sqrt(std::max(0.,1.-a*a));
                const double wetWeight=mix.getNextValue()*(1.-bypass.getNextValue());
                for(std::size_t c=0;c<chunk.getNumChannels();++c)
                {
                    auto* samples=chunk.getChannelPointer(c);
                    const float dry=samples[n];
                    double x=dry;
                    for(auto& state:history[c])
                    {
                        const double y=a*x+g*state;
                        state=g*x-a*state;
                        x=y;
                    }
                    samples[n]=wetWeight==0.?dry:static_cast<float>(wetWeight==1.?x:dry+wetWeight*(x-dry));
                }
            }
        });
    }
    unsigned int getLatencySamples() const noexcept { return 0; }
    // Retained target, rather than the in-flight smoother coefficient.
    double getCoefficient() const noexcept
    {
        const double effective=std::min(double(retained.frequencyHz),.45*sampleRate);
        const double t=std::tan(pi*effective/sampleRate);
        return std::clamp((t-1.)/(t+1.),-.99,.99);
    }
    double getEffectiveFrequencyHz() const noexcept
    {
        const double a=getCoefficient();
        return sampleRate/pi*std::atan((1.+a)/(1.-a));
    }
    // Static retained-control unit-impulse amplitude bound below 1e-6.
    // Not a bound for arbitrary driven state or ongoing control automation.
    // On |z|=rho>q, |A(z)^32|<=[(1-q*rho)/(rho-q)]^32=C;
    // Cauchy gives |h[n]|<=C*rho^n. This is deliberately conservative.
    double getTailSeconds() const noexcept
    {
        if (!prepared) return 0.;
        const double q=std::abs(getCoefficient());
        if(q<1e-15) return double(stages+1)/sampleRate;
        const double rho=(1.+q)*.5;
        const double logC=double(stages)*std::log((1.-q*rho)/(rho-q));
        return (std::ceil((std::log(1e-6)-logC)/std::log(rho))+1.)/sampleRate;
    }
private:
    static constexpr double pi=3.1415926535897932384626433832795;
    using Smoother=juce::SmoothedValue<double,juce::ValueSmoothingTypes::Linear>;
    BlockDispatcher dispatcher;
    FrequencyDispersionSettings retained;
    std::array<std::array<double,stages>,2> history{};
    Smoother coefficient{0.},mix{1.},bypass{0.};
    double sampleRate=48000.;
    bool prepared=false;
};
}
