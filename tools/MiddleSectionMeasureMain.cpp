#include "dsp/MiddleSection.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <complex>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace
{
using disdorktion::MiddleSection;
using disdorktion::MiddleSettings;
constexpr double pi = juce::MathConstants<double>::pi;
constexpr int order=14, frames=1<<order;
disdorktion::MiddleFoldAlgorithm foldAlgorithm=disdorktion::MiddleFoldAlgorithm::triangle;
double db(double x) { return 20.0*std::log10(std::max(1e-15,std::abs(x))); }
void require(bool ok, const char* why) { if(!ok) throw std::runtime_error(why); }
juce::DynamicObject* object() { return new juce::DynamicObject; }

MiddleSettings settings(const juce::String& path,float drive,float threshold)
{
    return {{drive,0,path=="folder"},{drive,threshold,0,path=="tanh"}};
}

std::vector<float> spectrum(const juce::AudioBuffer<float>& b,int offset,bool hann=false)
{
    std::vector<float> v(2*frames,0);
    double sum=0;
    for(int n=0;n<frames;++n)
    {
        const auto w=hann ? .5-.5*std::cos(2*pi*n/(frames-1)) : 1.;
        v[n]=static_cast<float>(b.getSample(0,n+offset)*w); sum+=w;
    }
    juce::dsp::FFT fft(order); fft.performFrequencyOnlyForwardTransform(v.data());
    for(int k=0;k<=frames/2;++k) v[k]=static_cast<float>(v[k]*(k==0 || k==frames/2 ? 1. : 2.)/sum);
    v.resize(frames/2+1); return v;
}

// Off-bin power is a lower bound: aliases colliding with true harmonics are excluded.
double aliasDbc(const std::vector<float>& v,int bin)
{
    std::vector<bool> legitimate(frames/2+1,false);
    for(int h=1;h*bin<=frames/2;h+=2) legitimate[h*bin]=true;
    require(std::gcd(bin,frames)==1,"Non-coprime coherent input bin");
    for(int h=3;h<=127;h+=2)
    {
        if(h*bin<=frames/2) continue;
        const auto wrapped=h*bin%frames;
        const auto folded=std::min(wrapped,frames-wrapped);
        require(!legitimate[folded],"Alias/harmonic collision in tested orders");
    }
    double power=0;
    for(int k=1;k<frames/2;++k) if(!legitimate[k]) power+=static_cast<double>(v[k])*v[k];
    return db(std::sqrt(power)/std::max(1e-15,static_cast<double>(v[bin])));
}

juce::var spectralFixture(double rate,unsigned factor,const juce::String& path,float drive,float threshold,
                          const juce::String& kind,std::ostream& summary,juce::GZIPCompressorOutputStream& spectra)
{
    const bool two=kind.startsWith("two");
    const bool high=kind.endsWith("high");
    const int bin=two ? (high ? 3371 : 683) : (high ? 3371 : 457);
    const int bin2=high ? 4093 : 997;
    const int warm=static_cast<int>(std::ceil(rate*.050))+1024;
    juce::AudioBuffer<float> audio(1,warm+frames);
    for(int n=0;n<audio.getNumSamples();++n)
        audio.setSample(0,n,static_cast<float>((two?.125:.25)*std::sin(2*pi*bin*n/frames)
            +(two?.125*std::sin(2*pi*bin2*n/frames):0.)));
    MiddleSection m; require(m.setParameters(settings(path,drive,threshold)),"Spectral settings");
    require(m.prepare({rate,127,1},factor,disdorktion::MiddleLatencyCorrection::fir,foldAlgorithm),"Spectral prepare");
    require(m.process(juce::dsp::AudioBlock<float>(audio)),"Spectral process");
    for(int n=0;n<audio.getNumSamples();++n) require(std::isfinite(audio.getSample(0,n)),"Nonfinite spectral output");
    const auto v=spectrum(audio,warm);
    const auto name=juce::String(static_cast<int>(rate))+"-"+path+"-d"+juce::String(drive,0)+"-t"+juce::String(threshold,2)+"-"+kind+"-"+juce::String(factor)+"x";
    const auto alias=two ? 0. : aliasDbc(v,bin);
    const auto peak=*std::max_element(v.begin()+1,v.end());
    const auto fundamental=db(v[bin]);
    summary<<name<<','<<rate<<','<<factor<<','<<path<<','<<drive<<','<<threshold<<','<<kind<<','<<bin<<','<<(two?bin2:0)<<','<<m.getLatencySamples()<<',';
    if(!two) summary<<alias;
    summary<<','<<fundamental<<','<<db(peak)<<'\n';
    for(int k=0;k<=frames/2;++k)
    {
        const auto line=name+","+juce::String(k)+","+juce::String(k*rate/frames,9)+","+juce::String(db(v[k]),9)+"\n";
        require(spectra.writeText(line,false,false,nullptr),"Spectrum write");
    }
    auto* result=object(); result->setProperty("id",name); result->setProperty("fundamentalDbfs",fundamental);
    result->setProperty("sampleRate",rate); result->setProperty("factor",static_cast<int>(factor));
    result->setProperty("path",path); result->setProperty("driveDb",drive); result->setProperty("threshold",threshold);
    result->setProperty("kind",kind); result->setProperty("warmupFrames",warm);
    result->setProperty("inputBin",bin); result->setProperty("inputBin2",two?bin2:0);
    result->setProperty("latencySamples",static_cast<int>(m.getLatencySamples()));
    result->setProperty("aliasLowerBoundDbc",two?juce::var():juce::var(alias));
    return juce::var(result);
}

juce::var filterFixture(double rate,unsigned factor,std::ostream& csv)
{
    MiddleSection m;
    require(m.setParameters({{0,0,true},{0,1,0,true}}),"Filter bypass");
    require(m.prepare({rate,127,1},factor,disdorktion::MiddleLatencyCorrection::fir,foldAlgorithm),"Filter prepare");
    juce::AudioBuffer<float> b(1,frames); b.clear(); b.setSample(0,0,1);
    require(m.process(juce::dsp::AudioBlock<float>(b)),"Filter process");
    double maxDb=0,maxPhase=0,maxBlend=0,maxGroup=0,tail=0;
    auto peakIndex=0;
    for(int n=0;n<frames;++n)
    {
        if(std::abs(b.getSample(0,n))>std::abs(b.getSample(0,peakIndex))) peakIndex=n;
        if(n>frames/2) tail+=static_cast<double>(b.getSample(0,n))*b.getSample(0,n);
    }
    require(tail<1e-16,"Impulse response not settled");
    for(int point=0;point<=245;++point)
    {
        const auto normalized=.002*point,omega=2*pi*normalized;
        std::complex<double> h=0, derivative=0;
        for(int n=0;n<frames/2;++n)
        {
            const auto term=static_cast<double>(b.getSample(0,n))*std::polar(1.,-omega*n);
            h+=term; derivative+=std::complex<double>(0,-n)*term;
        }
        const auto aligned=h*std::polar(1.,omega*m.getLatencySamples());
        const auto residual=std::arg(aligned)*180/pi;
        const auto group=-std::imag(derivative/h);
        if(normalized<=.4) { maxDb=std::max(maxDb,std::abs(db(std::abs(h)))); maxPhase=std::max(maxPhase,std::abs(residual)); maxGroup=std::max(maxGroup,std::abs(group-m.getLatencySamples())); }
        for(double mix : {0.,.25,.5,.75,1.})
        {
            const auto blend=db(std::abs((1.-mix)+mix*aligned));
            if(normalized<=.4 && mix==.5) maxBlend=std::max(maxBlend,std::abs(blend));
            csv<<rate<<','<<factor<<','<<normalized*rate<<','<<m.getLatencySamples()<<','<<peakIndex<<','<<db(std::abs(h))<<','<<residual<<','<<group<<','<<mix<<','<<blend<<'\n';
        }
    }
    auto* r=object(); r->setProperty("sampleRate",rate); r->setProperty("factor",static_cast<int>(factor));
    r->setProperty("latencySamples",static_cast<int>(m.getLatencySamples())); r->setProperty("impulsePeakSample",peakIndex);
    r->setProperty("maxMagnitudeDeviationDbTo04Fs",maxDb); r->setProperty("maxResidualPhaseDegreesTo04Fs",maxPhase);
    r->setProperty("maxResidualGroupDelaySamplesTo04Fs",maxGroup); r->setProperty("maxHalfBlendDeviationDbTo04Fs",maxBlend);
    r->setProperty("filterTargetsPassed",maxDb<=.1 && maxPhase<=5 && maxBlend<=.2);
    return juce::var(r);
}

juce::var cpuFixture(double rate,unsigned factor,unsigned channels,int block,const juce::String& path,std::ostream& raw,std::ostream& summary)
{
    MiddleSection m; require(m.setParameters(settings(path,12,.25f)),"CPU settings");
    require(m.prepare({rate,static_cast<juce::uint32>(block),channels},factor,disdorktion::MiddleLatencyCorrection::fir,foldAlgorithm),"CPU prepare");
    juce::AudioBuffer<float> b(static_cast<int>(channels),block),source(static_cast<int>(channels),block);
    for(unsigned c=0;c<channels;++c) for(int n=0;n<block;++n) source.setSample(static_cast<int>(c),n,.25f*std::sin(.13f*n));
    bool ok=true;
    const auto run=[&]() { for(unsigned c=0;c<channels;++c) b.copyFrom(static_cast<int>(c),0,source,static_cast<int>(c),0,block); ok &= m.process(juce::dsp::AudioBlock<float>(b)); };
    for(int i=0;i<128;++i) run();
    std::vector<double> timings;
    const juce::ScopedNoDenormals noDenormals;
    for(int batch=0;batch<21;++batch)
    {
        const auto start=std::chrono::steady_clock::now();
        for(int i=0;i<128;++i) run();
        const auto ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/128.;
        timings.push_back(ns); raw<<rate<<','<<factor<<','<<channels<<','<<block<<','<<path<<','<<batch<<','<<ns<<'\n';
    }
    require(ok,"CPU process");
    // Observable sink keeps actual processing in the timed loop.
    require(std::isfinite(b.getSample(0,block-1)),"CPU sink");
    std::sort(timings.begin(),timings.end()); const auto median=timings[10],p95=timings[19];
    const auto fraction=p95/(1e9*block/rate);
    summary<<rate<<','<<factor<<','<<channels<<','<<block<<','<<path<<','<<median<<','<<p95<<','<<fraction<<'\n';
    auto* r=object(); r->setProperty("sampleRate",rate); r->setProperty("factor",static_cast<int>(factor));
    r->setProperty("channels",static_cast<int>(channels)); r->setProperty("blockSize",block); r->setProperty("path",path);
    r->setProperty("medianBatchMeanNs",median); r->setProperty("p95BatchMeanNs",p95); r->setProperty("p95BatchMeanDeadlineFraction",fraction);
    r->setProperty("localBudgetPassed",fraction<.1); return juce::var(r);
}

bool writeWav(const juce::File& file,const juce::AudioBuffer<float>& b,double rate)
{
    if(file.exists()) return false;
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream()); if(!stream) return false;
    juce::WavAudioFormat wav;
    auto writer=wav.createWriterFor(stream,juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(2)
        .withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    return writer && writer->writeFromAudioSampleBuffer(b,0,b.getNumSamples()) && writer->flush();
}

juce::var listeningFixture(const juce::File& directory,const juce::String& path,float drive,float threshold,const juce::String& kind)
{
    constexpr double rate=48000; constexpr int count=3*48000,warm=4800;
    juce::AudioBuffer<float> input(2,warm+count+512);
    for(int n=0;n<input.getNumSamples();++n)
    {
        const auto t=static_cast<double>(n-warm)/rate;
        double x=0;
        if(n>=warm && n<warm+count)
        {
            if(kind=="tones") x=.125*(std::sin(2*pi*440*t)+std::sin(2*pi*3700*t));
            else if(kind=="transients")
            {
                const auto local=std::fmod(t,.25);
                x=.25*std::exp(-25*local)*(std::sin(2*pi*220*t)+.5*std::sin(2*pi*2640*t));
            }
            else
            {
                const auto a=std::log(20000./20.)/3.;
                x=.25*std::sin(2*pi*20.*std::expm1(a*t)/a);
            }
        }
        input.setSample(0,n,static_cast<float>(x)); input.setSample(1,n,static_cast<float>(-x));
    }
    std::vector<juce::AudioBuffer<float>> outputs;
    std::vector<double> rms,gains;
    std::vector<unsigned> delays;
    for(unsigned f : {1u,2u,4u,8u,16u})
    {
        MiddleSection m; require(m.setParameters(settings(path,drive,threshold)),"Listening settings");
        require(m.prepare({rate,127,2},f,disdorktion::MiddleLatencyCorrection::fir,foldAlgorithm),"Listening prepare");
        juce::AudioBuffer<float> full; full.makeCopyOf(input);
        require(m.process(juce::dsp::AudioBlock<float>(full)),"Listening process");
        outputs.emplace_back(2,count);
        const auto latency=m.getLatencySamples(); delays.push_back(latency);
        double power=0;
        for(int c=0;c<2;++c) for(int n=0;n<count;++n)
        {
            const auto x=full.getSample(c,warm+n+static_cast<int>(latency));
            require(std::isfinite(x),"Listening nonfinite"); outputs.back().setSample(c,n,x); power+=static_cast<double>(x)*x;
        }
        rms.push_back(std::sqrt(power/(2*count)));
    }
    const auto name=path+"-"+kind+"-d"+juce::String(drive,0)+"-t"+juce::String(threshold,2);
    double peak=0;
    for(std::size_t i=0;i<outputs.size();++i)
    {
        require(rms[i]>1e-8,"Listening silent fixture"); gains.push_back(.1/rms[i]);
        peak=std::max(peak,static_cast<double>(outputs[i].getMagnitude(0,count))*gains[i]);
    }
    const auto scale=std::min(1.,.8/peak);
    juce::Array<juce::var> matches;
    for(std::size_t i=0;i<outputs.size();++i)
    {
        outputs[i].applyGain(static_cast<float>(gains[i]*scale));
        const auto filename=name+"-"+juce::String(1u<<i)+"x.wav";
        require(writeWav(directory.getChildFile(filename),outputs[i],rate),"Listening WAV write");
        double measured=0;
        for(int c=0;c<2;++c) for(int n=0;n<count;++n) { const auto x=outputs[i].getSample(c,n); measured+=static_cast<double>(x)*x; }
        measured=std::sqrt(measured/(2*count));
        require(std::abs(db(measured/(.1*scale)))<.001,"Listening RMS matching error");
        auto* r=object(); r->setProperty("file",filename); r->setProperty("factor",static_cast<int>(1u<<i));
        r->setProperty("originalRms",rms[i]); r->setProperty("gainDb",db(gains[i]*scale));
        r->setProperty("alignedBySamples",static_cast<int>(delays[i])); r->setProperty("matchedRms",measured);
        matches.add(juce::var(r));
    }
    auto* r=object(); r->setProperty("id",name); r->setProperty("sampleRate",rate);
    r->setProperty("path",path); r->setProperty("driveDb",drive); r->setProperty("threshold",threshold);
    r->setProperty("kind",kind); r->setProperty("frames",count); r->setProperty("matches",juce::var(matches)); return juce::var(r);
}
}

