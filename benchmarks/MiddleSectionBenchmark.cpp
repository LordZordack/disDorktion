#include "../tools/ExpandedProcessPolicy.h"
#include "ExpandedTimingWindow.h"
#include "MiddleCpuSchedule.h"
#include <chrono>
#include <map>
#include <limits>

using namespace disdorktion;
using namespace disdorktion::qualification;
using Clock=std::chrono::steady_clock;
namespace
{
struct Options { std::string output,conditions;bool quick=false,diagnostic=false,highQos=false,capture=false,selfTest=false;int selected=-1; };
Options parse(int argc,char** argv)
{
    Options o;
    for(int i=1;i<argc;++i)
    {
        const std::string arg=argv[i];
        if(arg=="--quick")o.quick=true;
        else if(arg=="--diagnostic")o.diagnostic=true;
        else if(arg=="--high-qos")o.highQos=true;
        else if(arg=="--capture-only")o.capture=true;
        else if(arg=="--self-test")o.selfTest=true;
        else if((arg=="--output"||arg=="--conditions"||arg=="--case-id")&&i+1<argc)
        {
            const std::string value=argv[++i];
            if(arg=="--output")o.output=value;
            else if(arg=="--conditions")o.conditions=value;
            else{std::size_t used=0;o.selected=std::stoi(value,&used);require(used==value.size()&&o.selected>=0&&o.selected<200);}
        }
        else throw std::runtime_error("usage: --output NEW_DIRECTORY --conditions TEXT [--high-qos] [--diagnostic|--quick] [--case-id 0..199] [--capture-only] [--self-test]");
    }
    if(!o.selfTest)require(!o.output.empty()&&!o.conditions.empty()&&!std::filesystem::exists(o.output));
    o.diagnostic|=o.quick||o.selected>=0||!o.highQos;return o;
}
std::string gitText(const juce::StringArray& args)
{
    juce::ChildProcess child;require(child.start(args));require(child.waitForProcessToFinish(30000));
    const auto text=child.readAllProcessOutput().trim().toStdString();require(child.getExitCode()==0);return text;
}
juce::var identity(const char* exe)
{
    auto p=provenance(exe);
    put(p,"git_head",gitText({"git","-C",DISDORKTION_SOURCE_ROOT,"rev-parse","HEAD"}));
    put(p,"git_dirty",gitText({"git","-C",DISDORKTION_SOURCE_ROOT,"status","--porcelain","--untracked-files=no"}));
    put(p,"cmake_cache_sha256",hash(DISDORKTION_CMAKE_CACHE));
    put(p,"cmake_cache_path",DISDORKTION_CMAKE_CACHE);
    juce::Array<juce::var> extras;
    for(const auto* name:{"CMakeLists.txt","tests/test_middle_cpu_validator.py"})
    {
        auto f=object();put(f,"path",name);put(f,"sha256",hash(std::filesystem::path(DISDORKTION_SOURCE_ROOT)/name));extras.add(f);
    }
    put(p,"extra_sources",juce::var(extras));return p;
}
bool sameIdentity(const juce::var& a,const juce::var& b)
{ return juce::JSON::toString(a,true)==juce::JSON::toString(b,true); }
struct AffinityRestoration
{
    juce::var before=processpolicy::observe(),restored;
    bool attempted=false,succeeded=false;
#if defined(_WIN32)
    GROUP_AFFINITY saved{};
    bool captured=GetThreadGroupAffinity(GetCurrentThread(),&saved)!=0;
#else
    bool captured=false;
#endif
    void restore()
    {
        attempted=true;
#if defined(_WIN32)
        succeeded=captured&&SetThreadGroupAffinity(GetCurrentThread(),&saved,nullptr)!=0;
#endif
        restored=processpolicy::observe();
    }
    bool ok()const{return attempted&&succeeded&&processpolicy::sameAffinity(before,restored);}
    ~AffinityRestoration()
    {
#if defined(_WIN32)
        if(captured&&!succeeded)SetThreadGroupAffinity(GetCurrentThread(),&saved,nullptr);
#endif
    }
};
long long percentile(std::vector<long long> values,double p)
{ require(!values.empty());std::sort(values.begin(),values.end());return values[std::size_t(std::ceil(p*values.size()))-1]; }
double percentile(std::vector<double> values,double p)
{ require(!values.empty());std::sort(values.begin(),values.end());return values[std::size_t(std::ceil(p*values.size()))-1]; }
bool finite(const juce::AudioBuffer<float>& b)
{
    for(int c=0;c<b.getNumChannels();++c)for(int n=0;n<b.getNumSamples();++n)if(!std::isfinite(b.getSample(c,n)))return false;
    return true;
}
unsigned verifiedWarmupFrames()
{
    juce::dsp::Oversampling<float> os(1,2,juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,true,false);
    os.initProcessing(32);AdaaWaveFolder adaa;
    const double raw=os.getLatencyInSamples()+adaa.getLatencyInInternalSamples()/4.;
    return unsigned(std::ceil(2.*raw))+65+3;
}
void selfTest()
{
    require(verifiedWarmupFrames()==middlecpu::warmupFrames);
    for(double rate:{48000.,96000.})for(unsigned frames:{32u,64u,127u,256u,1024u,2051u})
    {
        unsigned seen=0;
        for(unsigned i=0;i<10000;++i)
        {
            const auto c=middlecpu::controls(8,i,rate,frames);seen|=c.phase;
            require(c.phase!=0&&c.fixture<3&&c.residue<101);
        }
        require(seen==middlecpu::requiredPhases(8));
    }
    require(middlecpu::controls(4,100,48000,32).driver==4.f);
    require(middlecpu::controls(7,0,48000,32).middle.tanh.bypass);
    // Independent DSP work oracle: the schedule must predict the actual
    // suspended/fading callback work, including callbacks crossing a boundary.
    for(double rate:{48000.,96000.})for(unsigned frames:{32u,127u,1024u})
    {
        MiddleSection section;auto initial=middlecpu::controls(8,0,rate,frames);
        require(section.setParameters(initial.middle)&&section.prepare({rate,frames,1},4,MiddleLatencyCorrection::fir,MiddleFoldAlgorithm::adaaTriangle));
        juce::AudioBuffer<float> b(1,int(frames));const unsigned ramp=unsigned(rate*.01),dwell=(ramp+middlecpu::warmupFrames+frames-1)/frames+8;
        for(unsigned i=0;i<9*dwell;++i)
        {
            const auto c=middlecpu::controls(8,i,rate,frames);b.clear();
            require(section.setParameters(c.middle)&&section.setDriver(c.driver));
            const auto before=section.getWetFramesProcessed();require(section.process(juce::dsp::AudioBlock<float>(b)));
            const auto work=section.getWetFramesProcessed()-before;
            unsigned expected=frames;
            if((i/dwell)%3==0){const auto a=(i%dwell)*frames;expected=i<dwell?0:a<ramp?std::min(frames,ramp-a):0;}
            require(work==expected&&section.getLatencySamples()==92);
        }
    }
    std::cout<<"middle CPU schedule and fixed-boundary self-test passed\n";
}
using Fixtures=std::array<juce::AudioBuffer<float>,3>;
Fixtures fixtures(double rate,int channels,unsigned frames)
{
    Fixtures result;
    for(unsigned f=0;f<3;++f)
    {
        auto& b=result[f];b.setSize(channels,int(frames));std::uint32_t state=0x13579bdfu;
        for(unsigned n=0;n<frames;++n)
        {
            state=state*1664525u+1013904223u;
            const double t=n/rate;
            const float v=f==0?float(.25*std::sin(2*juce::MathConstants<double>::pi*997*t)):
                f==1?float(.125*(std::sin(2*juce::MathConstants<double>::pi*997*t)+std::sin(2*juce::MathConstants<double>::pi*1499*t))):
                .15f*(float((state>>8)*(1./16777216.))*2.f-1.f)+(n%97==0?.25f:0.f);
            for(int c=0;c<channels;++c)b.setSample(c,int(n),v*(c?-.73f:1.f));
        }
    }
    return result;
}
void copy(juce::AudioBuffer<float>& b,const juce::AudioBuffer<float>& f)
{for(int c=0;c<b.getNumChannels();++c)b.copyFrom(c,0,f,c,0,b.getNumSamples());}
struct Observation { std::uint64_t iteration;long long wall,ns;unsigned residue,phase,fixture; };
struct Counts
{
    std::array<unsigned,7> phases{};std::array<unsigned,3> fixture{};std::array<unsigned,101> residue{};
    void add(const middlecpu::Controls& c){for(unsigned i=0;i<7;++i)if(c.phase&middlecpu::phaseBits[i])++phases[i];++fixture[c.fixture];++residue[c.residue];}
    bool covered(unsigned scenario,unsigned minimum)const
    {
        const auto required=middlecpu::requiredPhases(scenario);
        for(unsigned i=0;i<7;++i)if((required&middlecpu::phaseBits[i])&&phases[i]<minimum)return false;
        if(*std::min_element(fixture.begin(),fixture.end())<minimum)return false;
        return !((scenario==4||scenario==5)&&minimum>=100&&*std::min_element(residue.begin(),residue.end())<100);
    }
    juce::var phaseJson()const {auto o=object();for(unsigned i=0;i<7;++i)put(o,std::to_string(middlecpu::phaseBits[i]).c_str(),int(phases[i]));return o;}
};
template<std::size_t N>juce::var array(const std::array<unsigned,N>& a){juce::Array<juce::var> r;for(auto n:a)r.add(int(n));return juce::var(r);}
juce::var collect(const std::filesystem::path& root,int id,int run,double rate,int channels,unsigned capacity,unsigned scenario,const Options& options,combmeasure::ThreadAffinity& affinity,const combmeasure::Platform& platform,processpolicy::ScopedProcessPolicy& policy)
{
    const unsigned frames=scenario==9?2*capacity+3:capacity;
    MiddleSection section;const auto initial=middlecpu::controls(scenario,0,rate,frames);
    require(section.setParameters(initial.middle)&&section.setDriver(initial.driver));require(section.prepare({rate,capacity,unsigned(channels)},4,MiddleLatencyCorrection::fir,MiddleFoldAlgorithm::adaaTriangle));
    require(section.getLatencySamples()==92);
    auto source=fixtures(rate,channels,frames);juce::AudioBuffer<float> b(channels,int(frames));
    bool good=true;double sink=0.;std::uint64_t iteration=0;
    auto callback=[&](bool includeCopy)
    {
        const auto c=middlecpu::controls(scenario,iteration,rate,frames);
        if(!includeCopy)copy(b,source[c.fixture]);
        const auto start=Clock::now();if(includeCopy)copy(b,source[c.fixture]);
        const bool ok=section.setParameters(c.middle)&&section.setDriver(c.driver)&&section.process(juce::dsp::AudioBlock<float>(b));
        const auto stop=Clock::now();good&=ok&&finite(b);sink+=b.getSample(0,0);++iteration;
        return std::chrono::duration_cast<std::chrono::nanoseconds>(stop-start).count();
    };
    const auto before=combmeasure::observePlatform(true,affinity);const auto schedulingBefore=processpolicy::observe();
    juce::ScopedNoDenormals noDenormals;
    const double warm=options.quick?.02:2.,probe=options.quick?.02:2.;auto until=Clock::now()+std::chrono::duration<double>(warm);while(Clock::now()<until)callback(false);
    std::uint64_t probes=0;until=Clock::now()+std::chrono::duration<double>(probe);while(Clock::now()<until){callback(false);++probes;}
    const double seconds=options.quick?.05:benchmark::expandedTimingWindowSeconds(double(probes)/probe);
    const auto window=static_cast<long long>(seconds*1e9);
    const auto startIteration=iteration;
    std::vector<Observation> observations;observations.reserve(200000);Counts counts;
    benchmark::ExpandedWallTimeSampler sampler(window);const auto begin=Clock::now();
    for(;;)
    {
        const auto wall=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-begin).count();if(wall>=window)break;
        const auto index=iteration;const auto c=middlecpu::controls(scenario,index,rate,frames);const bool sample=sampler.shouldSample(wall);
        const auto ns=callback(false);
        if(sample){require(observations.size()<200000);observations.push_back({index,wall,ns,c.residue,c.phase,c.fixture});counts.add(c);}
    }
    const auto collectionElapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-begin).count();
    const auto endpointMargin=std::max(1000000LL,window/100);
    const bool collectionCoverage=!observations.empty()&&collectionElapsed>=window&&observations.front().wall<=endpointMargin&&observations.back().wall>=window-endpointMargin;
    std::vector<long long> sorted;sorted.reserve(observations.size());std::array<std::vector<long long>,7> classes;
    for(const auto& o:observations){sorted.push_back(o.ns);for(unsigned i=0;i<7;++i)if(o.phase&middlecpu::phaseBits[i])classes[i].push_back(o.ns);}
    require(!sorted.empty());const double deadline=frames/rate*1.e9;const auto p99=percentile(sorted,.99);
    const auto timingName="timing-"+std::to_string(id)+".csv",batchName="batch-"+std::to_string(id)+".csv";
    std::ofstream raw(root/timingName);raw<<"iteration,wall_ns,duration_ns,residue,phase,fixture\n";
    for(const auto& o:observations)raw<<o.iteration<<','<<o.wall<<','<<o.ns<<','<<o.residue<<','<<o.phase<<','<<o.fixture<<'\n';raw.close();require(!raw.fail());
    // Historical boundary includes input copy. Reset and replay128 warm blocks.
    iteration=0;require(section.setParameters(initial.middle)&&section.setDriver(initial.driver));section.reset();for(int n=0;n<128;++n)callback(true);
    Counts batchCounts;std::ofstream batches(root/batchName);batches<<"batch,block,iteration,phase,fixture,batch_elapsed_ns\n";
    std::vector<double> means;
    for(unsigned batch=0;batch<21;++batch)
    {
        std::array<middlecpu::Controls,128> batchControls;
        for(unsigned block=0;block<128;++block)batchControls[block]=middlecpu::controls(scenario,iteration+block,rate,frames);
        const auto batchStart=Clock::now();
        for(unsigned block=0;block<128;++block)
        {
            const auto& c=batchControls[block];copy(b,source[c.fixture]);
            good&=section.setParameters(c.middle)&&section.setDriver(c.driver)&&section.process(juce::dsp::AudioBlock<float>(b));
        }
        const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-batchStart).count();
        good&=finite(b);sink+=b.getSample(0,0);means.push_back(double(elapsed)/128.);
        for(unsigned block=0;block<128;++block)
        {
            const auto& c=batchControls[block];batchCounts.add(c);
            batches<<batch<<','<<block<<','<<iteration+block<<','<<c.phase<<','<<c.fixture<<','<<elapsed<<'\n';
        }
        iteration+=128;
    }
    batches.close();require(!batches.fail());const double batchP95=percentile(means,.95);
    // Replay outside every timing interval, checking every output block. A
    // recovered intermediate NaN must not escape by checking only batch tails.
    bool replayOk=true;require(section.setParameters(initial.middle)&&section.setDriver(initial.driver));section.reset();
    for(unsigned n=0;n<128+21*128;++n)
    {
        const auto c=middlecpu::controls(scenario,n,rate,frames);copy(b,source[c.fixture]);
        replayOk&=section.setParameters(c.middle)&&section.setDriver(c.driver)&&section.process(juce::dsp::AudioBlock<float>(b))&&finite(b);
    }
    const auto after=combmeasure::observePlatform(true,affinity);const auto schedulingAfter=processpolicy::observe();
    const bool power=before.onAC&&after.onAC&&before.power==after.power&&before.power==platform.power;
    auto classP99=object();bool classBudget=true;const auto required=middlecpu::requiredPhases(scenario);
    for(unsigned i=0;i<7;++i)
    {
        const auto value=classes[i].empty()?0:percentile(classes[i],.99);put(classP99,std::to_string(middlecpu::phaseBits[i]).c_str(),juce::int64(value));
        if(required&middlecpu::phaseBits[i])classBudget&=!classes[i].empty()&&value/deadline<.1;
    }
    auto row=object();put(row,"id",id);put(row,"run",run);put(row,"rate",rate);put(row,"channels",channels);put(row,"capacity",int(capacity));put(row,"frames",int(frames));put(row,"scenario",int(scenario));put(row,"scenario_name",middlecpu::scenarios[scenario]);
    put(row,"warmup_frames",int(middlecpu::warmupFrames));put(row,"warmup_seconds",warm);put(row,"probe_seconds",probe);put(row,"probe_callbacks",juce::int64(probes));put(row,"window_seconds",seconds);put(row,"start_iteration",juce::int64(startIteration));put(row,"collection_elapsed_ns",juce::int64(collectionElapsed));put(row,"collection_coverage_ok",collectionCoverage);put(row,"replay_ok",replayOk);
    put(row,"count",int(sorted.size()));put(row,"latency_samples",int(section.getLatencySamples()));put(row,"p50_ns",juce::int64(percentile(sorted,.5)));put(row,"p95_ns",juce::int64(percentile(sorted,.95)));put(row,"p99_ns",juce::int64(p99));put(row,"max_ns",juce::int64(*std::max_element(sorted.begin(),sorted.end())));put(row,"deadline_ns",deadline);put(row,"deadline_overruns",int(std::count_if(sorted.begin(),sorted.end(),[&](auto n){return n>deadline;})));
    put(row,"p99_fraction",p99/deadline);put(row,"batch_p95_fraction",batchP95/deadline);put(row,"callback_budget_ok",p99/deadline<.1&&classBudget);put(row,"batch_budget_ok",batchP95/deadline<.1);put(row,"coverage_ok",collectionCoverage&&counts.covered(scenario,100)&&sorted.size()>=100000&&sorted.size()<=200000);put(row,"batch_coverage_ok",batchCounts.covered(scenario,1));put(row,"processing_ok",good&&replayOk&&section.getLatencySamples()==92);put(row,"power_stable",power);put(row,"policy_ok",policy.caseOk(schedulingBefore,schedulingAfter));
    put(row,"phase_counts",counts.phaseJson());put(row,"class_p99_ns",classP99);put(row,"fixture_counts",array(counts.fixture));put(row,"residue_counts",array(counts.residue));put(row,"batch_phase_counts",batchCounts.phaseJson());put(row,"batch_fixture_counts",array(batchCounts.fixture));
    put(row,"ac_before",before.onAC);put(row,"ac_after",after.onAC);put(row,"power_before",before.power);put(row,"power_after",after.power);put(row,"scheduling_before",schedulingBefore);put(row,"scheduling_after",schedulingAfter);put(row,"timing_file",timingName);put(row,"timing_sha256",hash(root/timingName));put(row,"batch_file",batchName);put(row,"batch_sha256",hash(root/batchName));put(row,"sink",sink);
    json(root/("case-"+std::to_string(id)+".json"),row);std::cout<<id<<" "<<rate<<'/'<<channels<<'/'<<capacity<<'/'<<middlecpu::scenarios[scenario]<<" p99="<<p99/deadline<<" batch="<<batchP95/deadline<<std::endl;return row;
}
int capture(const std::filesystem::path& root,const char* executable)
{
    auto report=object();put(report,"kind","middle-sound-regression");put(report,"provenance",identity(executable));juce::Array<juce::var> rows;int id=0;
    for(double rate:{48000.,96000.})for(float driver:{1.f,1.5f,2.f,3.f,4.f})for(bool extreme:{false,true})for(float threshold:{.05f,1.f})for(float trim:{-60.f,0.f,12.f})for(float mix:{0.f,.5f,1.f})
    {
        MiddleSection section;MiddleSettings s;s.tanh.driveDb=s.folder.driveDb=extreme?36.f:6.f;s.folder.threshold=threshold;s.trimDb=trim;s.mix=mix;
        require(section.setParameters(s)&&section.setDriver(driver)&&section.prepare({rate,127,2},4,MiddleLatencyCorrection::fir,MiddleFoldAlgorithm::adaaTriangle));
        auto f=fixtures(rate,2,127);juce::AudioBuffer<float> b(2,127);const auto name="sound-"+std::to_string(id)+".f32";std::ofstream out(root/name,std::ios::binary);std::array<float,254> interleaved{};
        juce::ScopedNoDenormals noDenormals;
        for(unsigned block=0;block<64;++block){copy(b,f[block%3]);require(section.process(juce::dsp::AudioBlock<float>(b))&&finite(b));for(int n=0;n<127;++n)for(int c=0;c<2;++c)interleaved[std::size_t(n*2+c)]=b.getSample(c,n);out.write(reinterpret_cast<const char*>(interleaved.data()),sizeof(interleaved));}
        out.close();require(!out.fail());auto row=object();put(row,"id",id++);put(row,"rate",rate);put(row,"driver",driver);put(row,"extreme",extreme);put(row,"threshold",threshold);put(row,"trim",trim);put(row,"mix",mix);put(row,"file",name);put(row,"sha256",hash(root/name));put(row,"frames",64*127);put(row,"channels",2);rows.add(row);
    }
    put(report,"cases",juce::var(rows));json(root/"captures.json",report);return 0;
}
}
int main(int argc,char** argv)
{
    try
    {
        const auto options=parse(argc,argv);if(options.selfTest){selfTest();return 0;}require(verifiedWarmupFrames()==middlecpu::warmupFrames);
        const std::filesystem::path root(options.output);require(std::filesystem::create_directories(root));if(options.capture)return capture(root,argv[0]);
        AffinityRestoration original;juce::var pinned;bool affinityOk=false;combmeasure::Platform platform;juce::var policyMetadata,beforeRequest,afterCollection,afterRestore;bool applicationOk=false,restoreOk=false,collectionOk=false,affinityVerified=false;
        const auto beforeIdentity=identity(argv[0]);juce::Array<juce::var> rows;const int runs=options.diagnostic?1:3;bool all=true;
        {
            // Lifetime scope ensures the helper destructor restores its legacy mask
            // before explicit group restoration and final readback below.
            combmeasure::ThreadAffinity affinity;platform=combmeasure::observePlatform(options.diagnostic,affinity);pinned=processpolicy::observe();
            processpolicy::ScopedProcessPolicy policy(options.highQos);beforeRequest=policy.beforeRequest();applicationOk=policy.applicationOk();affinityVerified=processpolicy::selectedAffinityMatches(policy.appliedSnapshot(),platform);
            auto interim=object();put(interim,"kind","middle-cpu");put(interim,"complete",false);put(interim,"reference_qualified",false);put(interim,"provenance_before",beforeIdentity);json(root/"session.json",interim);
            int id=0;
            for(int run=0;run<runs;++run)for(double rate:{48000.,96000.})for(int channels:{1,2})for(unsigned capacity:{32u,64u,127u,256u,1024u})for(unsigned scenario=0;scenario<10;++scenario,++id)
            {
                if(options.selected>=0&&id!=options.selected)continue;
                auto row=collect(root,id,run,rate,channels,capacity,scenario,options,affinity,platform,policy);
                for(const auto* key:{"callback_budget_ok","batch_budget_ok","coverage_ok","batch_coverage_ok","processing_ok","power_stable","policy_ok"})all&=bool(row[key]);rows.add(row);
            }
            afterCollection=processpolicy::observe();collectionOk=policy.caseOk(policy.appliedSnapshot(),afterCollection);policy.restore();policyMetadata=policy.metadata();restoreOk=policy.restorationOk();afterRestore=policy.restoredSnapshot();
        }
        original.restore();affinityOk=original.ok();const auto afterIdentity=identity(argv[0]);const bool unchanged=sameIdentity(beforeIdentity,afterIdentity);
        bool qualified=!options.diagnostic&&platform.onAC&&platform.pinned&&platform.hybridEstablished&&platform.power!="unavailable"&&all&&applicationOk&&restoreOk&&collectionOk&&affinityVerified&&affinityOk&&unchanged&&rows.size()==600;
#ifndef NDEBUG
        qualified=false;
#endif
        auto report=object();put(report,"schema_version",1);put(report,"kind","middle-cpu");put(report,"method_id","middle-cpu-v1");put(report,"environment_method",options.highQos?"windows-explicit-highqos-v1":"windows-system-managed-v2");put(report,"runs",runs);put(report,"quick",options.quick);put(report,"diagnostic",options.diagnostic);put(report,"filtered",options.selected>=0);put(report,"complete",options.selected<0);put(report,"reference_qualified",qualified);put(report,"conditions",options.conditions);put(report,"cases",juce::var(rows));put(report,"provenance_before",beforeIdentity);put(report,"provenance_after",afterIdentity);put(report,"source_unchanged",unchanged);
        put(report,"pinned",platform.pinned);put(report,"hybrid_established",platform.hybridEstablished);put(report,"on_ac",platform.onAC);put(report,"affinity",platform.affinity);put(report,"topology",platform.topology);put(report,"power",platform.power);put(report,"sdk",platform.sdk);put(report,"thermal","sensor unavailable; ambient unconfirmed");put(report,"process_policy",policyMetadata);put(report,"affinity_before",original.before);put(report,"affinity_pinned",pinned);put(report,"affinity_restored",original.restored);put(report,"affinity_restoration_ok",affinityOk);put(report,"affinity_verified",affinityVerified);put(report,"collection_policy_ok",collectionOk);put(report,"scheduling_before",beforeRequest);put(report,"scheduling_after",afterCollection);put(report,"scheduling_after_restore",afterRestore);
        std::vector<long long> overhead(1000);for(auto& n:overhead){const auto a=Clock::now(),b=Clock::now();n=std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count();}put(report,"timer_only_median_ns",juce::int64(percentile(overhead,.5)));put(report,"timer_overhead_subtracted",false);json(root/"result.json",report);return qualified?0:1;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
