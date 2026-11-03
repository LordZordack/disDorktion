#pragma once
#include "dsp/ResonantComb.h"
#include <juce_cryptography/juce_cryptography.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <complex>
#include <array>
namespace combmeasure {
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
}
namespace combmeasure
{
using namespace disdorktion;

inline juce::var controls(const ResonantCombSettings& s)
{
    auto o=object();
#define FIELD(x) put(o,#x,s.x)
    FIELD(delayMs);FIELD(feedforward);FIELD(feedback);FIELD(dampingHz);FIELD(mix);FIELD(dampingEnabled);FIELD(bypass);
#undef FIELD
    return o;
}
inline void binary(const std::filesystem::path& p,const std::vector<float>& data)
{ std::ofstream out(p,std::ios::binary);out.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()*sizeof(float)));require(bool(out)); }
inline std::vector<float> capture(double rate,ResonantCombSettings s,size_t frames,bool irregular=false)
{
    ResonantComb comb;require(comb.setParameters(s));require(comb.prepare({rate,127,1}));comb.reset();
    std::vector<float> result(frames,0);result[0]=1;float* ptr=result.data();juce::dsp::AudioBlock<float> block(&ptr,1,frames);
    size_t n=0;unsigned iteration=0;
    while(n<frames){const size_t count=std::min(frames-n,size_t(irregular?std::array<unsigned,5>{1,32,257,13,1025}[iteration++%5]:127));require(comb.process(block.getSubBlock(n,count)));n+=count;}
    return result;
}
inline std::array<ResonantCombSettings,7> cases(double rate)
{
    std::array<ResonantCombSettings,7> out;
    for(auto& s:out){s.mix=1;s.dampingEnabled=false;s.feedback=0;s.delayMs=float(1000*std::round(rate*.005)/rate);}
    out[0].feedforward=1;out[1].feedforward=-1;
    out[2].delayMs=40;out[2].feedback=.95f;out[3]=out[2];out[3].feedback=-.95f;
    out[4].delayMs=5.123f;out[4].feedback=.7f;out[4].feedforward=.3f;out[4].dampingEnabled=true;
    out[5]=out[2];out[5].dampingEnabled=true;out[5].dampingHz=20;
    out[6].delayMs=.25f;out[6].feedback=-.95f;out[6].dampingEnabled=true;out[6].dampingHz=20000;
    return out;
}
}
