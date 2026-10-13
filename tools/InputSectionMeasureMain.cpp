#include "dsp/InputSection.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

namespace
{
using Settings = disdorktion::InputSettings;
constexpr std::array<const char*, 5> fixtures {"sine", "transients", "high-sine", "two-tone", "sweep"};
constexpr int seconds = 8;
const std::array<Settings, seconds> controls {{
    {{0, false, false}, {64, 384000, false}},
    {{24, false, false}, {8, 384000, false}},
    {{12, false, false}, {3, 8000, false, 1, .6f, -.8f, 1, 123}},
    {{24, false, false}, {4, 4000, false, .25f, 1, .8f, 1, 123}},
    {{24, false, true}, {2, 6000, false, 1, 1, -.8f, .7f, 17}},
    {{24, false, false}, {1, 100, true, 1, 1, 1, 1, 17}},
    {{24, false, true}, {8, 100, true, 0, 1, -1, 1, 17}},
    {{24, true, false}, {8, 100, false, 1, .5f, .8f, 1, 123}}
}};
void word(std::ostream& out, std::uint32_t value, unsigned bytes)
{
    for (unsigned n=0; n<bytes; ++n) out.put(static_cast<char>((value>>(8*n))&255));
}
// IEEE float WAV retains overload evidence; integer PCM writers would clip it.
bool writeWav(const juce::File& file, const juce::AudioBuffer<float>& audio, unsigned rate)
{
    if (file.exists()) return false;
    std::ofstream out(file.getFullPathName().toStdString(), std::ios::binary);
    const auto bytes=static_cast<std::uint32_t>(audio.getNumSamples()*audio.getNumChannels()*4);
    out.write("RIFF",4); word(out,48+bytes,4); out.write("WAVEfmt ",8); word(out,16,4);
    word(out,3,2); word(out,static_cast<unsigned>(audio.getNumChannels()),2); word(out,rate,4);
    word(out,rate*static_cast<unsigned>(audio.getNumChannels())*4,4);
    word(out,static_cast<unsigned>(audio.getNumChannels())*4,2); word(out,32,2);
    out.write("fact",4); word(out,4,4); word(out,static_cast<unsigned>(audio.getNumSamples()),4);
    out.write("data",4); word(out,bytes,4);
    for (int n=0;n<audio.getNumSamples();++n) for(int c=0;c<audio.getNumChannels();++c)
    { std::uint32_t bits; const float sample=audio.getSample(c,n); std::memcpy(&bits,&sample,4); word(out,bits,4); }
    return static_cast<bool>(out);
}
juce::var aggregate(const juce::AudioBuffer<float>& audio)
{
    double peak=0, energy=0; std::int64_t overload=0;
    for(int c=0;c<audio.getNumChannels();++c) for(int n=0;n<audio.getNumSamples();++n)
    { const double v=audio.getSample(c,n); peak=std::max(peak,std::abs(v)); energy+=v*v; overload+=std::abs(v)>1; }
    auto* object=new juce::DynamicObject;
    object->setProperty("peak",peak);object->setProperty("rms",std::sqrt(energy/(audio.getNumChannels()*audio.getNumSamples())));
    object->setProperty("overloadSamples",juce::var(overload));return juce::var(object);
}
juce::var schedule(unsigned rate)
{
    juce::Array<juce::var> events;
    for(int n=0;n<seconds;++n)
    {
        auto* e=new juce::DynamicObject;const auto& s=controls[n];
        e->setProperty("frame",n*static_cast<int>(rate));e->setProperty("gainDb",s.gain.gainDb);
        e->setProperty("mute",s.gain.mute);e->setProperty("gainBypass",s.gain.bypass);
        e->setProperty("bitDepth",static_cast<int>(s.crusher.bitDepth));e->setProperty("captureRateHz",s.crusher.captureRateHz);
        e->setProperty("crusherBypass",s.crusher.bypass);e->setProperty("mix",s.crusher.mix);
        e->setProperty("crush",s.crusher.crush);e->setProperty("shape",s.crusher.shape);
        e->setProperty("jitter",s.crusher.jitter);e->setProperty("jitterSeed",static_cast<int>(s.crusher.jitterSeed));events.add(juce::var(e));
    }
    // Explicit rapid controls during the expressive segment, with sub-envelope spacing.
    for(int n=1;n<=16;++n)
    {
        auto event=events[2].clone();auto* e=event.getDynamicObject();
        e->setProperty("frame",static_cast<int>(2*rate+n*(rate/1000)));
        e->setProperty("gainDb",n%2?24:-12);e->setProperty("mix",n%2?1.0:0.2);
        e->setProperty("shape",n%2?.8:-.8);e->setProperty("bitDepth",n%2?2:16);events.add(event);
    }
    std::sort(events.begin(),events.end(),[](const auto& a,const auto& b){return static_cast<int>(a["frame"])<static_cast<int>(b["frame"]);});
    return juce::var(events);
}
Settings decode(const juce::var& e)
{
    return {{static_cast<float>(e["gainDb"]),static_cast<bool>(e["mute"]),static_cast<bool>(e["gainBypass"])},
        {static_cast<unsigned>(static_cast<int>(e["bitDepth"])),static_cast<float>(e["captureRateHz"]),static_cast<bool>(e["crusherBypass"]),
        static_cast<float>(e["mix"]),static_cast<float>(e["crush"]),static_cast<float>(e["shape"]),static_cast<float>(e["jitter"]),
        static_cast<std::uint32_t>(static_cast<int>(e["jitterSeed"]))}};
}
juce::var measure(const juce::File& directory,const char* fixture,unsigned rate,bool& passed,std::ostream& csv)
{
    const int frames=static_cast<int>(rate)*seconds;
    juce::AudioBuffer<float> source(2,frames),wet(2,frames),reference(2,frames),intermediate(2,frames);
    for(int n=0;n<frames;++n)
    {
        const double t=static_cast<double>(n)/rate;
        double phase=440*t, envelope=1;
        const std::string name=fixture;
        if(name=="high-sine") phase=6000*t;
        if(name=="sweep") phase=80*t+(12000.0-80)*t*t/(2*seconds);
        if(name=="transients") envelope=std::exp(-static_cast<double>(n%(rate/4))/1000);
        double sample=.25*envelope*std::sin(juce::MathConstants<double>::twoPi*phase);
        if(name=="two-tone") sample+=.125*std::sin(juce::MathConstants<double>::twoPi*6000*t);
        source.setSample(0,n,static_cast<float>(sample));source.setSample(1,n,static_cast<float>(-.7*sample));
    }
    wet.makeCopyOf(source);reference.makeCopyOf(source);intermediate.makeCopyOf(source);
    disdorktion::InputSection section;disdorktion::Gain gain;disdorktion::BitCrusher crusher;
    bool good=section.prepare({static_cast<double>(rate),127,2})&&gain.prepare({static_cast<double>(rate),127,2})&&crusher.prepare({static_cast<double>(rate),127,2});
    const auto events=schedule(rate); const auto* list=events.getArray();double meterError=0,compositionError=0;
    constexpr std::array<int,5> blocks {32,64,127,256,1024};std::size_t blockIndex=0;
    for(int e=0;e<list->size();++e)
    {
        const auto settings=decode((*list)[e]);good &=section.setParameters(settings)&&gain.setParameters(settings.gain)&&crusher.setParameters(settings.crusher);
        int position=static_cast<int>((*list)[e]["frame"]);
        const int end=e+1<list->size()?static_cast<int>((*list)[e+1]["frame"]):frames;
        while(position<end)
        {
            const int length=std::min(blocks[blockIndex++%blocks.size()],end-position);
            auto a=juce::dsp::AudioBlock<float>(wet).getSubBlock(position,length);
            auto b=juce::dsp::AudioBlock<float>(reference).getSubBlock(position,length);
            good &=section.process(a)&&gain.process(b);
            double peak=0;
            for(int c=0;c<2;++c) for(int n=0;n<length;++n)
            {const float value=b.getSample(c,n);intermediate.setSample(c,position+n,value);peak=std::max(peak,std::abs(static_cast<double>(value)));}
            meterError=std::max(meterError,std::abs(peak-section.getPostGainPeak()));good &=crusher.process(b);
            for(int c=0;c<2;++c)for(int n=0;n<length;++n)compositionError=std::max(compositionError,std::abs(static_cast<double>(a.getSample(c,n))-b.getSample(c,n)));
            position+=length;
        }
    }
    const juce::String prefix=juce::String(fixture)+"-"+juce::String(rate);
    for(const auto& item:std::array<std::pair<const char*,const juce::AudioBuffer<float>*>,4>{{{"source",&source},{"wet",&wet},{"reference",&reference},{"post-gain",&intermediate}}})
        good &=writeWav(directory.getChildFile(prefix+"-"+item.first+".wav"),*item.second,rate);
    auto* row=new juce::DynamicObject;row->setProperty("id",prefix);row->setProperty("fixture",fixture);row->setProperty("rateHz",static_cast<int>(rate));
    row->setProperty("frames",frames);row->setProperty("channels",2);row->setProperty("events",events);row->setProperty("source",aggregate(source));
    row->setProperty("postGain",aggregate(intermediate));row->setProperty("output",aggregate(wet));
    row->setProperty("compositionMaxError",compositionError);row->setProperty("postGainMeterMaxError",meterError);row->setProperty("latencySamples",static_cast<int>(section.getLatencySamples()));
    // Direct sinusoidal projections in the last 2048 frames of each segment.
    // These are observations of content, not a claimed alias attenuation gate.
    juce::Array<juce::var> spectra;
    for(int second=0;second<seconds;++second)for(const double frequency:{440.0,2000.0,6000.0,10000.0})
    {
        double real=0,imaginary=0; const int start=(second+1)*static_cast<int>(rate)-2048;
        for(int n=0;n<2048;++n){const double phase=juce::MathConstants<double>::twoPi*frequency*n/rate;const double v=wet.getSample(0,start+n);real+=v*std::cos(phase);imaginary-=v*std::sin(phase);}
        auto* band=new juce::DynamicObject;band->setProperty("startFrame",start);band->setProperty("frames",2048);band->setProperty("frequencyHz",frequency);
        band->setProperty("amplitude",2*std::hypot(real,imaginary)/2048);spectra.add(juce::var(band));
    }
    row->setProperty("spectralProjections",juce::var(spectra));
    good &=compositionError==0&&meterError==0&&section.getLatencySamples()==0;row->setProperty("passed",good);passed &=good;
    const auto a=aggregate(intermediate),b=aggregate(wet);
    csv<<prefix<<','<<rate<<','<<frames<<','<<static_cast<double>(a["peak"])<<','<<static_cast<double>(a["rms"])<<','<<static_cast<juce::int64>(a["overloadSamples"])<<','
        <<static_cast<double>(b["peak"])<<','<<static_cast<double>(b["rms"])<<','<<static_cast<juce::int64>(b["overloadSamples"])<<','<<compositionError<<','<<meterError<<",0,"<<(good?"true":"false")<<'\n';
    return juce::var(row);
}
}
int main(int argc,char** argv)
{
    if(argc!=2){std::cerr<<"Usage: disdorktion_input_measurements OUTPUT_DIRECTORY\n";return 2;}
    const auto requested=juce::String(argv[1]);const auto directory=juce::File::isAbsolutePath(requested)?juce::File(requested):juce::File::getCurrentWorkingDirectory().getChildFile(requested);
    if(directory.exists()){std::cerr<<"Output directory must not exist; refusing to alter previous evidence\n";return 2;}
    if(directory.createDirectory().failed())return 2;
    std::ofstream csv(directory.getChildFile("metrics.csv").getFullPathName().toStdString());csv<<std::setprecision(17);
    csv<<"id,rate_hz,frames,post_gain_peak,post_gain_rms,post_gain_overload_samples,output_peak,output_rms,output_overload_samples,composition_max_error,meter_max_error,latency_samples,passed\n";
    bool passed=true;juce::Array<juce::var> cases;
    for(unsigned rate:{48000u,96000u})for(const auto* fixture:fixtures)cases.add(measure(directory,fixture,rate,passed,csv));
    auto* report=new juce::DynamicObject;report->setProperty("schemaVersion",1);report->setProperty("cases",juce::var(cases));report->setProperty("build",DISDORKTION_BUILD_ID);
    report->setProperty("juce",juce::SystemStats::getJUCEVersion());report->setProperty("gitRevision",DISDORKTION_GIT_REVISION);
    report->setProperty("sourceIdentity","Deterministic synthetic v1; mono formula at L, R=-0.7*L; float32 stereo WAV, no normalization; duration 8 seconds");
    report->setProperty("reference","Separately prepared production Gain then BitCrusher; identical events/blocks, capacity 127; exact same-build tolerance 0");
    report->setProperty("blocks","32,64,127,256,1024 cyclic, split at exact event frames; includes oversized calls");
    report->setProperty("listening","Pending human audition; synthetic fixtures do not qualify musical sources or human listening");report->setProperty("passed",passed);
    const auto json=juce::JSON::toString(juce::var(report));if(!directory.getChildFile("result.json").replaceWithText(json)||!csv)return 2;
    std::cout<<json<<'\n';return passed?0:1;
}
