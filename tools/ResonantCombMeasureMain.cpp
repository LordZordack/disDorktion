#include "CombMeasurementSupport.h"
int main(int argc,char** argv)
{
    using namespace combmeasure;
    try
    {
        require(argc==2);std::filesystem::path root(argv[1]);require(!std::filesystem::exists(root));require(std::filesystem::create_directories(root));
        auto report=object();put(report,"schema",1);put(report,"kind","resonant-comb-measurements");put(report,"provenance",provenance(argv[0]));
        put(report,"sample_format","little-endian IEEE754 float32 mono");juce::Array<juce::var> configs;int id=0;
        for(double rate:{8000.,44100.,48000.,96000.,192000.,384000.})
        {
            int scenario=0;
            for(const auto& s:cases(rate))
            {
                ResonantComb comb;require(comb.setParameters(s));require(comb.prepare({rate,127,1}));
                const double tail=comb.getTailSeconds();require(std::isfinite(tail)&&tail>=0&&tail<120);
                const auto delay=std::ceil(rate*s.delayMs*.001);const double a=s.dampingEnabled?std::exp(-2*juce::MathConstants<double>::pi*std::min(double(s.dampingHz),.45*rate)/rate):0.;
                double lo=std::max(a,1e-12),hi=1.;
                if(s.feedback!=0)for(int i=0;i<80;++i){const double q=(lo+hi)/2;if(std::abs(s.feedback)*(1-a)*std::pow(q,-delay)+a/q>1)lo=q;else hi=q;}
                const double rho=s.feedback==0?0.:hi;
                const size_t residualFrames=rho>0?size_t(std::ceil(std::log(1e-9*(1-rho)/4)/std::log(rho))+delay):size_t(delay+2);
                const size_t frames=std::max(residualFrames,size_t(std::ceil((tail+.15)*rate)));auto data=capture(rate,s,frames);const auto repeated=capture(rate,s,frames,true);require(data==repeated);
                const auto filename="impulse_"+std::to_string(id)+".f32";binary(root/filename,data);
                auto c=object();put(c,"id",id++);put(c,"scenario",scenario++);put(c,"rate",rate);put(c,"frames",juce::int64(frames));put(c,"settings",controls(s));
                put(c,"tail_seconds",tail);put(c,"latency_samples",int(comb.getLatencySamples()));put(c,"effective_damping_hz",comb.getEffectiveDampingHz());
                put(c,"file",filename);put(c,"sha256",hash(root/filename));put(c,"irregular_repeat_bit_exact",true);
                juce::Array<juce::var> spectrum;
                const double exactDelay=rate*s.delayMs*.001;
                for(double hz:{0.,rate/(2*exactDelay),rate/exactDelay,rate*0.03125,rate*.125,rate*.249,rate*.45})
                {
                    std::complex<double> z=std::polar(1.,-2*juce::MathConstants<double>::pi*hz/rate),phase=1.,sum=0.;
                    for(float value:data){sum+=double(value)*phase;phase*=z;}
                    auto p=object();put(p,"hz",hz);put(p,"real",sum.real());put(p,"imag",sum.imag());spectrum.add(p);
                }
                put(c,"complex_response",juce::var(spectrum));configs.add(c);std::cout<<"captured "<<id<<" frames="<<frames<<'\n';
            }
        }
        juce::Array<juce::var> automation;
        for(double rate:{8000.,44100.,48000.,96000.,192000.,384000.})
        {
            const size_t frames=size_t(rate*.3);ResonantComb comb;ResonantCombSettings initial;require(comb.setParameters(initial));require(comb.prepare({rate,127,1}));comb.reset();
            std::vector<float> input(frames),output(frames);for(size_t n=0;n<frames;++n)input[n]=float(.2*std::sin(2*juce::MathConstants<double>::pi*997*n/rate));output=input;
            juce::Array<juce::var> events;
            for(size_t n=0;n<frames;++n)
            {
                if(n==size_t(rate*.02)||n==size_t(rate*.07)||n==size_t(rate*.15))
                {
                    auto s=initial;s.delayMs=n<size_t(rate*.1)?40.f:.25f;s.feedback=n<size_t(rate*.05)?-.95f:.95f;s.feedforward=-1;s.mix=.8f;
                    s.dampingEnabled=n>=size_t(rate*.1);s.dampingHz=20;s.bypass=n>=size_t(rate*.15);require(comb.setParameters(s));
                    auto e=object();put(e,"frame",juce::int64(n));put(e,"settings",controls(s));events.add(e);
                }
                float* ptr=output.data()+n;require(comb.process(juce::dsp::AudioBlock<float>(&ptr,1,1)));
            }
            const auto prefix="automation_"+std::to_string(int(rate));binary(root/(prefix+"_source.f32"),input);binary(root/(prefix+"_wet.f32"),output);
            auto c=object();put(c,"rate",rate);put(c,"frames",juce::int64(frames));put(c,"initial_settings",controls(initial));put(c,"events",juce::var(events));
            put(c,"source_file",prefix+"_source.f32");put(c,"source_sha256",hash(root/(prefix+"_source.f32")));put(c,"wet_file",prefix+"_wet.f32");put(c,"wet_sha256",hash(root/(prefix+"_wet.f32")));automation.add(c);
        }
        put(report,"automation",juce::var(automation));put(report,"cases",juce::var(configs));put(report,"listening_approved",false);json(root/"result.json",report);return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
