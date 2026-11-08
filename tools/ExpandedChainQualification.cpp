#include "CombBenchmarkPlatform.h"
#include "FullChainQualification.h"
#include "ExpandedProcessPolicy.h"
#include "dsp/FullChainControls.h"
#include "../benchmarks/ExpandedTimingWindow.h"
#include <chrono>

using namespace disdorktion;
using namespace disdorktion::qualification;
using Clock = std::chrono::steady_clock;

namespace
{
juce::var allSettings(const FullChainSettings& s)
{
    auto result = settings(s);
    for (const auto& control : fullChainControls)
        put(result, control.id, getFullChainControl(s, control.id));
    put(result, "output.comb.delayMs", s.output.comb.delayMs);
    put(result, "input.crusher.jitterSeed", juce::int64(s.input.crusher.jitterSeed));
    return result;
}
FullChainSettings activeSettings()
{
    auto s = expressive();
    s.output.comb = {40.f, 1.f, .95f, 20.f, 1.f, false, false};
    s.output.dispersion = {20.f, 1.f, false};
    s.output.tone = {6.f, false};
    s.output.highPass = {20.f, false, FilterResponse::chebyshev1};
    s.output.lowPass = {20000.f, false, FilterResponse::chebyshev2};
    return s;
}
void automate(FullChainSettings& s, unsigned iteration, unsigned dwell)
{
    const float t = float(iteration % 101) / 100.f;
    s.output.comb.delayMs = .25f + 39.75f*t;
    s.output.comb.feedback = -.95f + 1.9f*t;
    s.output.comb.feedforward = -1.f + 2.f*t;
    s.output.comb.dampingHz = 20.f + 19980.f*t;
    s.output.comb.dampingEnabled = iteration % 2 != 0;
    s.output.dispersion.frequencyHz = 20.f + 19980.f*t;
    s.output.tone.toneDb = -6.f + 12.f*t;
    s.output.comb.bypass = (iteration/dwell)%2 != 0;
    s.output.dispersion.bypass = (iteration/dwell)%3 == 1;
    s.output.tone.bypass = (iteration/dwell)%5 == 1;
    s.mix = t;
}
bool finite(const juce::AudioBuffer<float>& b)
{
    for(int c=0;c<b.getNumChannels();++c)
        for(int n=0;n<b.getNumSamples();++n)
            if (!std::isfinite(b.getSample(c,n))) return false;
    return true;
}
int stress(const std::filesystem::path& root, const char* executable)
{
    juce::Array<juce::var> cases; bool passed = true;
    juce::ScopedNoDenormals noDenormals;
    for(double rate : {8000.,44100.,48000.,96000.,192000.,384000.})
        for(float feedback : {-.95f,.95f}) for(bool dynamic : {false,true}) for(bool upstreamBypassed : {false,true})
    {
        auto s=activeSettings(); s.output.comb.feedback=feedback;
        s.input.gain.gainDb=24; s.middle.tanh.driveDb=36;
        s.middle.folder.driveDb=36; s.middle.tanh.outputDb=12;
        s.middle.folder.outputDb=12; s.finalGain.gainDb=0;
        if(upstreamBypassed) { s.input.crusher.bypass=true; s.middle.tanh.bypass=true; s.middle.folder.bypass=true; }
        const auto settledSettings=s;
        auto initial=allSettings(s); FullChain chain;
        require(chain.setParameters(s)); require(chain.prepare({rate,127,2}));
        juce::AudioBuffer<float> b(2,257); double peak=0.,residual=0.; bool ok=true,isolated=true;
        unsigned event=0; const auto total=unsigned(21*rate);bool controlsSettled=!dynamic;
        const auto name="stress-"+std::to_string(cases.size())+".f32";
        std::ofstream capture(root/name,std::ios::binary);std::array<float,514> interleaved{};
        for(unsigned offset=0;offset<total;)
        {
            const unsigned count=std::min(total-offset,std::array<unsigned,5>{1,32,257,13,127}[event%5]);
            auto chunk=juce::dsp::AudioBlock<float>(b).getSubBlock(0,count); b.clear();
            for(unsigned n=0;n<count&&offset+n<unsigned(rate);++n)
                b.setSample(0,int(n),float(.75*std::sin(2*juce::MathConstants<double>::pi*(feedback>0?25.:12.5)*(offset+n)/rate)));
            // Automation only during excitation. Settle controls before timing decay.
            if(dynamic && offset<unsigned(rate)) automate(s,event,17);
            else if(!controlsSettled) { s=settledSettings; controlsSettled=true; }
            require(chain.setParameters(s)); require(chain.process(chunk));
            for(unsigned n=0;n<count;++n)
            {
                const auto value=b.getSample(0,int(n)); ok &= std::isfinite(value);
                isolated &= b.getSample(1,int(n))==0.f; peak=std::max(peak,std::abs(double(value)));
                interleaved[2*n]=value;interleaved[2*n+1]=b.getSample(1,int(n));
                if(offset+n>=total-unsigned(rate/10)) residual=std::max(residual,std::abs(double(value)));
            }
            capture.write(reinterpret_cast<const char*>(interleaved.data()),std::streamsize(count*2*sizeof(float)));
            offset+=count; ++event;
        }
        capture.close();require(!capture.fail());
        // Larger-than-nominal upstream drive is a stress case; retain its measured
        // residual rather than silently applying the nominal unit-excitation gate.
        const bool bounded=peak<8192.; passed &= ok&&isolated&&bounded&&chain.getLatencySamples()==92;
        auto row=object();put(row,"rate",rate);put(row,"feedback",feedback);put(row,"dynamic",dynamic);
        put(row,"initial_settings",initial);put(row,"settled_settings",allSettings(s));
        put(row,"upstream_bypassed",upstreamBypassed);put(row,"frames",int(total));
        put(row,"capture_file",name);put(row,"capture_sha256",hash(root/name));put(row,"layout","stereo-interleaved-float32-le");
        put(row,"finite",ok);put(row,"channel_isolated",isolated);put(row,"peak",peak);
        put(row,"peak_limit",8192);put(row,"residual_after_20s",residual);put(row,"latency_samples",int(chain.getLatencySamples()));
        cases.add(row); std::cout<<rate<<" feedback="<<feedback<<" dynamic="<<dynamic<<" upstreamBypassed="<<upstreamBypassed<<" peak="<<peak<<" residual="<<residual<<std::endl;
    }
    auto result=object();put(result,"kind","expanded-chain-stress");put(result,"passed",passed);
    put(result,"cases",juce::var(cases));put(result,"provenance",provenance(executable));
    put(result,"tail_note","20s residual is measured for overdriven excitation; nominal <=1e-6 gate remains in OutputSectionTests and is not relaxed");
    json(root/"stress.json",result); return passed?0:1;
}
}

