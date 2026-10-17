#include "FullChainQualification.h"
using namespace disdorktion;
using namespace disdorktion::qualification;

int main(int argc,char** argv)
{
    try
    {
        if(argc!=2)throw std::runtime_error("usage: disdorktion_full_chain_measurements NEW_DIRECTORY");
        const std::filesystem::path root(argv[1]);require(!std::filesystem::exists(root));require(std::filesystem::create_directories(root));
        juce::ScopedNoDenormals noDenormals;
        auto report=object();put(report,"schema",1);put(report,"kind","full-chain-measurements");put(report,"quality","4x-fir-adaa-v1");
        put(report,"provenance",provenance(argv[0]));put(report,"listening","pending human audition");
        put(report,"sample_format","little-endian IEEE754 float32 planar lane/channel/frame");
        put(report,"policy","fixed duration; leading latency retained; no tail flush; captures include burst silence");
        juce::Array<juce::var> cases;unsigned id=0;
        for(double rate:{48000.,96000.})for(int channels:{1,2})for(unsigned scenario=0;scenario<68;++scenario)
        {
            const int kind=scenario<4?int(scenario):2,frames=scenario<4?8192:1024;
            auto s=expressive(scenario<4?0:scenario-4);auto input=source(rate,channels,frames,kind);
            unsigned latency=0;std::vector<juce::AudioBuffer<float>> lanes;lanes.push_back(input);
            auto drySettings=s;drySettings.mix=0;lanes.push_back(render(input,rate,drySettings,latency));
            FullChainPeaks peaks;lanes.push_back(render(input,rate,s,latency,false,&peaks));
            for(float mix:{.25f,.5f,.75f}){auto blend=s;blend.mix=mix;lanes.push_back(render(input,rate,blend,latency));}
            lanes.push_back(ordered(input,rate,s));auto bypass=s;bypass.bypass=true;lanes.push_back(render(input,rate,bypass,latency));
            lanes.push_back(render(input,rate,s,latency,true));drySettings.input.gain.gainDb=-24;
            lanes.push_back(render(input,rate,drySettings,latency));
            auto finalGain=s;finalGain.finalGain.gainDb=6;lanes.push_back(render(input,rate,finalGain,latency));
            const std::string filename="capture-"+std::to_string(id)+".f32";std::ofstream f(root/filename,std::ios::binary);require(bool(f));
            double peak=0;for(const auto& lane:lanes)for(int c=0;c<channels;++c)for(int n=0;n<frames;++n)
            {const float v=lane.getSample(c,n);require(std::isfinite(v));peak=std::max(peak,std::abs(double(v)));f.write(reinterpret_cast<const char*>(&v),sizeof v);}
            f.close();require(!f.fail());auto row=object();put(row,"id",int(id++));put(row,"rate",rate);put(row,"channels",channels);put(row,"frames",frames);
            put(row,"source_kind",kind);put(row,"bypass_mask",scenario<4?0:int(scenario-4));put(row,"matrix_kind",scenario<4?"sources":"bypasses");
            put(row,"settings",settings(s));put(row,"latency_samples",int(latency));put(row,"file",filename);put(row,"sha256",hash(root/filename));
            put(row,"peak_all_lanes",peak);put(row,"sample_count",juce::int64(11*channels*frames));
            auto headroom=object();put(headroom,"postInputGain",peaks.postInputGain);put(headroom,"postHighPass",peaks.postHighPass);put(headroom,"wet",peaks.wet);put(headroom,"output",peaks.output);put(row,"wet_endpoint_peaks",headroom);cases.add(row);
        }
        // The bypassed wet FIR/ADAA path measures actual delay independently of
        // both the reported integer and the dry delay line using that integer.
        juce::Array<juce::var> latencyChecks;
        for(double rate:{48000.,96000.})for(int channels:{1,2})
        {
            FullChainSettings s;s.input.gain.bypass=true;s.input.crusher.bypass=true;
            s.middle.tanh.bypass=true;s.middle.folder.bypass=true;s.output.highPass.bypass=true;s.output.lowPass.bypass=true;
            constexpr int frames=4096;const auto input=source(rate,channels,frames,0);unsigned latency=0;
            const auto wet=render(input,rate,s,latency);const std::string filename="latency-"+std::to_string(int(rate))+"-"+std::to_string(channels)+".f32";
            std::ofstream f(root/filename,std::ios::binary);require(bool(f));
            for(const auto* lane:{&input,&wet})for(int c=0;c<channels;++c)for(int n=0;n<frames;++n)
            {const float v=lane->getSample(c,n);require(std::isfinite(v));f.write(reinterpret_cast<const char*>(&v),sizeof v);}
            f.close();require(!f.fail());auto check=object();put(check,"rate",rate);put(check,"channels",channels);put(check,"frames",frames);
            put(check,"settings",settings(s));put(check,"latency_samples",int(latency));put(check,"file",filename);put(check,"sha256",hash(root/filename));
            put(check,"sample_count",2*channels*frames);put(check,"lanes","source,bypassed_wet");latencyChecks.add(check);
        }
        put(report,"latency_checks",juce::var(latencyChecks));
        put(report,"lanes","source,dry,wet,mix25,mix50,mix75,ordered,common_bypass,variable_partition,dry_changed_drive,wet_final_gain_6db");
        put(report,"cases",juce::var(cases));json(root/"result.json",report);
        std::cout<<"Wrote "<<cases.size()<<" cases; independent validator required; human audition pending\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
