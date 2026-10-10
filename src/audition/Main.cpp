#include "MainComponent.h"

namespace disdorktion::audition
{
class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "DisDorktion Audition"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void initialise(const juce::String&) override { window = std::make_unique<Window>(); }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted(const juce::String&) override {}
private:
    class Window final : public juce::DocumentWindow
    {
    public:
        Window() : DocumentWindow("DisDorktion Audition", juce::Colour(0xff20252c), allButtons)
        {
            setUsingNativeTitleBar(true); setContentOwned(new MainComponent(), true);
            setResizable(true, false); setResizeLimits(850, 680, 1600, 1200);
            centreWithSize(getWidth(), getHeight()); setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
};
}
START_JUCE_APPLICATION(disdorktion::audition::Application)
