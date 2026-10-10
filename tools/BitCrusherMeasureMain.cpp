#include "dsp/BitCrusher.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

namespace
{
bool writeWav(const juce::File& file, const juce::AudioBuffer<float>& audio, double rate)
{
    if (file.exists()) return false;
    auto stream=file.createOutputStream();
    if (!stream) return false;
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(),rate,
        static_cast<unsigned>(audio.getNumChannels()),32,{},0));
    if (!writer) return false;
    stream.release();
    return writer->writeFromAudioSampleBuffer(audio,0,audio.getNumSamples());
}
juce::var spectrum(unsigned hostRate, bool& passed)
{
    constexpr int order=15, count=1<<order;
    disdorktion::BitCrusher crusher;
    passed &= crusher.setParameters({64,8000,false}) && crusher.prepare({static_cast<double>(hostRate),127,1});
    juce::AudioBuffer<float> source(1,count),wet(1,count);
    for(int n=0;n<count;++n)
        source.setSample(0,n,static_cast<float>(.25*std::sin(juce::MathConstants<double>::twoPi*6000*n/hostRate)));
    wet.makeCopyOf(source);
    passed &= crusher.process(juce::dsp::AudioBlock<float>(wet));
    std::vector<float> a(2*count,0),b(2*count,0);
    const auto stride=hostRate/8000;
    double maxError=0;
    for(int n=0;n<count;++n)
    {
        const auto captured=source.getSample(0,static_cast<int>((n/stride)*stride));
        // Independent mathematical PCM reference (nominal input never clips).
        // Even a 64-bit quantizer rounds tiny floating sine zero-crossings.
        const auto reference=static_cast<float>(std::ldexp(std::round(std::ldexp(
            static_cast<double>(captured),63)),-63));
        maxError=std::max(maxError,std::abs(static_cast<double>(wet.getSample(0,n))-reference));
        // Hann window: identical independently constructed reference spectrum.
        const auto window=static_cast<float>(.5-.5*std::cos(juce::MathConstants<double>::twoPi*n/(count-1)));
        a[n]=wet.getSample(0,n)*window;b[n]=reference*window;
    }
    juce::dsp::FFT fft(order);
    fft.performFrequencyOnlyForwardTransform(a.data());fft.performFrequencyOnlyForwardTransform(b.data());
    int peak=1;
    for(int k=2;k<count/2;++k) if(a[k]>a[peak]) peak=k;
    double spectralErrorDb=0;
    const auto referencePeak=*std::max_element(b.begin()+1,b.begin()+count/2);
    for(int k=1;k<count/2;++k)
        if(b[k]>referencePeak*1e-5f)
            spectralErrorDb=std::max(spectralErrorDb,std::abs(20.0*std::log10(a[k]/b[k])));
    const auto peakHz=static_cast<double>(peak)*hostRate/count;
    const bool good=maxError==0 && std::abs(peakHz-2000)<=static_cast<double>(hostRate)/count && spectralErrorDb<=.1;
    passed &= good;
    auto* result=new juce::DynamicObject;
    result->setProperty("hostRateHz",static_cast<int>(hostRate));result->setProperty("captureRateHz",8000);
    result->setProperty("inputHz",6000);result->setProperty("bitDepth",64);
    result->setProperty("fftFrames",count);result->setProperty("window","Hann");
    result->setProperty("foldedPeakHz",peakHz);result->setProperty("maxTimeDomainError",maxError);
    result->setProperty("maxReferenceSpectralErrorDbAboveMinus100",spectralErrorDb);result->setProperty("passed",good);
    return juce::var(result);
}
bool transitions(const juce::File& directory, bool transients)
{
    constexpr int host=48000,frames=host*8;
    juce::AudioBuffer<float> source(2,frames),wet(2,frames);
    disdorktion::BitCrusher crusher;
    if(!crusher.prepare({host,127,2})) return false;
    for(int n=0;n<frames;++n)
    {
        const double t=static_cast<double>(n)/host;
        const double envelope=transients?std::exp(-static_cast<double>(n%(host/4))/1000.0):1.0;
        const auto sample=static_cast<float>(.25*envelope*(std::sin(juce::MathConstants<double>::twoPi*440*t)
            +.5*std::sin(juce::MathConstants<double>::twoPi*6000*t)));
        source.setSample(0,n,sample);source.setSample(1,n,-sample);
    }
    wet.makeCopyOf(source);
    const disdorktion::BitCrusherSettings events[] {{64,384000,false},{8,384000,false},
        {64,384000,false,1,.25f,0,0,1},{64,384000,false,1,.5f,.8f,0,1},
        {64,8000,false,1,.5f,-.8f,1,123},{64,4000,false,.3f,.8f,.8f,1,123},
        {1,8000,false,1,1,0,1,123},{64,100,true,1,.5f,.8f,1,123}};
    for(int second=0;second<8;++second)
    {
        if(!crusher.setParameters(events[second])) return false;
        auto segment=juce::dsp::AudioBlock<float>(wet).getSubBlock(second*host,host);
        if(!crusher.process(segment)) return false;
    }
    const juce::String name=transients?"transients":"sine";
    return writeWav(directory.getChildFile(name+"-source.wav"),source,host)
        && writeWav(directory.getChildFile(name+"-wet.wav"),wet,host);
}
}
int main(int argc,char** argv)
{
    if(argc!=2) {std::cerr<<"Usage: disdorktion_bit_crusher_measurements OUTPUT_DIRECTORY\n";return 2;}
    const auto directory=juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(argv[1]));
    if(directory.exists() && directory.findChildFiles(juce::File::findFilesAndDirectories,false).size()!=0)
    {std::cerr<<"Output directory must be empty\n";return 2;}
    if(directory.createDirectory().failed()) return 2;
    bool passed=true;
    juce::Array<juce::var> spectra;spectra.add(spectrum(48000,passed));spectra.add(spectrum(96000,passed));
    auto* report=new juce::DynamicObject;
    report->setProperty("spectra",juce::var(spectra));report->setProperty("latencySamples",0);
    report->setProperty("listening","Pending; generated fixtures are not listening observations");
    report->setProperty("source","Generated 440 Hz + 6 kHz tones; transient envelope every quarter second; stereo polarity inverted");
    report->setProperty("transitionSchedule","48000 Hz, one event per second; depth/rate/bypass/mix/crush/shape/jitter/seed: 64/384000/off/1/0/0/0/1; 8/384000/off/1/0/0/0/1; 64/384000/off/1/.25/0/0/1; 64/384000/off/1/.5/.8/0/1; 64/8000/off/1/.5/-.8/1/123; 64/4000/off/.3/.8/.8/1/123; 1/8000/off/1/1/0/1/123; 64/100/on/1/.5/.8/1/123");
    passed &= transitions(directory,false) && transitions(directory,true);
    report->setProperty("passed",passed);
    const auto json=juce::JSON::toString(juce::var(report));
    if(!directory.getChildFile("result.json").replaceWithText(json)) return 2;
    std::cout<<json<<'\n';return passed?0:1;
}
