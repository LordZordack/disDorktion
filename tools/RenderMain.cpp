#include "harness/OfflineRender.h"
#include <iostream>

int main(int argc, char* argv[])
{
    juce::String experiment, output;
    const auto usage = [] { std::cout << "Usage: disdorktion_render --experiment <json> --output-dir <directory>\n"; };
    if (argc == 2 && juce::String(argv[1]) == "--help") { usage(); return 0; }
    for (int i = 1; i < argc; ++i)
    {
        const juce::String option(argv[i]);
        if ((option != "--experiment" && option != "--output-dir") || i + 1 >= argc)
        { usage(); return 2; }
        auto& destination = option == "--experiment" ? experiment : output;
        if (destination.isNotEmpty()) { usage(); return 2; }
        destination = argv[++i];
        if (destination.isEmpty() || destination.startsWith("--")) { usage(); return 2; }
    }
    if (experiment.isEmpty() || output.isEmpty()) { usage(); return 2; }
    const auto absolute = [](const juce::String& path)
    { return juce::File::isAbsolutePath(path) ? juce::File(path) : juce::File::getCurrentWorkingDirectory().getChildFile(path); };
    disdorktion::harness::ExperimentRecord record;
    juce::String error;
    if (!disdorktion::harness::ExperimentRecord::load(absolute(experiment), record, error))
    { std::cerr << error << '\n'; return 2; }
    const auto result = disdorktion::harness::renderExperiment(record, absolute(output));
    if (result.status != disdorktion::harness::RenderStatus::success) std::cerr << result.message << '\n';
    return result.status == disdorktion::harness::RenderStatus::success ? 0
         : result.status == disdorktion::harness::RenderStatus::invalidRequest ? 2 : 1;
}
