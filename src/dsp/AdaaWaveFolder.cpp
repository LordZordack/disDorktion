#include "AdaaWaveFolder.h"
namespace disdorktion
{
namespace
{
double fold(double z, double t) noexcept
{
    if (std::abs(z) <= t) return z;
    const auto r = std::fmod(std::abs(z)/t, 4.0);
    const auto v = r <= 1.0 ? r : r <= 3.0 ? 2.0-r : r-4.0;
    return (std::signbit(z) ? -1.0 : 1.0)*t*v;
}
double primitive(double z, double t) noexcept
{
    const auto r = std::fmod(std::abs(z)/t, 4.0);
    const auto v = r <= 1.0 ? .5*r*r : r <= 3.0 ? 1.0-.5*(r-2.0)*(r-2.0) : .5*(r-4.0)*(r-4.0);
    return t*t*v;
}
double averageFold(double current, double previous, double t) noexcept
{
    const auto delta = current-previous;
    if (delta == 0.0) return fold(current,t);
    if (std::abs(current) <= t && std::abs(previous) <= t) return .5*(current+previous);
    // Threshold-scaled fallback never mistakes a large many-cycle jump for a small step.
    if (std::abs(delta)/t < 1e-6) return fold(.5*(current+previous),t);
    return (primitive(current,t)-primitive(previous,t))/delta;
}
}

bool AdaaWaveFolder::prepareOversampled(const juce::dsp::ProcessSpec& s,unsigned f) noexcept
{
    if (!dispatcher.prepareOversampled(s,f)) return false;
    const auto rate=s.sampleRate*f;
    drive.reset(rate,.010); threshold.reset(rate,.010); output.reset(rate,.010); bypass.reset(rate,.010);
    reset(); return true;
}
void AdaaWaveFolder::reset() noexcept
{
    drive.setCurrentAndTargetValue(driveTarget); threshold.setCurrentAndTargetValue(retained.threshold);
    output.setCurrentAndTargetValue(outputTarget); bypass.setCurrentAndTargetValue(retained.bypass ? 1. : 0.);
    previousInput.fill(0); correctionOne.fill(0); correctionTwo.fill(0);
}
bool AdaaWaveFolder::setParameters(const WaveFolderSettings& p) noexcept
{
    if (!isValidWaveFolderSettings(p)) return false;
    if (p.driveDb != retained.driveDb) driveTarget=std::pow(10.,static_cast<double>(p.driveDb)/20.);
    if (p.outputDb != retained.outputDb) outputTarget=std::pow(10.,static_cast<double>(p.outputDb)/20.);
    retained=p;
    drive.setTargetValue(driveTarget); threshold.setTargetValue(p.threshold);
    output.setTargetValue(outputTarget); bypass.setTargetValue(p.bypass ? 1. : 0.);
    return true;
}
bool AdaaWaveFolder::process(juce::dsp::AudioBlock<float> block) noexcept
{
    return dispatcher.process(block,[this](auto chunk) noexcept
    {
        for (std::size_t n=0;n<chunk.getNumSamples();++n)
        {
            const auto a=drive.getNextValue(),t=threshold.getNextValue(),g=output.getNextValue(),b=bypass.getNextValue();
            for (std::size_t c=0;c<chunk.getNumChannels();++c)
            {
                const double x=chunk.getChannelPointer(c)[n],previous=previousInput[c];
                const auto dry=.5*(x+previous);
                const auto wet=b == 1. ? 0. : g*averageFold(a*x,a*previous,t);
                const auto raw=b == 1. ? dry : b == 0. ? wet : b*dry+(1.-b)*wet;
                // Matched linear/bypass phase and a maximally-flat droop compensation.
                chunk.getChannelPointer(c)[n]=static_cast<float>(-.125*raw+1.25*correctionOne[c]-.125*correctionTwo[c]);
                correctionTwo[c]=correctionOne[c]; correctionOne[c]=raw; previousInput[c]=x;
            }
        }
    });
}
}
