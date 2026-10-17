#include "../tools/FullChainQualification.h"
#include <chrono>
#include <algorithm>
using namespace disdorktion;
using namespace disdorktion::qualification;
using Clock=std::chrono::steady_clock;
int main(int argc,char** argv)
{
    try
    {
        std::string destination,conditions;bool quick=false;
        for(int i=1;i<argc;++i){const std::string arg=argv[i];if(arg=="--diagnostic")continue;
            if(arg=="--quick"){quick=true;continue;}if((arg=="--output"||arg=="--conditions")&&i+1<argc){(arg=="--output"?destination:conditions)=argv[++i];continue;}
            throw std::runtime_error("usage: --diagnostic [--quick] --conditions TEXT --output NEW_DIRECTORY");}
        require(!destination.empty()&&!conditions.empty());std::filesystem::path root(destination);require(!std::filesystem::exists(root));require(std::filesystem::create_directories(root));
        std::ofstream raw(root/"timings.csv"),summary(root/"summary.csv");raw<<std::setprecision(17)<<"case,iteration,elapsed_ns,mix,bypass,input_gain_db,folder_drive_db\n";
        summary<<std::setprecision(17)<<"case,rate,channels,capacity,frames,scenario,count,p50_ns,p99_ns,max_ns,deadline_ns,p99_deadline_fraction,processing_ok,within_budget\n";
        juce::ScopedNoDenormals noDenormals;const int count=quick?1000:10000;int id=0;bool all=true;double sink=0;
        juce::Array<juce::var> configs;
        for(double rate:{48000.,96000.})for(int channels:{1,2})for(unsigned capacity:{32u,64u,127u,256u,1024u})
        for(const std::string scenario:{"neutral","extreme","automation","bypass","oversized","tail"})
        {
            const int frames=int(scenario=="oversized"?capacity*2+3:capacity);FullChain chain;
            auto s=scenario=="neutral"?FullChainSettings{}:expressive();if(scenario=="extreme"){s.input.gain.gainDb=24;s.middle.tanh.driveDb=36;s.middle.folder.driveDb=36;s.finalGain.gainDb=24;}
            require(chain.setParameters(s));require(chain.prepare({rate,capacity,unsigned(channels)}));
            auto fixture=source(rate,channels,frames,2);juce::AudioBuffer<float> b(channels,frames);std::vector<long long> timings(size_t(count),0);
            const auto warmupEnd=Clock::now()+std::chrono::milliseconds(quick?20:2000);
            while(Clock::now()<warmupEnd){for(int c=0;c<channels;++c)b.copyFrom(c,0,fixture,c,0,frames);require(chain.process(juce::dsp::AudioBlock<float>(b)));}
            auto config=object();put(config,"id",id);put(config,"rate",rate);put(config,"channels",channels);put(config,"capacity",int(capacity));put(config,"frames",frames);
            put(config,"scenario",scenario);put(config,"initial_settings",settings(s));put(config,"latency_samples",int(chain.getLatencySamples()));configs.add(config);
            bool ok=true;
            for(int iteration=0;iteration<count;++iteration)
            {
                for(int c=0;c<channels;++c)b.copyFrom(c,0,fixture,c,0,frames);if(scenario=="tail"&&iteration>0)b.clear();
                if(scenario=="automation"){const float t=float(iteration%101)/100;s.mix=t;s.input.gain.gainDb=-60+84*t;s.middle.folder.driveDb=-24+60*t;}
                if(scenario=="bypass"){s.bypass=bool((iteration/8)%2);s.mix=float(iteration%101)/100;}
                const auto start=Clock::now();const bool controls=chain.setParameters(s);const bool processed=chain.process(juce::dsp::AudioBlock<float>(b));
                const auto peaks=chain.getPeaks();const auto end=Clock::now();
                timings[size_t(iteration)]=std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
                ok=ok&&controls&&processed&&std::isfinite(peaks.output);sink+=peaks.output;
                for(int c=0;c<channels;++c)for(int n=0;n<frames;++n)ok=ok&&std::isfinite(b.getSample(c,n));
                raw<<id<<','<<iteration<<','<<timings[size_t(iteration)]<<','<<s.mix<<','<<int(s.bypass)<<','<<s.input.gain.gainDb<<','<<s.middle.folder.driveDb<<'\n';
            }
            std::sort(timings.begin(),timings.end());const auto p99=timings[size_t(std::ceil(.99*count))-1];const double deadline=frames/rate*1.e9;const bool budget=p99/deadline<.25;
            summary<<id++<<','<<rate<<','<<channels<<','<<capacity<<','<<frames<<','<<scenario<<','<<count<<','<<timings[size_t(count/2)]<<','<<p99<<','<<timings.back()<<','<<deadline<<','<<p99/deadline<<','<<ok<<','<<budget<<'\n';all=all&&ok&&budget;
        }
        raw.close();summary.close();require(!raw.fail()&&!summary.fail());auto report=object();put(report,"schema",1);put(report,"kind","full-chain-cpu-diagnostic");
        put(report,"reference_qualified",false);put(report,"reference_pending","three pinned AC Release runs with established adaptive windows and coverage; diagnostic has no affinity/power observations");
        put(report,"quick",quick);put(report,"conditions",conditions);put(report,"provenance",provenance(argv[0]));put(report,"cases",juce::var(configs));
        put(report,"timings_sha256",hash(root/"timings.csv"));put(report,"summary_sha256",hash(root/"summary.csv"));put(report,"processing_and_diagnostic_budget_ok",all);put(report,"sink",sink);
        std::vector<long long> overhead(1000);for(auto& duration:overhead){const auto a=Clock::now(),b=Clock::now();duration=std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count();}
        std::sort(overhead.begin(),overhead.end());put(report,"timer_only_median_ns",juce::int64(overhead[500]));put(report,"timer_overhead_subtracted",false);
        json(root/"result.json",report);std::cout<<id<<" diagnostic cases; reference qualification pending; diagnostic gates="<<all<<'\n';return 1;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