int main(int argc,char** argv)
{
    if(argc<2 || argc>3 || (argc==3 && juce::String(argv[2])!="--adaa")) { std::cerr<<"Usage: disdorktion_middle_measurements NEW_OUTPUT_DIRECTORY [--adaa]\n"; return 2; }
    if(argc==3) foldAlgorithm=disdorktion::MiddleFoldAlgorithm::adaaTriangle;
    const juce::String arg(argv[1]); const auto directory=juce::File::isAbsolutePath(arg) ? juce::File(arg)
        : juce::File::getCurrentWorkingDirectory().getChildFile(arg);
    if(directory.exists()) { std::cerr<<"Refusing to overwrite existing output\n"; return 2; }
    try
    {
        require(directory.createDirectory().wasOk(),"Output directory creation");
        std::ofstream summary(directory.getChildFile("alias_summary.csv").getFullPathName().toStdString());
        std::ofstream filter(directory.getChildFile("filter_response.csv").getFullPathName().toStdString());
        std::ofstream cpuRaw(directory.getChildFile("cpu_raw.csv").getFullPathName().toStdString());
        std::ofstream cpuSummary(directory.getChildFile("cpu_summary.csv").getFullPathName().toStdString());
        require(summary.good() && filter.good() && cpuRaw.good() && cpuSummary.good(),"CSV open");
        summary<<std::setprecision(12); filter<<std::setprecision(12); cpuRaw<<std::setprecision(12); cpuSummary<<std::setprecision(12);
        summary<<"id,sample_rate,factor,path,drive_db,threshold,kind,bin,bin2,latency,alias_lower_bound_dbc,fundamental_dbfs,peak_dbfs\n";
        filter<<"sample_rate,factor,frequency_hz,latency_samples,impulse_peak_sample,magnitude_db,residual_phase_deg,group_delay_samples,mix,blend_db\n";
        cpuRaw<<"sample_rate,factor,channels,block_size,path,batch,batch_mean_ns\n";
        cpuSummary<<"sample_rate,factor,channels,block_size,path,median_batch_mean_ns,p95_batch_mean_ns,p95_batch_mean_deadline_fraction\n";
        juce::Array<juce::var> spectra,filters,cpu,listening;
        {
            juce::FileOutputStream raw(directory.getChildFile("spectra.csv.gz")); require(raw.openedOk(),"Spectra open");
            {
                juce::GZIPCompressorOutputStream zipped(raw,6,31);
                require(zipped.writeText("id,bin,frequency_hz,amplitude_dbfs\n",false,false,nullptr),"Spectrum header");
                const juce::ScopedNoDenormals noDenormals;
                for(double rate : {44100.,48000.,96000.}) for(unsigned f : {1u,2u,4u,8u,16u})
                {
                    filters.add(filterFixture(rate,f,filter));
                    for(const juce::String path : {juce::String("tanh"),juce::String("folder"),juce::String("cascade")})
                        for(float drive : {0.f,12.f,36.f}) for(float threshold : {1.f,.25f})
                    {
                        if(path=="tanh" && threshold!=1.f) continue;
                        for(const juce::String kind : {juce::String("sine-low"),juce::String("sine-high"),juce::String("two-low"),juce::String("two-high")})
                            spectra.add(spectralFixture(rate,f,path,drive,threshold,kind,summary,zipped));
                    }
                }
                zipped.flush();
            }
            raw.flush(); require(raw.getStatus().wasOk(),"Spectra close");
        }
        for(double rate : {48000.,96000.}) for(unsigned f : {1u,2u,4u,8u}) for(unsigned channels : {1u,2u}) for(int block : {32,128,512})
            for(const juce::String path : {juce::String("tanh"),juce::String("folder"),juce::String("cascade")})
                cpu.add(cpuFixture(rate,f,channels,block,path,cpuRaw,cpuSummary));
        for(const juce::String path : {juce::String("tanh"),juce::String("folder"),juce::String("cascade")})
            for(float drive : {12.f,36.f}) for(const juce::String kind : {juce::String("tones"),juce::String("transients"),juce::String("sweep")})
                listening.add(listeningFixture(directory,path,drive,path=="tanh" ? 1.f : .25f,kind));
        summary.flush(); filter.flush(); cpuRaw.flush(); cpuSummary.flush();
        require(summary.good() && filter.good() && cpuRaw.good() && cpuSummary.good(),"CSV final write");
        auto* report=object(); report->setProperty("schemaVersion",1); report->setProperty("tool","disdorktion_middle_measurements");
        report->setProperty("buildId",DISDORKTION_BUILD_ID); report->setProperty("gitRevision",DISDORKTION_GIT_REVISION);
        report->setProperty("juceVersion",juce::SystemStats::getJUCEVersion()); report->setProperty("createdUtc",juce::Time::getCurrentTime().toISO8601(true));
#if defined(_MSC_FULL_VER)
        report->setProperty("compiler","MSVC " + juce::String(_MSC_FULL_VER));
#elif defined(__clang__)
        report->setProperty("compiler","Clang " __clang_version__);
#elif defined(__GNUC__)
        report->setProperty("compiler","GCC " __VERSION__);
#else
        report->setProperty("compiler","Unknown");
#endif
        report->setProperty("latencyCorrection","FIR windowed-sinc 65 taps, Kaiser beta 8.6, center 32+ceil(raw latency)-raw latency, unity DC; omitted when raw latency is integer.");
        report->setProperty("workingTreeEvaluation",true);
        report->setProperty("foldAlgorithm",foldAlgorithm==disdorktion::MiddleFoldAlgorithm::adaaTriangle ? "first-order ADAA triangle with internal 3-tap compensation" : "native triangle");
        report->setProperty("operatingSystem",juce::SystemStats::getOperatingSystemName());
        report->setProperty("cpuModel",juce::SystemStats::getCpuModel()); report->setProperty("cpuCores",juce::SystemStats::getNumCpus());
        report->setProperty("fftFrames",frames); report->setProperty("spectralWindow","Rectangular; coherent warm steady-state; peak-amplitude normalization");
        report->setProperty("aliasMetric","Non-legitimate-bin power relative to output fundamental; lower bound excludes true in-band odd harmonics and DC/Nyquist; no collisions through odd order 127. Two-tone IM and aliasing are not separated.");
        report->setProperty("referenceStatus","16x is a higher-rate comparison, not ground truth or guaranteed alias-free reference.");
        report->setProperty("cpuProtocol","128 warmup blocks; 21 batches of 128; nearest-rank p95 batch mean; preallocated input copy included; no real-time priority/power policy control; not per-callback deadline tails.");
        report->setProperty("listeningStatus","Pending human listening; synthetic fixtures are not observations. Whole-signal RMS matched and integer-delay aligned; phase differences remain.");
        report->setProperty("spectra",juce::var(spectra)); report->setProperty("filters",juce::var(filters));
        report->setProperty("cpu",juce::var(cpu)); report->setProperty("listening",juce::var(listening));
        require(directory.getChildFile("result.json").replaceWithText(juce::JSON::toString(juce::var(report))),"JSON write");
        std::cout<<"Measured "<<spectra.size()<<" spectral cases, "<<filters.size()<<" filter responses, "<<cpu.size()<<" CPU cases and "<<listening.size()<<" matched listening sets. Human listening pending.\n";
        return 0;
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<"; partial output retained, choose a new destination to retry\n"; return 1; }
}
