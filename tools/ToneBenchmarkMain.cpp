#include "dsp/Tone.h"
#include "CombBenchmarkPlatform.h"
#include "../benchmarks/WallTimeSampling.h"
#include "../benchmarks/TimingWindow.h"
#include <juce_cryptography/juce_cryptography.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc,char** argv)
{
    using namespace disdorktion;using Clock=std::chrono::steady_clock;
    try {
        bool quick=false;std::string output,conditions;
        for(int i=1;i<argc;++i){const std::string arg=argv[i];if(arg=="--quick")quick=true;else if(arg=="--output"&&i+1<argc)output=argv[++i];else if(arg=="--conditions"&&i+1<argc)conditions=argv[++i];else throw std::runtime_error("usage: [--quick] --output NEW_DIRECTORY --conditions TEXT");}
        if(output.empty()||conditions.empty()||std::filesystem::exists(output))throw std::runtime_error("output must be new and conditions specified");
        std::filesystem::create_directories(output);const std::filesystem::path root(output);
        combmeasure::ThreadAffinity affinity;const auto platform=combmeasure::observePlatform(false,affinity);
        bool eligible=!quick&&platform.onAC&&platform.pinned&&platform.hybridEstablished&&platform.power!="unavailable";bool passed=true;
#ifndef NDEBUG
        eligible=false;
#endif
        const int runs=quick?1:3;std::ofstream raw(root/"timings.csv"),summary(root/"summary.csv");raw<<"case,iteration,elapsed_ns,control_residue,bypass_target\n";summary<<std::setprecision(17)<<"case,run,rate,capacity,frames,scenario,count,p99_ns,deadline_ns,fraction,processing_ok,coverage_ok,power_stable,window_seconds\n";
        int id=0;double sink=0;juce::Array<juce::var> rows;
        for(int run=0;run<runs;++run)for(double fs:{48000.,96000.,192000.})for(unsigned capacity:{32u,64u,127u,256u,1024u})for(int scenario=0;scenario<7;++scenario){
            const auto before=combmeasure::observePlatform(true,affinity);Tone dsp;ToneSettings s;s.toneDb=scenario==4?6.f:scenario==5?0.f:-6.f;const unsigned frames=scenario==6?2*capacity+7:capacity;
            if(!dsp.setParameters(s)||!dsp.prepare({fs,capacity,2}))throw std::runtime_error("prepare rejected");
            juce::AudioBuffer<float> buffer(2,int(frames)),source(2,int(frames));for(int c=0;c<2;++c)for(unsigned n=0;n<frames;++n)source.setSample(c,int(n),float(.2*std::sin(.013*n)*(c?-.73:1)));
            if(scenario==3){buffer.clear();buffer.setSample(0,0,1);if(!dsp.process(juce::dsp::AudioBlock<float>(buffer)))throw std::runtime_error("process rejected");for(unsigned n=0;n<unsigned(std::ceil(2*fs/capacity));++n){buffer.clear();if(!dsp.process(juce::dsp::AudioBlock<float>(buffer)))throw std::runtime_error("process rejected");}}
            const unsigned ramp=unsigned(std::ceil(.01*fs/capacity)),dwell=ramp+8;unsigned iteration=0;bool ok=true;std::array<unsigned,101> residues{};std::array<unsigned,2> settled{};
            auto callback=[&](bool sample){if(scenario==3)buffer.clear();else for(int c=0;c<2;++c)buffer.copyFrom(c,0,source,c,0,int(frames));const unsigned residue=iteration%101;
                if(scenario==1){s.toneDb=-6+12*float(residue)/100;}
                if(scenario==2)s.bypass=bool((iteration/dwell)%2);
                const auto begin=Clock::now();const bool controls=dsp.setParameters(s),process=dsp.process(juce::dsp::AudioBlock<float>(buffer));const auto end=Clock::now();ok=ok&&controls&&process;
                for(int c=0;c<2;++c)for(unsigned n=0;n<frames;++n)ok=ok&&std::isfinite(buffer.getSample(c,int(n)));sink+=buffer.getSample(0,int(frames-1));
                if(sample){++residues[residue];if(scenario==2&&iteration%dwell>=ramp)++settled[size_t(s.bypass)];}++iteration;return std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin).count();};
            const double warm=quick?.02:2.,probe=quick?.02:2.;auto stop=Clock::now()+std::chrono::duration<double>(warm);while(Clock::now()<stop)callback(false);unsigned probes=0;stop=Clock::now()+std::chrono::duration<double>(probe);while(Clock::now()<stop){callback(false);++probes;}
            const double seconds=quick?.05:benchmark::timingWindowSeconds(probes*10/probe);const auto window=static_cast<long long>(seconds*1e9);benchmark::WallTimeSampler sampler(window);struct Observation{unsigned iteration;long long ns;bool bypass;};std::vector<Observation> observations;observations.reserve(150000);const auto begin=Clock::now();
            while(true){const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-begin).count();if(elapsed>=window)break;const bool sample=sampler.shouldSample(elapsed);const auto index=iteration;const auto ns=callback(sample);if(sample)observations.push_back({index,ns,s.bypass});}
            std::vector<long long> sorted;for(const auto& o:observations){raw<<id<<','<<o.iteration<<','<<o.ns<<','<<o.iteration%101<<','<<o.bypass<<'\n';sorted.push_back(o.ns);}if(sorted.empty())throw std::runtime_error("empty timing capture");std::sort(sorted.begin(),sorted.end());const auto p99=sorted[size_t(std::ceil(.99*sorted.size()))-1];const double deadline=frames/fs*1e9;const bool budget=p99/deadline<.02;const bool coverage=sorted.size()>=100000&&sorted.size()<=200000&&(scenario!=1||*std::min_element(residues.begin(),residues.end())>=100)&&(scenario!=2||std::min(settled[0],settled[1])>=100);
            const auto after=combmeasure::observePlatform(true,affinity);const bool power=before.onAC&&after.onAC&&before.power==after.power&&before.power==platform.power;eligible=eligible&&ok&&coverage&&power&&budget;passed=passed&&ok&&budget;
            summary<<id<<','<<run<<','<<fs<<','<<capacity<<','<<frames<<','<<scenario<<','<<sorted.size()<<','<<p99<<','<<deadline<<','<<p99/deadline<<','<<ok<<','<<coverage<<','<<power<<','<<seconds<<'\n';auto row=juce::var(new juce::DynamicObject);auto* o=row.getDynamicObject();o->setProperty("id",id++);o->setProperty("run",run);o->setProperty("rate",fs);o->setProperty("capacity",int(capacity));o->setProperty("frames",int(frames));o->setProperty("scenario",scenario);o->setProperty("count",int(sorted.size()));o->setProperty("p99_fraction",p99/deadline);o->setProperty("processing_ok",ok);o->setProperty("coverage_ok",coverage);o->setProperty("power_stable",power);rows.add(row);std::cout<<id<<" p99="<<p99/deadline<<'\n';}
        raw.close();summary.close();if(raw.fail()||summary.fail())throw std::runtime_error("output failure");auto report=juce::var(new juce::DynamicObject);auto* o=report.getDynamicObject();o->setProperty("schema",1);o->setProperty("kind","tone-cpu");o->setProperty("quick",quick);o->setProperty("runs",runs);o->setProperty("conditions",juce::String(conditions));o->setProperty("reference_qualified",eligible);o->setProperty("processing_and_budget_ok",passed);o->setProperty("pinned",platform.pinned);o->setProperty("hybrid_established",platform.hybridEstablished);o->setProperty("on_ac",platform.onAC);o->setProperty("cpu",juce::String(platform.cpu));o->setProperty("os",juce::String(platform.os));o->setProperty("affinity",juce::String(platform.affinity));o->setProperty("topology",juce::String(platform.topology));o->setProperty("power",juce::String(platform.power));o->setProperty("thermal","sensor unavailable; ambient unconfirmed");o->setProperty("scenarios","0=dark endpoint static,1=automation,2=bypass transitions,3=quiet decay after2s,4=bright static,5=neutral static,6=oversized dark");o->setProperty("timer_overhead_subtracted",false);o->setProperty("cases",juce::var(rows));o->setProperty("sink",sink);
        auto digest=[](const std::filesystem::path& p){return juce::SHA256(juce::File(juce::String(std::filesystem::absolute(p).string()))).toHexString();};o->setProperty("executable_sha256",digest(argv[0]));o->setProperty("timings_sha256",digest(root/"timings.csv"));o->setProperty("summary_sha256",digest(root/"summary.csv"));o->setProperty("dsp_sha256",digest(std::filesystem::path(DISDORKTION_SOURCE_ROOT)/"src/dsp/Tone.h"));o->setProperty("collector_sha256",digest(std::filesystem::path(DISDORKTION_SOURCE_ROOT)/"tools/ToneBenchmarkMain.cpp"));o->setProperty("sdk",juce::String(platform.sdk));o->setProperty("juce",juce::SystemStats::getJUCEVersion());
#ifdef _MSC_FULL_VER
        o->setProperty("compiler",juce::String("MSVC ")+juce::String(_MSC_FULL_VER));
#endif
        std::array<long long,1000> timer{};for(auto& value:timer){const auto a=Clock::now(),b=Clock::now();value=std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count();}std::sort(timer.begin(),timer.end());o->setProperty("timer_only_median_ns",juce::int64(timer[500]));
#ifdef NDEBUG
        o->setProperty("release",true);
#else
        o->setProperty("release",false);
#endif
        std::ofstream f(root/"result.json");f<<juce::JSON::toString(report,true).toStdString()<<'\n';if(!f)throw std::runtime_error("json output failure");return eligible?0:1;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