int main(int argc,char** argv)
{
    try
    {
        std::string output,conditions; bool quick=false,diagnostic=false,stressMode=false,failFast=false,highQos=false;
        for(int i=1;i<argc;++i)
        {
            const std::string arg=argv[i];
            if(arg=="--quick")quick=true;
            else if(arg=="--diagnostic")diagnostic=true;
            else if(arg=="--stress")stressMode=true;
            else if(arg=="--fail-fast")failFast=true;
            else if(arg=="--high-qos")highQos=true;
            else if((arg=="--output"||arg=="--conditions")&&i+1<argc)(arg=="--output"?output:conditions)=argv[++i];
            else throw std::runtime_error("usage: [--stress|--diagnostic|--quick] [--high-qos (CPU only)] --output NEW_DIRECTORY --conditions TEXT");
        }
        require(!output.empty()&&!conditions.empty()&&!std::filesystem::exists(output));
        require(!stressMode||!highQos);
        std::filesystem::create_directories(output); const std::filesystem::path root(output);
        if(stressMode)return stress(root,argv[0]);
        diagnostic |= quick; const int runs=diagnostic?1:3;
        combmeasure::ThreadAffinity affinity;const auto platform=combmeasure::observePlatform(diagnostic,affinity);
        processpolicy::ScopedProcessPolicy processPolicy(highQos);
        const char* methodId=highQos?"windows-explicit-highqos-v1":"windows-system-managed-v2";
        const bool affinityVerified=processpolicy::selectedAffinityMatches(processPolicy.appliedSnapshot(),platform);
        bool eligible=!diagnostic&&platform.onAC&&platform.pinned&&platform.hybridEstablished&&platform.power!="unavailable";
        eligible &= !highQos||(processPolicy.applicationOk()&&affinityVerified);
#ifndef NDEBUG
        eligible=false;
#endif
        const auto sourceIdentity=provenance(argv[0]);
        auto session=object();put(session,"provenance",sourceIdentity);put(session,"conditions",conditions);
        put(session,"method_id",methodId);put(session,"process_policy",processPolicy.metadata());put(session,"affinity_verified",affinityVerified);
        put(session,"scheduling_before",processPolicy.beforeRequest());put(session,"scheduling_after",processPolicy.appliedSnapshot());
        put(session,"pinned",platform.pinned);put(session,"hybrid_established",platform.hybridEstablished);put(session,"on_ac",platform.onAC);
        put(session,"affinity",platform.affinity);put(session,"topology",platform.topology);put(session,"power",platform.power);put(session,"sdk",platform.sdk);
        put(session,"complete",false);put(session,"reference_qualified",false);json(root/"session.json",session);
        std::ofstream raw(root/"timings.csv"),summary(root/"summary.csv");
        raw<<"case,iteration,elapsed_ns,residue,bypass\n";
        summary<<std::setprecision(17)<<"case,run,rate,capacity,frames,scenario,count,p99_ns,deadline_ns,fraction,processing_ok,coverage_ok,power_stable,window_seconds\n";
        int id=0;double sink=0.;bool all=true,stoppedEarly=false;juce::Array<juce::var> rows;
        const std::array<const char*,6> scenarios{"warm-bypass","active-static","automation","bypass","oversized","quiet-decay"};
        for(int run=0;run<runs;++run)for(double rate:{48000.,96000.})for(unsigned capacity:{32u,64u,127u,256u,1024u})for(int scenario=0;scenario<6;++scenario)
        {
            const auto before=combmeasure::observePlatform(true,affinity);
            const auto schedulingBefore=processpolicy::observe();
            auto s=activeSettings();if(scenario==0)s.output.comb.bypass=s.output.dispersion.bypass=s.output.tone.bypass=true;
            const auto initial=allSettings(s);FullChain chain;require(chain.setParameters(s));require(chain.prepare({rate,capacity,2}));
            const bool latencyOk=chain.getLatencySamples()==92;
            const unsigned frames=scenario==4?2*capacity+3:capacity;
            juce::AudioBuffer<float> b(2,int(frames)),fixture(2,int(frames));
            for(int c=0;c<2;++c)for(unsigned n=0;n<frames;++n)fixture.setSample(c,int(n),float(.3*std::sin(.013*n)*(c?-.73:1)));
            const unsigned ramp=unsigned(std::ceil(.01*rate/frames)),dwell=ramp+8;
            unsigned iteration=0;bool ok=latencyOk;std::array<unsigned,101> residues{};std::array<unsigned,2> settled{};std::array<unsigned,4> decayPhases{};
            juce::ScopedNoDenormals noDenormals;
            if(scenario==5)
            {
                // Drive resonance for one audio second, then retain a 30s raw
                // decay trace. Timing includes control application and peak scans.
                std::ofstream decay(root/("decay-"+std::to_string(id)+".csv"));decay<<"frame,elapsed_ns,peak\n";
                for(unsigned offset=0;offset<unsigned(31*rate);offset+=frames)
                {
                    b.clear();if(offset<unsigned(rate))for(int c=0;c<2;++c)b.copyFrom(c,0,fixture,c,0,int(frames));
                    const auto start=Clock::now();require(chain.setParameters(s));require(chain.process(juce::dsp::AudioBlock<float>(b)));const auto peaks=chain.getPeaks();const auto stop=Clock::now();
                    ok &= finite(b);decay<<offset<<','<<std::chrono::duration_cast<std::chrono::nanoseconds>(stop-start).count()<<','<<std::setprecision(17)<<peaks.output<<'\n';
                }
                decay.close();require(!decay.fail());
            }
            auto callback=[&](bool sample)
            {
                const auto cycleFrame=(std::uint64_t(iteration)*frames)%std::uint64_t(31*rate);
                const unsigned decayPhase=cycleFrame<unsigned(rate)?0u:cycleFrame<unsigned(2*rate)?1u:cycleFrame<unsigned(20*rate)?2u:3u;
                if(scenario==5&&cycleFrame>=unsigned(rate))b.clear();else for(int c=0;c<2;++c)b.copyFrom(c,0,fixture,c,0,int(frames));
                const unsigned residue=iteration%101;if(scenario==2)automate(s,iteration,dwell);
                if(scenario==3){s.output.comb.bypass=s.output.dispersion.bypass=s.output.tone.bypass=(iteration/dwell)%2!=0;s.bypass=(iteration/(dwell*2))%2!=0;}
                const auto start=Clock::now();const bool controls=chain.setParameters(s),processed=chain.process(juce::dsp::AudioBlock<float>(b));const auto peaks=chain.getPeaks();const auto stop=Clock::now();
                ok &= controls&&processed&&finite(b)&&std::isfinite(peaks.output);sink+=peaks.output;
                if(sample){++residues[residue];if(iteration%dwell>=ramp)++settled[size_t(s.output.comb.bypass)];if(scenario==5)++decayPhases[decayPhase];}
                ++iteration;return std::chrono::duration_cast<std::chrono::nanoseconds>(stop-start).count();
            };
            const double warm=quick?.02:2.,probe=quick?.02:2.;auto stop=Clock::now()+std::chrono::duration<double>(warm);while(Clock::now()<stop)callback(false);
            unsigned probes=0;stop=Clock::now()+std::chrono::duration<double>(probe);while(Clock::now()<stop){callback(false);++probes;}
            const double seconds=quick?.05:benchmark::expandedTimingWindowSeconds(probes/probe);
            const auto window=static_cast<long long>(seconds*1e9);benchmark::ExpandedWallTimeSampler sampler(window);
            struct Observation{unsigned iteration;long long ns;bool bypass;};std::vector<Observation> observations;observations.reserve(150000);
            const auto begin=Clock::now();while(true){const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-begin).count();if(elapsed>=window)break;const bool sample=sampler.shouldSample(elapsed);const auto index=iteration;const auto ns=callback(sample);if(sample)observations.push_back({index,ns,s.output.comb.bypass});}
            std::vector<long long> sorted;sorted.reserve(observations.size());for(const auto& o:observations){raw<<id<<','<<o.iteration<<','<<o.ns<<','<<o.iteration%101<<','<<o.bypass<<'\n';sorted.push_back(o.ns);}
            require(!sorted.empty());std::sort(sorted.begin(),sorted.end());const auto p99=sorted[size_t(std::ceil(.99*sorted.size()))-1];const double deadline=frames/rate*1e9;
            const bool budget=p99/deadline<.25,coverage=sorted.size()>=100000&&sorted.size()<=200000&&(scenario!=2||*std::min_element(residues.begin(),residues.end())>=100)&&(scenario!=3||std::min(settled[0],settled[1])>=100)&&(scenario!=5||*std::min_element(decayPhases.begin(),decayPhases.end())>=100);
            const auto after=combmeasure::observePlatform(true,affinity);const bool power=before.onAC&&after.onAC&&before.power==after.power&&before.power==platform.power;
            const auto schedulingAfter=processpolicy::observe();
            const bool policyOk=processPolicy.caseOk(schedulingBefore,schedulingAfter);
            eligible &= ok&&coverage&&power&&budget&&policyOk;all &= ok&&budget;
            summary<<id<<','<<run<<','<<rate<<','<<capacity<<','<<frames<<','<<scenarios[size_t(scenario)]<<','<<sorted.size()<<','<<p99<<','<<deadline<<','<<p99/deadline<<','<<ok<<','<<coverage<<','<<power<<','<<seconds<<'\n';
            auto row=object();put(row,"id",id);put(row,"run",run);put(row,"rate",rate);put(row,"capacity",int(capacity));put(row,"frames",int(frames));put(row,"scenario",scenarios[size_t(scenario)]);put(row,"initial_settings",initial);put(row,"dwell",int(dwell));put(row,"ramp",int(ramp));put(row,"warmup_seconds",warm);put(row,"probe_seconds",probe);put(row,"probe_callbacks",int(probes));put(row,"window_seconds",seconds);put(row,"ac_before",before.onAC);put(row,"ac_after",after.onAC);put(row,"power_before",before.power);put(row,"power_after",after.power);
            put(row,"latency_samples",int(chain.getLatencySamples()));put(row,"count",int(sorted.size()));put(row,"p99_fraction",p99/deadline);put(row,"coverage_ok",coverage);put(row,"processing_ok",ok);put(row,"power_stable",power);
            put(row,"method_id",methodId);put(row,"scheduling_before",schedulingBefore);put(row,"scheduling_after",schedulingAfter);put(row,"policy_ok",policyOk);
            if(scenario==5){const auto name="decay-"+std::to_string(id)+".csv";put(row,"decay_file",name);put(row,"decay_sha256",hash(root/name));}
            raw.flush();summary.flush();require(!raw.fail()&&!summary.fail());json(root/("case-"+std::to_string(id)+".json"),row);
            rows.add(row);++id;std::cout<<id<<'/'<<runs*60<<" "<<rate<<'/'<<capacity<<'/'<<scenarios[size_t(scenario)]<<" p99="<<p99/deadline<<" count="<<sorted.size()<<" coverage="<<coverage<<" power="<<power<<std::endl;
            if(failFast&&!diagnostic&&(!eligible||!ok||!coverage||!power||!budget||!policyOk)){stoppedEarly=true;goto finishCollection;}
        }
finishCollection:
        raw.close();summary.close();require(!raw.fail()&&!summary.fail());
        const auto schedulingAfterCollection=processpolicy::observe();
        const bool collectionPolicyOk=processPolicy.caseOk(processPolicy.appliedSnapshot(),schedulingAfterCollection);
        processPolicy.restore();eligible &= collectionPolicyOk&&processPolicy.restorationOk();
        auto report=object();
        put(report,"method_id",methodId);put(report,"process_policy",processPolicy.metadata());put(report,"affinity_verified",affinityVerified);
        put(report,"collection_policy_ok",collectionPolicyOk);
        put(report,"scheduling_before",processPolicy.beforeRequest());put(report,"scheduling_after",schedulingAfterCollection);put(report,"scheduling_after_restore",processPolicy.restoredSnapshot());
        put(report,"kind","expanded-chain-cpu");put(report,"complete",!stoppedEarly);put(report,"reference_qualified",eligible&&!stoppedEarly);put(report,"processing_and_budget_ok",all);put(report,"runs",runs);put(report,"quick",quick);put(report,"diagnostic",diagnostic);put(report,"conditions",conditions);put(report,"cases",juce::var(rows));put(report,"provenance",sourceIdentity);
        put(report,"pinned",platform.pinned);put(report,"hybrid_established",platform.hybridEstablished);put(report,"on_ac",platform.onAC);put(report,"affinity",platform.affinity);put(report,"topology",platform.topology);put(report,"power",platform.power);put(report,"sdk",platform.sdk);put(report,"thermal","sensor unavailable; ambient unconfirmed");put(report,"sink",sink);
        put(report,"timings_sha256",hash(root/"timings.csv"));put(report,"summary_sha256",hash(root/"summary.csv"));put(report,"timer_overhead_subtracted",false);
        std::array<long long,1000> overhead{};for(auto& n:overhead){const auto a=Clock::now(),b=Clock::now();n=std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count();}std::sort(overhead.begin(),overhead.end());put(report,"timer_only_median_ns",juce::int64(overhead[500]));
        json(root/"result.json",report);return eligible&&!stoppedEarly?0:1;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
