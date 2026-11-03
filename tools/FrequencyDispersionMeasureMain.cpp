#include "dsp/FrequencyDispersion.h"
#include "CombBenchmarkPlatform.h"
#include <juce_cryptography/juce_cryptography.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <complex>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <array>
#include <vector>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace {
using namespace disdorktion;
constexpr double pi=3.14159265358979323846;
void require(bool b){if(!b)throw std::runtime_error("measurement operation or acceptance gate failed");}
juce::var object(){return juce::var(new juce::DynamicObject);}
void put(juce::var& o,const char* key,juce::var v){o.getDynamicObject()->setProperty(key,v);}
void put(juce::var& o,const char* key,const std::string& v){put(o,key,juce::var(juce::String(v)));}
void put(juce::var& o,const char* key,const char* v){put(o,key,juce::var(v));}
std::string hash(const std::filesystem::path& p){return juce::SHA256(juce::File(juce::String(std::filesystem::absolute(p).string()))).toHexString().toStdString();}
void writeJson(const std::filesystem::path& p,const juce::var& o){std::ofstream f(p);f<<juce::JSON::toString(o,true).toStdString()<<'\n';require(bool(f));}
void binary(const std::filesystem::path& p,const std::vector<float>& v){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(v.data()),std::streamsize(v.size()*sizeof(float)));require(bool(f));}
// Standalone mono IEEE float WAV writer. The supported Windows build is little endian.
void wav(const std::filesystem::path& p,const std::vector<float>& v,unsigned rate){std::ofstream f(p,std::ios::binary);auto u32=[&](std::uint32_t x){for(unsigned i=0;i<4;++i)f.put(char((x>>(8*i))&255));};auto u16=[&](unsigned x){f.put(char(x&255));f.put(char(x>>8));};f.write("RIFF",4);u32(36+unsigned(v.size()*4));f.write("WAVEfmt ",8);u32(16);u16(3);u16(1);u32(rate);u32(rate*4);u16(4);u16(32);f.write("data",4);u32(unsigned(v.size()*4));f.write(reinterpret_cast<const char*>(v.data()),std::streamsize(v.size()*4));require(bool(f));}
double coefficient(double hz,double fs){const double t=std::tan(pi*std::min(hz,.45*fs)/fs);return std::clamp((t-1)/(t+1),-.99,.99);}
std::vector<double> frequencies(double rate){std::vector<double> result{20.,500.,20000.};if(rate/4<=20000)result.push_back(rate/4);return result;}
struct Bound {size_t frames;double l1,moment,peak;};
Bound captureBound(double a){const double q=std::abs(a),rho=(1+q)*.5,logC=32*std::log((1-q*rho)/(rho-q));size_t n=64;double tail,moment,peak;
    do {n+=64;peak=std::exp(logC+n*std::log(rho));tail=peak/(1-rho);moment=peak*(double(n)*(1-rho)+rho)/((1-rho)*(1-rho));}while(tail>1e-8||moment>1e-4);
    return {n,tail,moment,peak};}
