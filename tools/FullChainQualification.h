#pragma once
#include "dsp/FullChain.h"
#include <juce_cryptography/juce_cryptography.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cmath>

namespace disdorktion::qualification
{
inline void require(bool value) { if (!value) throw std::runtime_error("DSP/artifact operation rejected"); }
inline juce::var object() { return juce::var(new juce::DynamicObject); }
inline void put(juce::var& o,const char* k,juce::var v) { o.getDynamicObject()->setProperty(k,v); }
inline void put(juce::var& o,const char* k,const std::string& v) { put(o,k,juce::var(juce::String(v))); }
inline void put(juce::var& o,const char* k,const char* v) { put(o,k,juce::var(v)); }
inline std::string hash(const std::filesystem::path& p)
{
    juce::File file(juce::String(p.string())); require(file.existsAsFile());
    return juce::SHA256(file).toHexString().toStdString();
}
inline void json(const std::filesystem::path& p,const juce::var& o)
{ std::ofstream f(p); require(bool(f)); f<<juce::JSON::toString(o,true).toStdString()<<'\n'; require(bool(f)); }
inline juce::var provenance(const char* executable)
{
    auto o=object();put(o,"executable_sha256",hash(std::filesystem::absolute(executable)));
    juce::Array<juce::var> files;
    const std::filesystem::path root(DISDORKTION_SOURCE_ROOT);
    for(const auto& directory:{"src/dsp","tools","benchmarks","scripts"})
        for(const auto& entry:std::filesystem::directory_iterator(root/directory))
        {
            const auto ext=entry.path().extension().string();
            if(!entry.is_regular_file() || (ext!=".h"&&ext!=".cpp"&&ext!=".py"))continue;
            auto f=object();put(f,"path",std::filesystem::relative(entry.path(),root).generic_string());put(f,"sha256",hash(entry.path()));files.add(f);
        }
    put(o,"sources",juce::var(files));put(o,"juce_version",juce::SystemStats::getJUCEVersion());
#if defined(_MSC_FULL_VER)
    put(o,"compiler","MSVC "+std::to_string(_MSC_FULL_VER));
#elif defined(__VERSION__)
    put(o,"compiler",__VERSION__);
#else
    put(o,"compiler","unknown");
#endif
#ifdef NDEBUG
    put(o,"build_configuration","Release");put(o,"ndebug",true);
#else
    put(o,"build_configuration","Debug");put(o,"ndebug",false);
#endif
    put(o,"os",juce::SystemStats::getOperatingSystemName());put(o,"cpu",juce::SystemStats::getCpuModel());
    return o;
}
inline juce::var settings(const FullChainSettings& s)
{
    auto o=object();
#define Q(field) put(o,#field,s.field)
    Q(input.gain.gainDb);Q(input.gain.mute);Q(input.gain.bypass);
    put(o,"input.crusher.bitDepth",int(s.input.crusher.bitDepth));Q(input.crusher.captureRateHz);Q(input.crusher.bypass);
    Q(input.crusher.mix);Q(input.crusher.crush);Q(input.crusher.shape);Q(input.crusher.jitter);
    put(o,"input.crusher.jitterSeed",juce::int64(s.input.crusher.jitterSeed));
    Q(middle.tanh.driveDb);Q(middle.tanh.outputDb);Q(middle.tanh.bypass);
    Q(middle.folder.driveDb);Q(middle.folder.threshold);Q(middle.folder.outputDb);Q(middle.folder.bypass);
    Q(output.highPass.cutoffHz);Q(output.highPass.bypass);Q(output.lowPass.cutoffHz);Q(output.lowPass.bypass);
    put(o,"output.highPass.response",int(s.output.highPass.response));put(o,"output.lowPass.response",int(s.output.lowPass.response));
    Q(finalGain.gainDb);Q(finalGain.mute);Q(finalGain.bypass);Q(mix);Q(bypass);
#undef Q
    return o;
}
inline FullChainSettings expressive(unsigned mask=0)
{
    FullChainSettings s;s.input.gain.gainDb=9;s.input.crusher.bitDepth=8;s.input.crusher.captureRateHz=12345.5f;
    s.input.crusher.crush=.35f;s.input.crusher.shape=.2f;s.input.crusher.jitter=.1f;
    s.middle.tanh.driveDb=12;s.middle.tanh.outputDb=3;s.middle.folder.driveDb=6;s.middle.folder.threshold=.4f;
    s.output.highPass.cutoffHz=120;s.output.lowPass.cutoffHz=8000;
    s.input.gain.bypass=bool(mask&1);s.input.crusher.bypass=bool(mask&2);s.middle.tanh.bypass=bool(mask&4);
    s.middle.folder.bypass=bool(mask&8);s.output.highPass.bypass=bool(mask&16);s.output.lowPass.bypass=bool(mask&32);
    return s;
}
inline juce::AudioBuffer<float> source(double rate,int channels,int frames,int kind)
{
    juce::AudioBuffer<float> b(channels,frames);b.clear();const double pi=juce::MathConstants<double>::pi;
    for(int c=0;c<channels;++c)for(int n=0;n<frames;++n)
    {
        double t=n/rate,x=0;
        if(kind==0)x=n==0?1.:0.;
        if(kind==1){const double duration=frames/rate,k=std::log((rate*.45)/20.);x=.35*std::sin(2*pi*20*duration/k*(std::exp(k*t/duration)-1));}
        if(kind==2)x=.3*std::sin(2*pi*997*t)+.2*std::sin(2*pi*7001*t);
        if(kind==3)x=n<frames/4?4.*std::sin(2*pi*311*t):0.;
        b.setSample(c,n,float(x*(c?-.73:1.)));
    }return b;
}
inline juce::AudioBuffer<float> render(const juce::AudioBuffer<float>& source,double rate,FullChainSettings s,unsigned& latency,bool variable=false,FullChainPeaks* peaks=nullptr)
{
    FullChain chain;require(chain.setParameters(s));require(chain.prepare({rate,127,unsigned(source.getNumChannels())}));latency=chain.getLatencySamples();
    auto out=source;juce::dsp::AudioBlock<float> block(out);size_t offset=0;unsigned iteration=0;
    while(offset<block.getNumSamples())
    {
        const size_t count=std::min(block.getNumSamples()-offset,size_t(variable?std::array<unsigned,5>{1,32,257,13,1025}[iteration++%5]:127));
        require(chain.process(block.getSubBlock(offset,count)));offset+=count;
        if(peaks){const auto p=chain.getPeaks();peaks->postInputGain=std::max(peaks->postInputGain,p.postInputGain);peaks->postHighPass=std::max(peaks->postHighPass,p.postHighPass);peaks->wet=std::max(peaks->wet,p.wet);peaks->output=std::max(peaks->output,p.output);}
    }return out;
}
inline juce::AudioBuffer<float> ordered(const juce::AudioBuffer<float>& src,double rate,const FullChainSettings& s)
{
    InputSection input;MiddleSection middle;OutputSection output;Gain gain;
    require(input.setParameters(s.input));require(middle.setParameters(s.middle));require(output.setParameters(s.output));require(gain.setParameters(s.finalGain));
    juce::dsp::ProcessSpec spec{rate,127,unsigned(src.getNumChannels())};require(input.prepare(spec));
    require(middle.prepare(spec,4,MiddleLatencyCorrection::fir,MiddleFoldAlgorithm::adaaTriangle));require(output.prepare(spec));require(gain.prepare(spec));
    auto out=src;juce::dsp::AudioBlock<float> block(out);
    for(size_t offset=0;offset<block.getNumSamples();offset+=127)
    {auto b=block.getSubBlock(offset,std::min(size_t(127),block.getNumSamples()-offset));require(input.process(b));require(middle.process(b));require(output.process(b));require(gain.process(b));}
    return out;
}
}
