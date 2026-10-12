#include "dsp/HighPass.h"
#include "dsp/LowPass.h"
#include "harness/OfflineRender.h"
#include "CutoffFilterAcceptance.h"
#include <algorithm>
#include <chrono>
#include <complex>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <vector>

using namespace disdorktion;
using namespace disdorktion::harness;
namespace
{
constexpr double pi=std::numbers::pi;
constexpr std::array modes {FilterResponse::butterworth,FilterResponse::chebyshev1,FilterResponse::chebyshev2};
double expectedMagnitude(bool high,FilterResponse mode,double rate,double cutoff,double frequency)
{
    if(frequency==0) return high ? (mode==FilterResponse::chebyshev2?.01:0.) : (mode==FilterResponse::chebyshev1?std::pow(10.,-.05):1.);
    if(frequency==rate/2.) return high ? (mode==FilterResponse::chebyshev1?std::pow(10.,-.05):1.) : (mode==FilterResponse::chebyshev2?.01:0.);
    double u=std::tan(pi*frequency/rate)/std::tan(pi*cutoff/rate);if(high)u=1./u;
    if(mode==FilterResponse::butterworth)return 1./std::sqrt(1.+u*u*u*u);
    const double epsilon=mode==FilterResponse::chebyshev1?std::sqrt(std::pow(10.,.1)-1.):1./std::sqrt(9999.);
    if(mode==FilterResponse::chebyshev1)
    {const double w=u*std::sqrt((1.+1./epsilon)/2.);const double t=2*w*w-1.;return 1./std::sqrt(1.+epsilon*epsilon*t*t);}
    const double w=u*std::sqrt(2./(1.+1./epsilon));const double t=2./(w*w)-1.;
    return std::abs(epsilon*t)/std::sqrt(1.+epsilon*epsilon*t*t);
}
double impulseMagnitude(const juce::AudioBuffer<float>& audio,double rate,double frequency)
{
    const auto step=std::polar(1.,-2*pi*frequency/rate);std::complex<double> phase=1.,sum=0.;
    for(int n=0;n<audio.getNumSamples();++n){sum+=static_cast<double>(audio.getSample(0,n))*phase;phase*=step;}
    return std::abs(sum);
}
template<class F> bool spectral(const juce::File& root,bool high,std::ofstream& csv,double& maxError,double& maxToneDb)
{
    for(double rate:{8000.,44100.,48000.,96000.,192000.,384000.})
    for(float request:{20.f,1000.f,20000.f})for(auto mode:modes)
    {
        F filter;if(!filter.setParameters({request,false,mode})||!filter.prepare({rate,32,1}))return false;
        const double cutoff=std::min(static_cast<double>(request),.45*rate),base=std::tan(pi*cutoff/rate);
        juce::AudioBuffer<float> audio(1,131072);audio.clear();audio.setSample(0,0,1.f);
        if(!filter.process(juce::dsp::AudioBlock<float>(audio)))return false;
        std::vector<double> frequencies {0,cutoff*.1,cutoff*.5,cutoff,cutoff*2,cutoff*8,rate*.49,rate/2};
        if(mode==FilterResponse::chebyshev1)
        {
            const double e=std::sqrt(std::pow(10.,.1)-1.),w3=std::sqrt((1.+1./e)/2.);
            const double factor=1./(std::sqrt(2.)*w3);
            frequencies.push_back(rate/pi*std::atan(base*(high?1./factor:factor)));
        }
        if(mode==FilterResponse::chebyshev2)
        {
            const double w3=std::sqrt(2./(1.+std::sqrt(9999.))),factor=std::sqrt(2.)/w3;
            frequencies.push_back(rate/pi*std::atan(base*(high?1./factor:factor)));
        }
        for(double f:frequencies)
        {
            if(f>rate/2.)continue;
            const double expected=expectedMagnitude(high,mode,rate,cutoff,f),measured=impulseMagnitude(audio,rate,f);
            const double error=std::abs(expected-measured);maxError=std::max(maxError,error);
            csv<<(high?"high-pass":"low-pass")<<','<<filterResponseIds[static_cast<unsigned>(mode)]<<','<<rate<<','<<request<<','<<cutoff<<','<<f<<','<<expected<<','<<measured<<','<<error<<'\n';
        }
        // A stepped sine sweep supplies a second measurement independent of impulse DFT.
        if(rate==48000 && request==1000)
        for(double f:{125.,250.,500.,1000.,2000.,4000.,8000.,16000.})
        {
            filter.reset();juce::AudioBuffer<float> tone(1,96000);
            for(int n=0;n<96000;++n)tone.setSample(0,n,static_cast<float>(.25*std::sin(2*pi*f*n/rate)));
            if(!filter.process(juce::dsp::AudioBlock<float>(tone)))return false;
            double ss=0,cc=0,sc=0,ys=0,yc=0;
            for(int n=48000;n<96000;++n){const double s=std::sin(2*pi*f*n/rate),c=std::cos(2*pi*f*n/rate),y=tone.getSample(0,n);ss+=s*s;cc+=c*c;sc+=s*c;ys+=y*s;yc+=y*c;}
            const double determinant=ss*cc-sc*sc,a=(ys*cc-yc*sc)/determinant,b=(yc*ss-ys*sc)/determinant;
            const double measured=std::hypot(a,b)/.25,expected=expectedMagnitude(high,mode,rate,cutoff,f);
            const double db=20.*std::log10(measured/expected);maxToneDb=std::max(maxToneDb,std::abs(db));
        }
    }
    for(auto mode:modes)
    {
        ExperimentRecord record;record.version=7;record.targetId=high?"high-pass":"low-pass";record.cutoffHz=1000;record.response=mode;
        record.source.kind=SourceKind::sweep;record.source.frequency=20;record.source.frequency2=20000;
        record.source.durationSamples=240000;record.renderDurationSamples=240000;record.source.amplitude=.25;
        record.observations="Listening pending: compare sweep coloration/ringing and bypass at monitor -18 dB; also audition your own transient/music file.";
        const auto directory=root.getChildFile(record.targetId+"-"+juce::String(filterResponseIds[static_cast<unsigned>(mode)].data()));
        const auto rendered=renderExperiment(record,directory);if(rendered.status!=RenderStatus::success){std::cerr<<rendered.message<<'\n';return false;}
    }
    return true;
}
volatile float outputSink=0;
template<class F> bool timing(bool high,std::ofstream& summary,std::ofstream& raw,unsigned& caseIndex,double& worst)
{
    using Clock=std::chrono::steady_clock;
    const juce::ScopedNoDenormals noDenormals;
    for(double rate:{44100.,48000.,96000.,192000.})for(unsigned size:{32u,64u,128u,256u,512u,1024u})
    for(unsigned channels:{1u,2u})for(unsigned scenario=0;scenario<3;++scenario)
    {
        F filter;if(!filter.setParameters({1000,scenario==2,FilterResponse::butterworth})||!filter.prepare({rate,size,channels}))return false;
        juce::AudioBuffer<float> audio(static_cast<int>(channels),static_cast<int>(size));
        std::vector<double> durations;durations.reserve(1000);
        for(unsigned iteration=0;iteration<1256;++iteration)
        {
            for(unsigned c=0;c<channels;++c)for(unsigned n=0;n<size;++n)audio.setSample(static_cast<int>(c),static_cast<int>(n),static_cast<float>(.25*std::sin((iteration*size+n)*.13)));
            const auto start=Clock::now();
            bool ok=true;
            if(scenario==1)ok=filter.setParameters({iteration%2?20.f:20000.f,false,modes[iteration%3]});
            ok &= filter.process(juce::dsp::AudioBlock<float>(audio));
            const auto end=Clock::now();if(!ok)return false;outputSink=audio.getSample(0,0);
            if(iteration>=256){const double ns=std::chrono::duration<double,std::nano>(end-start).count();durations.push_back(ns);raw<<caseIndex<<','<<ns<<'\n';}
        }
        std::sort(durations.begin(),durations.end());const double p99=durations[989],deadline=1e9*size/rate,fraction=p99/deadline;
        worst=std::max(worst,fraction);
        summary<<caseIndex++<<','<<(high?"high-pass":"low-pass")<<','<<rate<<','<<size<<','<<channels<<','
            <<(scenario==0?"settled":scenario==1?"cutoff-mode-automation":"bypass")<<",1000,"<<durations[499]<<','<<p99<<','<<durations.back()<<','<<fraction<<",0,diagnostic\n";
    }
    return true;
}
}
int main(int argc,char** argv)
{
    if(argc!=2){std::cerr<<"Usage: disdorktion_cutoff_filter_measurements NEW_OUTPUT_DIRECTORY\n";return 2;}
    const juce::File root=juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    if(root.exists()||root.createDirectory().failed()){std::cerr<<"Output directory must be fresh\n";return 2;}
    std::ofstream spectra(root.getChildFile("spectra.csv").getFullPathName().toStdString());spectra<<std::setprecision(17)<<"target,response,sample_rate,requested_cutoff,effective_cutoff,frequency,expected_magnitude,measured_magnitude,absolute_error\n";
    double highError=0,lowError=0,highTone=0,lowTone=0;
    const bool spectraComplete=spectral<HighPass>(root,true,spectra,highError,highTone)&&spectral<LowPass>(root,false,spectra,lowError,lowTone);
    std::ofstream summary(root.getChildFile("cpu_summary.csv").getFullPathName().toStdString()),raw(root.getChildFile("cpu_raw.csv").getFullPathName().toStdString());
    summary<<std::setprecision(17)<<"case,target,sample_rate,block_size,channels,scenario,count,median_ns,p99_ns,max_ns,deadline_fraction,latency_samples,qualification\n";raw<<"case,nanoseconds\n";
    double highCpu=0,lowCpu=0;unsigned cases=0;
    const bool timingComplete=timing<HighPass>(true,summary,raw,cases,highCpu)&&timing<LowPass>(false,summary,raw,cases,lowCpu);
    auto* object=new juce::DynamicObject;juce::var report(object);
    object->setProperty("build",DISDORKTION_BUILD_ID);object->setProperty("os",juce::SystemStats::getOperatingSystemName());
    object->setProperty("cpuVendor",juce::SystemStats::getCpuVendor());object->setProperty("logicalCpus",juce::SystemStats::getNumCpus());
    object->setProperty("cpuQualification","diagnostic only; 256 warmup + 1000 samples per case; no affinity/power/thermal qualification");
    object->setProperty("highPassMaxMagnitudeError",highError);object->setProperty("lowPassMaxMagnitudeError",lowError);
    object->setProperty("highPassMaxSteppedSineErrorDb",highTone);object->setProperty("lowPassMaxSteppedSineErrorDb",lowTone);
    object->setProperty("highPassWorstP99DeadlineFraction",highCpu);object->setProperty("lowPassWorstP99DeadlineFraction",lowCpu);
    object->setProperty("cpuCases",static_cast<int>(cases));object->setProperty("listening","pending human evaluation");
    const bool spectralPass=spectraComplete&&highError<3e-5&&lowError<3e-5&&highTone<.05&&lowTone<.05;
    const bool cpuPass=timingComplete&&measurement::cutoffCpuWithinBudget(highCpu,lowCpu,cases);
    object->setProperty("spectralChecksPass",spectralPass);
    object->setProperty("cpuDiagnosticBudgetPass",cpuPass);
    object->setProperty("checksPass",spectralPass&&cpuPass);
    object->setProperty("referenceHardwareQualified",false);
    object->setProperty("sourceStatus","uncommitted issue-8 implementation; build identity names base revision");
    if(!root.getChildFile("result.json").replaceWithText(juce::JSON::toString(report)))return 1;
    if(!spectra.good()||!summary.good()||!raw.good())return 1;
    std::cout<<juce::JSON::toString(report)<<'\n';return spectralPass&&cpuPass?0:1;
}
