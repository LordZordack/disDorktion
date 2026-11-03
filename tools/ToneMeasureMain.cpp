#include "dsp/Tone.h"
#include <juce_cryptography/juce_cryptography.h>
#include <complex>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>

int main(int argc,char** argv)
{
    using namespace disdorktion;
    try
    {
        if(argc!=2 || std::filesystem::exists(argv[1]))throw std::runtime_error("usage: NEW_DIRECTORY");
        const std::filesystem::path root(argv[1]);std::filesystem::create_directories(root);
        std::ofstream csv(root/"response.csv");csv<<std::setprecision(17)<<"rate,tone_db,frequency,real,imag\n";
        constexpr double pi=3.14159265358979323846;
        for(double rate:{8000.,44100.,48000.,88200.,96000.,192000.,384000.})for(float db:{-6.f,-3.f,0.f,3.f,6.f})
        {
            Tone dsp;if(!dsp.setParameters({db,false}) || !dsp.prepare({rate,127,1}))throw std::runtime_error("prepare");
            const int count=static_cast<int>(std::ceil(rate*.1));juce::AudioBuffer<float> b(1,count);b.clear();b.setSample(0,0,1);
            if(!dsp.process(juce::dsp::AudioBlock<float>(b)))throw std::runtime_error("process");
            for(int i=0;i<=100;++i)
            {
                const double f=i==0?0.:i==99?1000.:i==100?rate*.5:20.*std::pow(std::min(20000.,rate*.45)/20.,double(i-1)/97.);
                std::complex<double> response{};
                for(int n=0;n<count;++n)response+=double(b.getSample(0,n))*std::polar(1.,-2*pi*f*n/rate);
                csv<<rate<<','<<db<<','<<f<<','<<response.real()<<','<<response.imag()<<'\n';
            }
        }
        csv.close();if(!csv)throw std::runtime_error("response write");
        auto report=juce::var(new juce::DynamicObject);auto* o=report.getDynamicObject();
        auto digest=[](const std::filesystem::path& p){return juce::SHA256(juce::File(juce::String(std::filesystem::absolute(p).string()))).toHexString();};
        o->setProperty("kind","tone-response");o->setProperty("schema",1);o->setProperty("latency_samples",0);
        o->setProperty("impulse_seconds",.1);o->setProperty("response_sha256",digest(root/"response.csv"));
        o->setProperty("executable_sha256",digest(argv[0]));
        o->setProperty("dsp_sha256",digest(std::filesystem::path(DISDORKTION_SOURCE_ROOT)/"src/dsp/Tone.h"));
        o->setProperty("collector_sha256",digest(std::filesystem::path(DISDORKTION_SOURCE_ROOT)/"tools/ToneMeasureMain.cpp"));
        std::ofstream json(root/"result.json");json<<juce::JSON::toString(report,true).toStdString();if(!json)throw std::runtime_error("json write");
        return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
