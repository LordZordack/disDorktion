// Isolated comb CPU collector using the established platform and wall-time methods.
#include "../tools/CombBenchmarkPlatform.h"
#include "../tools/CombMeasurementSupport.h"
#include "WallTimeSampling.h"
#include "TimingWindow.h"
#include <chrono>

int main(int argc,char** argv)
{
    using namespace combmeasure;using Clock=std::chrono::steady_clock;
    try
    {
        std::string destination,conditions;bool quick=false,diagnostic=false;int runs=3;
        for(int i=1;i<argc;++i){std::string a=argv[i];if(a=="--diagnostic"){diagnostic=true;continue;}if(a=="--quick"){quick=true;continue;}
            if((a=="--output"||a=="--conditions"||a=="--runs")&&i+1<argc){if(a=="--runs")runs=std::stoi(argv[++i]);else(a=="--output"?destination:conditions)=argv[++i];continue;}
            throw std::runtime_error("usage: --diagnostic [--quick] [--runs N] --conditions TEXT --output NEW_DIRECTORY");}
        if(diagnostic||quick){diagnostic=true;runs=1;}
        ThreadAffinity affinity;const auto platform=observePlatform(diagnostic,affinity);
        require(!destination.empty()&&!conditions.empty()&&runs>=1&&runs<=3);std::filesystem::path root(destination);require(!std::filesystem::exists(root));require(std::filesystem::create_directories(root));
        std::ofstream raw(root/"timings.csv"),summary(root/"summary.csv"),quietTrace(root/"quiet_decay.csv");raw<<"case,iteration,elapsed_ns,control_residue,bypass_target\n";
        quietTrace<<std::setprecision(17)<<"case,audio_frame,elapsed_ns,output_peak\n";
        summary<<std::setprecision(17)<<"case,run,rate,capacity,frames,scenario,count,p50_ns,p95_ns,p99_ns,max_ns,deadline_ns,p99_deadline_fraction,processing_ok,within_budget,window_seconds,warmup_seconds,probe_seconds,probe_callbacks,automation_min_count\n";
        int id=0;double sink=0;bool all=true;bool referenceEligible=!diagnostic&&!quick&&runs==3&&platform.onAC&&platform.pinned&&platform.hybridEstablished&&platform.power!="unavailable";
#ifndef NDEBUG
        referenceEligible=false;
#endif
        juce::Array<juce::var> configs;
        const std::array<const char*,8> scenarios{"neutral","maximum-feedback-delay","damping-low","damping-high","automation","bypass","oversized","quiet-decay"};
        for(int run=0;run<runs;++run)for(int rate:{48000,96000})for(unsigned capacity:{32u,64u,127u,256u,1024u})for(int scenario=0;scenario<8;++scenario)
        {
            const auto beforePower=observePlatform(true,affinity);
            const int frames=int(scenario==6?capacity*2+3:capacity);ResonantCombSettings s;s.mix=1;
            if(scenario==0){s.feedback=0;s.feedforward=0;}
            else{s.feedback=.95f;s.feedforward=1;s.delayMs=40;s.dampingEnabled=false;}
            if(scenario==2||scenario==3){s.dampingEnabled=true;s.dampingHz=scenario==2?20.f:20000.f;}
            if(scenario==7){s.delayMs=.25f;s.feedback=.7f;}
            const auto initialSettings=s;ResonantComb comb;require(comb.setParameters(s));require(comb.prepare({double(rate),capacity,2}));
            const unsigned bypassRampCallbacks=unsigned(std::ceil(.01*rate/frames));
            const unsigned bypassDwellCallbacks=bypassRampCallbacks+8;
            juce::AudioBuffer<float> buffer(2,frames),fixture(2,frames);for(int c=0;c<2;++c)for(int n=0;n<frames;++n)fixture.setSample(c,n,float(.2*std::sin(.013*n)*(c?-.73:1)));
            if(scenario==7)
            {
                buffer.clear();buffer.setSample(0,0,1);require(comb.process(juce::dsp::AudioBlock<float>(buffer)));
                for(int n=0;n<int(std::ceil(120.*rate/frames));++n)
                {
                    buffer.clear();const auto start=Clock::now();const bool processed=comb.process(juce::dsp::AudioBlock<float>(buffer));const auto finish=Clock::now();require(processed);
                    const auto frame=static_cast<long long>(n+1)*frames;
                    if(frame<=2*rate || n%std::max(1,rate/(2*frames))==0)
                    {
                        const double peak=buffer.getMagnitude(0,frames);require(std::isfinite(peak));
                        quietTrace<<id<<','<<frame<<','<<std::chrono::duration_cast<std::chrono::nanoseconds>(finish-start).count()<<','<<peak<<'\n';
                    }
                }
            }
            bool ok=true;unsigned iteration=0;std::array<unsigned,101> residues{};
            auto callback=[&](bool timed)->long long
            {
                if(scenario==7)buffer.clear();else for(int c=0;c<2;++c)buffer.copyFrom(c,0,fixture,c,0,frames);
                const unsigned residue=iteration%101;const float t=float(residue)/100;
                if(scenario==4){s.delayMs=.25f+39.75f*t;s.feedback=-.95f+1.9f*t;s.feedforward=-1+2*t;s.dampingEnabled=true;s.dampingHz=20+19980*t;s.mix=t;}
                if(scenario==5)s.bypass=bool((iteration/bypassDwellCallbacks)%2);
                const auto begin=Clock::now();const bool controls=comb.setParameters(s);const bool processed=comb.process(juce::dsp::AudioBlock<float>(buffer));const auto end=Clock::now();
                ok=ok&&controls&&processed;for(int c=0;c<2;++c)for(int n=0;n<frames;++n)ok=ok&&std::isfinite(buffer.getSample(c,n));
                sink+=buffer.getSample(0,frames-1);if(timed)++residues[residue];++iteration;
                return std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin).count();
            };
            const double warm=quick?.02:2.,probe=quick?.02:2.;auto end=Clock::now()+std::chrono::duration<double>(warm);while(Clock::now()<end)callback(false);
            unsigned probeCallbacks=0;end=Clock::now()+std::chrono::duration<double>(probe);while(Clock::now()<end){callback(false);++probeCallbacks;}
            const double seconds=quick?.05:disdorktion::benchmark::timingWindowSeconds(probeCallbacks*10/probe);
            const auto window=static_cast<long long>(seconds*1e9);disdorktion::benchmark::WallTimeSampler sampler(window);
            struct Observation{unsigned iteration,residue;long long ns;};std::vector<Observation> observations;observations.reserve(150000);
            const auto begin=Clock::now();while(true){const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-begin).count();if(elapsed>=window)break;
                const bool sample=sampler.shouldSample(elapsed);const auto index=iteration;const auto ns=callback(sample);if(sample)observations.push_back({index,index%101,ns});}
            std::array<unsigned,2> settledBypassCounts{};
            std::vector<long long> sorted;sorted.reserve(observations.size());for(const auto& o:observations)
            {
                const bool bypassTarget=scenario==5&&bool((o.iteration/bypassDwellCallbacks)%2);
                raw<<id<<','<<o.iteration<<','<<o.ns<<','<<o.residue<<','<<bypassTarget<<'\n';sorted.push_back(o.ns);
                if(scenario==5&&o.iteration%bypassDwellCallbacks>=bypassRampCallbacks)++settledBypassCounts[size_t(bypassTarget)];
            }
            require(!sorted.empty());std::sort(sorted.begin(),sorted.end());
            const auto percentile=[&](double p){return sorted[size_t(std::ceil(p*sorted.size()))-1];};const auto p99=percentile(.99);const double deadline=frames/double(rate)*1e9;const bool budget=p99/deadline<.05;all=all&&ok&&budget;
            const auto afterPower=observePlatform(true,affinity);const bool caseAC=beforePower.onAC&&afterPower.onAC&&beforePower.power==afterPower.power&&beforePower.power==platform.power;
            const bool coverage=sorted.size()>=100000&&sorted.size()<=200000&&(scenario!=4||*std::min_element(residues.begin(),residues.end())>=100)&&(scenario!=5||*std::min_element(settledBypassCounts.begin(),settledBypassCounts.end())>=100);
            referenceEligible=referenceEligible&&caseAC&&coverage&&ok&&budget;
            summary<<id<<','<<run<<','<<rate<<','<<capacity<<','<<frames<<','<<scenarios[size_t(scenario)]<<','<<sorted.size()<<','<<percentile(.5)<<','<<percentile(.95)<<','<<p99<<','<<sorted.back()<<','<<deadline<<','<<p99/deadline<<','<<ok<<','<<budget<<','<<seconds<<','<<warm<<','<<probe<<','<<probeCallbacks<<','<<*std::min_element(residues.begin(),residues.end())<<'\n';
            auto c=object();put(c,"id",id++);put(c,"run",run);put(c,"rate",rate);put(c,"capacity",int(capacity));put(c,"frames",frames);put(c,"scenario",scenarios[size_t(scenario)]);put(c,"initial_settings",controls(initialSettings));put(c,"ac_before",beforePower.onAC);put(c,"ac_after",afterPower.onAC);put(c,"power_before",beforePower.power);put(c,"power_after",afterPower.power);put(c,"reference_sampling_coverage",coverage);put(c,"bypass_dwell_callbacks",int(bypassDwellCallbacks));put(c,"settled_active_observations",int(settledBypassCounts[0]));put(c,"settled_bypass_observations",int(settledBypassCounts[1]));put(c,"quiet_preroll_audio_seconds",scenario==7?120:0);configs.add(c);
            std::cout<<id<<" "<<rate<<'/'<<capacity<<'/'<<scenarios[size_t(scenario)]<<" p99="<<p99/deadline<<'\n';
        }
        raw.close();summary.close();quietTrace.close();require(!raw.fail()&&!summary.fail()&&!quietTrace.fail());auto report=object();put(report,"schema",1);put(report,"kind",diagnostic?"resonant-comb-cpu-diagnostic":"resonant-comb-cpu-reference");put(report,"runs",runs);put(report,"quick",quick);put(report,"conditions",conditions);
        put(report,"reference_qualified",referenceEligible);put(report,"reference_pending",referenceEligible?"none":"requires three complete pinned AC Release runs, observed hybrid topology/power profile, full timing and automation coverage, p99 below5% in all rows");
        auto environment=object();put(environment,"cpu",platform.cpu);put(environment,"os",platform.os);put(environment,"sdk",platform.sdk);put(environment,"architecture",platform.architecture);put(environment,"memory",platform.memory);put(environment,"topology",platform.topology);put(environment,"affinity",platform.affinity);put(environment,"hybrid_established",platform.hybridEstablished);put(environment,"pinned",platform.pinned);put(environment,"on_ac",platform.onAC);put(environment,"power_scheme",platform.power);put(environment,"battery",platform.battery);put(environment,"thermal","sensor unavailable; ambient unconfirmed");put(report,"environment",environment);put(report,"cases",juce::var(configs));put(report,"provenance",provenance(argv[0]));
        put(report,"timings_sha256",hash(root/"timings.csv"));put(report,"summary_sha256",hash(root/"summary.csv"));put(report,"quiet_decay_sha256",hash(root/"quiet_decay.csv"));put(report,"quiet_decay_trace_method","every callback through first2s audio; then every0.5s through120s; raw process interval includes ScopedNoDenormals, no timer overhead subtraction");put(report,"processing_and_diagnostic_budget_ok",all);put(report,"sink",sink);
        std::array<long long,1000> overhead{};for(auto& n:overhead){const auto a=Clock::now(),b=Clock::now();n=std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count();}std::sort(overhead.begin(),overhead.end());put(report,"timer_only_median_ns",juce::int64(overhead[500]));put(report,"timer_overhead_subtracted",false);
        json(root/"result.json",report);return referenceEligible?0:1;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
