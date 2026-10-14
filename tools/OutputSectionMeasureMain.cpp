#include "OutputSectionReference.h"
#include "dsp/HighPass.h"
#include "dsp/LowPass.h"
#include "harness/OfflineRender.h"
#include <juce_cryptography/juce_cryptography.h>
#include <fstream>
#include <iomanip>
#include <iostream>

using namespace disdorktion;
using namespace disdorktion::measurement::output;
using namespace disdorktion::harness;
namespace
{
struct Totals {double impulse=0,complex=0,toneDb=0,toneAbsolute=0,ordered=0,peak=0,interstage=0,tail=0;unsigned spectra=0,orderedCount=0,tones=0,stress=0,tails=0,fixtures=0;};
std::ofstream csv(const juce::File& root,const char* name,const char* header)
{std::ofstream out(root.getChildFile(name).getFullPathName().toStdString());out<<std::setprecision(17)<<header<<'\n';return out;}
void checked(bool result){if(!result)throw std::runtime_error("DSP or artifact operation rejected");}
bool spectral(const juce::File& root,Totals& t)
{
    auto samples=csv(root,"impulses.csv","case,rate,hp_mode,lp_mode,hp_cutoff,lp_cutoff,frames,max_error,interstage_peak,latency_samples");
    auto spectra=csv(root,"spectra.csv","case,frequency,expected_real,expected_imaginary,measured_real,measured_imaginary,complex_error,phase_radians,phase_defined,group_delay_samples,reference_group_delay_samples,group_delay_error_samples,group_delay_defined,measured_lower_real,measured_lower_imaginary,measured_upper_real,measured_upper_imaginary,latency_samples");
    unsigned id=0;
    for(double rate:rates)for(auto hp:responses)for(auto lp:responses)for(const auto& pair:cutoffs)
    {
        OutputSettings s {{pair[0],false,hp},{pair[1],false,lp}};OutputSection section;
        checked(section.setParameters(s));checked(section.prepare({rate,127,1}));
        juce::AudioBuffer<float> audio(1,impulseFrames);audio.clear();audio.setSample(0,0,1);checked(section.process(juce::dsp::AudioBlock<float>(audio)));
        auto ref=cascade(s,rate);double error=0;
        for(int n=0;n<audio.getNumSamples();++n)error=std::max(error,std::abs(double(audio.getSample(0,n))-ref.next(n==0?1.:0.)));
        const double interstage=section.getPostHighPassPeak();t.impulse=std::max(t.impulse,error);t.interstage=std::max(t.interstage,interstage);
        samples<<id<<','<<rate<<','<<unsigned(hp)<<','<<unsigned(lp)<<','<<pair[0]<<','<<pair[1]<<','<<impulseFrames<<','<<error<<','<<interstage<<','<<section.getLatencySamples()<<'\n';
        for(double f:frequencies(s,rate))
        {
            const auto expected=ref.response(2*pi*f/rate),measured=projection(audio,rate,f);
            const double e=std::abs(expected-measured);t.complex=std::max(t.complex,e);
            const bool phaseDefined=std::abs(expected)>=1e-9&&std::abs(measured)>=1e-9;
            const double angle=2*pi*f/rate,lowerAngle=std::max(0.,angle-1e-6),upperAngle=std::min(pi,angle+1e-6);
            const auto measuredLower=projection(audio,rate,lowerAngle*rate/(2*pi)),measuredUpper=projection(audio,rate,upperAngle*rate/(2*pi));
            const auto referenceLower=ref.response(lowerAngle),referenceUpper=ref.response(upperAngle);
            // Group delay is a measured finite difference of impulse DFT phase.
            // Transmission zeros and deep stopbands do not define useful delay.
            const bool delayDefined=std::abs(expected)>.001&&std::abs(measured)>.001
                &&std::abs(referenceLower)>.001&&std::abs(referenceUpper)>.001
                &&std::abs(measuredLower)>.001&&std::abs(measuredUpper)>.001;
            const double measuredDelay=delayDefined?-std::arg(measuredUpper*std::conj(measuredLower))/(upperAngle-lowerAngle):0.;
            const double referenceDelay=delayDefined?-std::arg(referenceUpper*std::conj(referenceLower))/(upperAngle-lowerAngle):0.;
            spectra<<id<<','<<f<<','<<expected.real()<<','<<expected.imag()<<','<<measured.real()<<','<<measured.imag()<<','<<e<<','
                <<(phaseDefined?std::arg(measured):0.)<<','<<int(phaseDefined)<<','<<measuredDelay<<','<<referenceDelay<<','<<std::abs(measuredDelay-referenceDelay)<<','<<int(delayDefined)<<','
                <<measuredLower.real()<<','<<measuredLower.imag()<<','<<measuredUpper.real()<<','<<measuredUpper.imag()<<','<<section.getLatencySamples()<<'\n';
        }
        ++id;
    }
    t.spectra=id;return samples.good()&&spectra.good();
}
bool ordered(const juce::File& root,Totals& t)
{
    auto out=csv(root,"ordered.csv","case,rate,hp_mode,lp_mode,hp_cutoff,lp_cutoff,bypass,frames,max_error,interstage_peak,latency_samples");unsigned id=0;
    for(double rate:rates)for(auto hp:responses)for(auto lp:responses)for(const auto& pair:cutoffs)for(unsigned bypass=0;bypass<4;++bypass)
    {
        OutputSettings s {{pair[0],bool(bypass&1),hp},{pair[1],bool(bypass&2),lp}};OutputSection section;HighPass high;LowPass low;
        checked(section.setParameters(s));checked(high.setParameters(s.highPass));checked(low.setParameters(s.lowPass));
        checked(section.prepare({rate,32,2}));checked(high.prepare({rate,32,2}));checked(low.prepare({rate,32,2}));
        juce::AudioBuffer<float> audio(2,2051);for(int n=0;n<2051;++n){audio.setSample(0,n,source(0,n,rate));audio.setSample(1,n,source(2,n,rate));}
        auto ref=audio;checked(section.process(juce::dsp::AudioBlock<float>(audio)));checked(high.process(juce::dsp::AudioBlock<float>(ref)));
        const double peak=ref.getMagnitude(0,2051);checked(low.process(juce::dsp::AudioBlock<float>(ref)));
        double error=0;for(int c=0;c<2;++c)for(int n=0;n<2051;++n)error=std::max(error,std::abs(double(audio.getSample(c,n))-ref.getSample(c,n)));
        t.ordered=std::max(t.ordered,error);t.interstage=std::max(t.interstage,peak);
        out<<id++<<','<<rate<<','<<unsigned(hp)<<','<<unsigned(lp)<<','<<pair[0]<<','<<pair[1]<<','<<bypass<<",2051,"<<error<<','<<peak<<','<<section.getLatencySamples()<<'\n';
    }
    t.orderedCount=id;return out.good();
}
bool tones(const juce::File& root,Totals& t)
{
    auto out=csv(root,"tones.csv","case,hp_mode,lp_mode,hp_cutoff,lp_cutoff,frequency,expected_magnitude,measured_magnitude,absolute_error,db_error,db_gate");unsigned id=0;
    // At 48kHz use every response pair and representative wide/narrow/equal/crossed
    // cutoffs; eight tones including both stopbands. Deep attenuation uses absolute error.
    for(auto hp:responses)for(auto lp:responses)for(unsigned pairIndex=0;pairIndex<4;++pairIndex)for(double f:{40.,125.,500.,1000.,2000.,4000.,8000.,16000.})
    {
        const auto pair=cutoffs[pairIndex];OutputSettings s {{pair[0],false,hp},{pair[1],false,lp}};OutputSection section;
        checked(section.setParameters(s));checked(section.prepare({48000,127,1}));
        juce::AudioBuffer<float> audio(1,96000);for(int n=0;n<96000;++n)audio.setSample(0,n,float(.25*std::sin(2*pi*f*n/48000.)));
        checked(section.process(juce::dsp::AudioBlock<float>(audio)));double ss=0,cc=0,sc=0,ys=0,yc=0;
        for(int n=48000;n<96000;++n){double sine=std::sin(2*pi*f*n/48000.),cosine=std::cos(2*pi*f*n/48000.),y=audio.getSample(0,n);ss+=sine*sine;cc+=cosine*cosine;sc+=sine*cosine;ys+=y*sine;yc+=y*cosine;}
        const double d=ss*cc-sc*sc,measured=std::hypot((ys*cc-yc*sc)/d,(yc*ss-ys*sc)/d)/.25;
        const double expected=std::abs(cascade(s,48000).response(2*pi*f/48000.)),error=std::abs(measured-expected);
        const bool gate=expected>.001;const double db=gate?20*std::log10(measured/expected):0.;
        if(gate)t.toneDb=std::max(t.toneDb,std::abs(db));else t.toneAbsolute=std::max(t.toneAbsolute,error);
        out<<id++<<','<<unsigned(hp)<<','<<unsigned(lp)<<','<<pair[0]<<','<<pair[1]<<','<<f<<','<<expected<<','<<measured<<','<<error<<','<<db<<','<<int(gate)<<'\n';
    }
    t.tones=id;return out.good();
}
bool stress(const juce::File& root,Totals& t)
{
    auto out=csv(root,"stability.csv","case,rate,source_kind,blocks,ordered_error,peak,interstage_peak,finite,latency_samples");unsigned id=0;
    for(double rate:rates)for(unsigned kind=0;kind<4;++kind)
    {
        OutputSection section;HighPass high;LowPass low;checked(section.prepare({rate,127,2}));checked(high.prepare({rate,127,2}));checked(low.prepare({rate,127,2}));
        juce::AudioBuffer<float> audio(2,257);double peak=0,interstage=0,error=0;bool finite=true;
        for(unsigned block=0;block<160;++block)
        {
            OutputSettings s {{block%2?20.f:20000.f,block%5==0,responses[block%3]},{block%2?20000.f:20.f,block%7==0,responses[(block/3)%3]}};
            checked(section.setParameters(s));checked(high.setParameters(s.highPass));checked(low.setParameters(s.lowPass));
            for(int n=0;n<257;++n)for(int c=0;c<2;++c)audio.setSample(c,n,(c?-.7f:1.f)*source(kind,block*257+n,rate));
            auto ref=audio;checked(section.process(juce::dsp::AudioBlock<float>(audio)));checked(high.process(juce::dsp::AudioBlock<float>(ref)));
            interstage=std::max(interstage,double(ref.getMagnitude(0,257)));checked(low.process(juce::dsp::AudioBlock<float>(ref)));
            for(int c=0;c<2;++c)for(int n=0;n<257;++n){const double v=audio.getSample(c,n);finite&=std::isfinite(v);peak=std::max(peak,std::abs(v));error=std::max(error,std::abs(v-ref.getSample(c,n)));}
        }
        checked(finite);t.peak=std::max(t.peak,peak);t.interstage=std::max(t.interstage,interstage);t.ordered=std::max(t.ordered,error);
        out<<id++<<','<<rate<<','<<kind<<",160,"<<error<<','<<peak<<','<<interstage<<','<<int(finite)<<','<<section.getLatencySamples()<<'\n';
    }
    t.stress=id;
    auto tails=csv(root,"tails.csv","case,hp_mode,lp_mode,bypass,rate,silence_frames,tail_peak,latency_samples");id=0;
    for(auto hp:responses)for(auto lp:responses)for(unsigned bypass=0;bypass<4;++bypass)
    {
        OutputSection section;checked(section.setParameters({{20,bool(bypass&1),hp},{20,bool(bypass&2),lp}}));checked(section.prepare({384000,32,1}));
        juce::AudioBuffer<float> audio(1,768000);audio.clear();audio.setSample(0,0,1);checked(section.process(juce::dsp::AudioBlock<float>(audio)));
        checked(section.setParameters({{20,false,hp},{20,false,lp}}));juce::AudioBuffer<float> after(1,3840);after.clear();checked(section.process(juce::dsp::AudioBlock<float>(after)));
        const double peak=after.getMagnitude(0,3840);t.tail=std::max(t.tail,peak);
        tails<<id++<<','<<unsigned(hp)<<','<<unsigned(lp)<<','<<bypass<<",384000,768000,"<<peak<<','<<section.getLatencySamples()<<'\n';
    }
    t.tails=id;return out.good()&&tails.good();
}
void fixtures(const juce::File& root,Totals& t)
{
    for(auto hp:responses)for(auto lp:responses)for(unsigned index=0;index<4;++index)for(unsigned kind=0;kind<2;++kind)
    {
        ExperimentRecord record;record.version=10;record.targetId="output-section";
        record.output={{cutoffs[index][0],false,hp},{cutoffs[index][1],false,lp}};
        record.source.kind=kind?SourceKind::impulse:SourceKind::sweep;record.source.frequency=20;record.source.frequency2=20000;
        record.source.amplitude=.25;record.source.durationSamples=144000;record.renderDurationSamples=144000;
        record.observations="Human listening pending. Compare wide/narrow/equal/crossed cutoffs and response pairs at matched monitor level. Algorithmic latency is zero; group delay is frequency dependent.";
        const auto name="fixture-"+juce::String(int(t.fixtures));const auto destination=root.getChildFile(name);
        auto result=renderExperiment(record,destination);
        for(unsigned attempt=1;attempt<3&&result.status!=RenderStatus::success
            &&result.message=="Cannot publish complete result directory"&&!destination.exists();++attempt)
        {
            // Windows scanners may briefly hold a newly closed staging file.
            // Retry only an unpublished directory; never replace a destination.
            juce::Thread::sleep(100);result=renderExperiment(record,destination);
        }
        if(result.status!=RenderStatus::success)throw std::runtime_error(result.message.toStdString());++t.fixtures;
    }
}
juce::String hash(const juce::File& file)
{checked(file.existsAsFile());juce::FileInputStream stream(file);checked(stream.openedOk());return juce::SHA256(stream).toHexString();}
}
int main(int argc,char** argv)
{
    if(argc!=2){std::cerr<<"Usage: disdorktion_output_measurements NEW_OUTPUT_DIRECTORY\n";return 2;}
    try
    {
        const auto root=juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
        if(root.exists()||root.createDirectory().failed())throw std::runtime_error("Output directory must be fresh");
        const juce::ScopedNoDenormals noDenormals;Totals t;
        std::cout<<"Independent impulse/complex cascade matrix\n";checked(spectral(root,t));
        std::cout<<"Ordered bypass matrix and stepped sines\n";checked(ordered(root,t));checked(tones(root,t));
        std::cout<<"Unit peak automation, tails, and deterministic listening fixtures\n";checked(stress(root,t));fixtures(root,t);
        auto* object=new juce::DynamicObject;juce::var report(object);
        object->setProperty("schemaVersion",1);object->setProperty("build",DISDORKTION_BUILD_ID);
        object->setProperty("reference","independent double direct-form analog-pole BLT cascade; HP output rounded to float before LP");
        object->setProperty("listening","pending human evaluation");object->setProperty("referenceHardwareQualified",false);
        object->setProperty("latencySamples",0);object->setProperty("groupDelayPolicy","measured impulse-DFT phase derivative over +/-1e-6 radians; reference BLT derivative reported separately; undefined unless base and neighbor measured/reference magnitudes all exceed -60dB; diagnostic delay errors have no separate acceptance bound; phase is undefined below 1e-9");
        object->setProperty("staticCases",int(t.spectra));object->setProperty("orderedCases",int(t.orderedCount));object->setProperty("toneCases",int(t.tones));object->setProperty("stressCases",int(t.stress));object->setProperty("tailCases",int(t.tails));object->setProperty("fixtureCases",int(t.fixtures));
        object->setProperty("maxImpulseError",t.impulse);object->setProperty("maxComplexError",t.complex);object->setProperty("maxToneDbError",t.toneDb);object->setProperty("maxToneAbsoluteError",t.toneAbsolute);object->setProperty("maxOrderedError",t.ordered);object->setProperty("maxOutputPeak",t.peak);object->setProperty("maxInterstagePeak",t.interstage);object->setProperty("maxTailPeak",t.tail);
        object->setProperty("executablePath",juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName());object->setProperty("executableSha256",hash(juce::File::getSpecialLocation(juce::File::currentExecutableFile)));
        auto* sources=new juce::DynamicObject;const juce::File sourceRoot(DISDORKTION_SOURCE_ROOT);
        for(const auto* path:{"tools/OutputSectionMeasureMain.cpp","tools/OutputSectionReference.h","src/dsp/OutputSection.h","src/dsp/OutputSection.cpp","src/dsp/CutoffFilter.h","src/harness/AuditionEngine.cpp","src/harness/OutputSectionTarget.h","src/harness/OfflineRender.cpp","src/harness/ExperimentRecord.cpp","src/harness/SignalSource.cpp","scripts/validate_output_measurements.py"})sources->setProperty(path,hash(sourceRoot.getChildFile(path)));
        object->setProperty("sourceRoot",sourceRoot.getFullPathName());object->setProperty("sourceSha256",juce::var(sources));
        const bool pass=t.spectra==staticCases&&t.orderedCount==orderedCases&&t.tones==288&&t.stress==24&&t.tails==36&&t.fixtures==72&&t.impulse<=6e-6&&t.complex<=6e-5&&t.toneDb<=.1&&t.toneAbsolute<=6e-5&&t.ordered<=1e-6&&t.peak<8&&t.interstage<=4&&t.tail<1e-6;
        object->setProperty("passed",pass);checked(root.getChildFile("result.json").replaceWithText(juce::JSON::toString(report)));
        auto* artifacts=new juce::DynamicObject;juce::Array<juce::File> files;root.findChildFiles(files,juce::File::findFiles,true);
        for(const auto& file:files)artifacts->setProperty(file.getRelativePathFrom(root).replaceCharacter('\\','/'),hash(file));
        checked(root.getChildFile("manifest.json").replaceWithText(juce::JSON::toString(juce::var(artifacts))));
        std::cout<<juce::JSON::toString(report)<<'\n';return pass?0:1;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