std::vector<float> render(double rate,const FrequencyDispersionSettings& s,std::vector<float> input,bool irregular=false){FrequencyDispersion dsp;require(dsp.setParameters(s)&&dsp.prepare({rate,127,1}));float* p=input.data();juce::dsp::AudioBlock<float> b(&p,1,input.size());size_t n=0,i=0;while(n<input.size()){const size_t count=std::min(input.size()-n,irregular?std::array<size_t,5>{1,32,257,13,1025}[i++%5]:size_t(127));require(dsp.process(b.getSubBlock(n,count)));n+=count;}return input;}
juce::var provenance(const char* exe){auto p=object();put(p,"executable_sha256",hash(exe));put(p,"dsp_sha256",hash(std::filesystem::path(DISDORKTION_SOURCE_ROOT)/"src/dsp/FrequencyDispersion.h"));put(p,"collector_sha256",hash(std::filesystem::path(DISDORKTION_SOURCE_ROOT)/"tools/FrequencyDispersionMeasureMain.cpp"));put(p,"juce",juce::SystemStats::getJUCEVersion());
#ifdef NDEBUG
put(p,"release",true);
#else
put(p,"release",false);
#endif
return p;}
void measurements(const std::filesystem::path& root,const char* exe){auto report=object();put(report,"schema",1);put(report,"kind","frequency-dispersion-measurements");put(report,"provenance",provenance(exe));put(report,"listening_approved",false);juce::Array<juce::var> rows;bool all=true;int id=0;
    for(double fs:{8000.,44100.,48000.,96000.,192000.,384000.})for(double hz:frequencies(fs))for(float mix:{.5f,1.f}){
        FrequencyDispersionSettings s;s.frequencyHz=float(hz);s.mix=mix;const double a=coefficient(hz,fs);const auto bound=captureBound(a);std::vector<float> impulse(bound.frames);impulse[0]=1;auto wet=render(fs,s,impulse),repeat=render(fs,s,impulse,true);bool ok=wet==repeat;const std::string name="impulse_"+std::to_string(id)+".f32";binary(root/name,wet);
        auto row=object();put(row,"id",id++);put(row,"rate",fs);put(row,"frequency_hz",hz);put(row,"mix",mix);put(row,"coefficient",a);put(row,"effective_hz",fs/pi*std::atan((1+a)/(1-a)));put(row,"frames",juce::int64(bound.frames));put(row,"omitted_l1_bound",bound.l1);put(row,"omitted_first_moment_bound",bound.moment);put(row,"file",name);put(row,"sha256",hash(root/name));put(row,"partition_bit_exact",ok);
        FrequencyDispersion dsp;require(dsp.setParameters(s)&&dsp.prepare({fs,127,1}));put(row,"reported_latency_samples",int(dsp.getLatencySamples()));put(row,"tail_bound_seconds",dsp.getTailSeconds());ok=ok&&dsp.getLatencySamples()==0&&std::abs(dsp.getCoefficient()-a)<1e-13;
        const bool pureDelay=std::abs(a)<1e-15;double delayError=0;if(pureDelay){for(size_t n=0;n<wet.size();++n){const double expected=(1.-mix)*(n==0?1.:0.)+mix*(n==32?1.:0.);delayError=std::max(delayError,std::abs(double(wet[n])-expected));}ok=ok&&delayError<2e-6;}put(row,"pure_delay_case",pureDelay);put(row,"pure_delay_error_max",delayError);
        double complexError=0,magnitudeError=0,groupError=0;juce::Array<juce::var> spectrum;
        for(int k=0;k<=128;++k){const double w=pi*k/128;const auto z=std::polar(1.,-w);const auto A=(a+z)/(1.+a*z);const auto expected=(1.-mix)+double(mix)*std::pow(A,32);std::complex<double> h=0,nh=0,phase=1;
            for(size_t n=0;n<wet.size();++n){h+=double(wet[n])*phase;nh+=double(n)*double(wet[n])*phase;phase*=z;}
            const double ce=std::abs(h-expected),gd=(nh/h).real(),reference=32*(1-a*a)/(1+2*a*std::cos(w)+a*a);complexError=std::max(complexError,ce);
            if(mix==1){magnitudeError=std::max(magnitudeError,std::abs(20*std::log10(std::abs(h))));groupError=std::max(groupError,std::abs(gd-reference));ok=ok&&std::abs(gd-reference)<=std::max(.02,.001*reference);}
            auto bin=object();put(bin,"radians",w);put(bin,"real",h.real());put(bin,"imag",h.imag());if(mix==1){put(bin,"group_delay_samples",gd);put(bin,"expected_group_delay_samples",reference);}spectrum.add(bin);}
        ok=ok&&complexError<=2e-4&&(mix!=1||magnitudeError<=.01);put(row,"complex_error_max",complexError);put(row,"wet_magnitude_error_db_max",magnitudeError);put(row,"group_delay_error_samples_max",groupError);put(row,"spectrum",juce::var(spectrum));
        // Verify the one-second static impulse tail by direct capture, separately from conservative contour bound.
        std::vector<float> tail(size_t(fs*1.1));tail[0]=1;tail=render(fs,s,tail);double peak=0;for(size_t n=size_t(fs);n<tail.size();++n)peak=std::max(peak,std::abs(double(tail[n])));ok=ok&&peak<1e-6;put(row,"peak_after_one_second",peak);put(row,"passed",ok);rows.add(row);all=all&&ok;std::cout<<"static "<<id<<" passed="<<ok<<'\n';}
    put(report,"cases",juce::var(rows));
    juce::Array<juce::var> automated;
    for(double rate:{8000.,44100.,48000.,96000.,192000.,384000.}){
        const size_t count=size_t(rate*.3);std::vector<float> input(count),wet(count);for(size_t n=0;n<count;++n)input[n]=float(.2*(std::sin(2*pi*997*n/rate)+.4*std::sin(2*pi*1499*n/rate)));wet=input;
        FrequencyDispersion dsp;FrequencyDispersionSettings s;s.frequencyHz=20;require(dsp.setParameters(s)&&dsp.prepare({rate,127,1}));juce::Array<juce::var> events;
        for(size_t n=0;n<count;++n){if(n==size_t(rate*.02)||n==size_t(rate*.07)||n==size_t(rate*.15)){
            s.frequencyHz=n==size_t(rate*.02)?20.f:20000.f;s.mix=n==size_t(rate*.02)?.8f:1.f;s.bypass=n==size_t(rate*.15);require(dsp.setParameters(s));auto e=object();put(e,"frame",juce::int64(n));put(e,"frequency_hz",s.frequencyHz);put(e,"mix",s.mix);put(e,"bypass",s.bypass);events.add(e);}
            float* p=wet.data()+n;require(dsp.process(juce::dsp::AudioBlock<float>(&p,1,1)));require(std::isfinite(wet[n]));}
        const auto prefix="automation_"+std::to_string(int(rate));binary(root/(prefix+"_source.f32"),input);binary(root/(prefix+"_wet.f32"),wet);auto row=object();put(row,"rate",rate);put(row,"initial_frequency_hz",20);put(row,"events",juce::var(events));put(row,"source_file",prefix+"_source.f32");put(row,"wet_file",prefix+"_wet.f32");put(row,"source_sha256",hash(root/(prefix+"_source.f32")));put(row,"wet_sha256",hash(root/(prefix+"_wet.f32")));automated.add(row);
    }
    put(report,"automation",juce::var(automated));
    // Reproducible generated source examples; these are fixtures, never human listening observations.
    juce::Array<juce::var> examples;const double fs=48000;const size_t frames=96000;
    for(const std::string name:{"impulse","kick","snare","bright-percussion","bass","two-tone"}){
        std::vector<float> source(frames);std::uint32_t random=1;double phase=0;for(size_t n=0;n<frames;++n){const double t=n/fs;random^=random<<13;random^=random>>17;random^=random<<5;const double noise=double(random)/2147483648.-1.;
            if(name=="impulse")source[n]=n==0?1.f:0.f;
            if(name=="kick"){phase+=2*pi*(45+120*std::exp(-30*t))/fs;source[n]=float(.7*std::sin(phase)*std::exp(-10*t));}
            if(name=="snare")source[n]=float(.5*(noise+.3*std::sin(2*pi*180*t))*std::exp(-15*t));
            if(name=="bright-percussion")source[n]=float(.3*(noise+std::sin(2*pi*7319*t))*std::exp(-35*t));
            if(name=="bass")source[n]=float(.3*(std::sin(2*pi*55*t)+.5*std::sin(2*pi*110*t)+.25*std::sin(2*pi*220*t))*std::exp(-2*t));
            if(name=="two-tone")source[n]=float(.2*(std::sin(2*pi*997*t)+std::sin(2*pi*1499*t)));}
        wav(root/(name+"-source.wav"),source,48000);
        for(float hz:{20.f,500.f,20000.f})for(float mix:{.5f,1.f}){FrequencyDispersionSettings s;s.frequencyHz=hz;s.mix=mix;const auto wet=render(fs,s,source);const auto file=name+"-"+std::to_string(int(hz))+"-mix"+std::to_string(int(mix*100))+".wav";wav(root/file,wet,48000);auto e=object();put(e,"source_file",name+"-source.wav");put(e,"wet_file",file);put(e,"wet_sha256",hash(root/file));put(e,"frequency_hz",hz);put(e,"mix",mix);double energy=0,peak=0;for(float x:wet){energy+=double(x)*x;peak=std::max(peak,std::abs(double(x)));}put(e,"peak",peak);put(e,"rms",std::sqrt(energy/wet.size()));examples.add(e);}}
    put(report,"examples",juce::var(examples));put(report,"passed",all);writeJson(root/"result.json",report);require(all);}
}
int main(int argc,char** argv){try{require(argc==2);const std::filesystem::path root(argv[1]);require(!std::filesystem::exists(root)&&std::filesystem::create_directories(root));measurements(root,argv[0]);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
