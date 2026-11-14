#include "MainComponent.h"

class FugaApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Fuga"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String& commandLine) override
    {
        window = std::make_unique<MainWindow>();

        // Developer aid: `Fuga --snapshot out.png [--demo]` renders the window to an image and quits.
        auto args = juce::StringArray::fromTokens (commandLine, true);
        const int i = args.indexOf ("--snapshot");
        if (i >= 0 && i + 1 < args.size())
        {
            const auto out = juce::File::getCurrentWorkingDirectory().getChildFile (args[i + 1].unquoted());
            const bool demo = args.contains ("--demo");
            juce::Timer::callAfterDelay (700, [this, out, demo]
            {
                if (demo) window->main->runDemo();
                juce::Timer::callAfterDelay (500, [this, out]
                {
                    auto image = window->main->createComponentSnapshot (window->main->getLocalBounds(), true, 2.0f);
                    juce::FileOutputStream stream (out);
                    if (stream.openedOk())
                    {
                        stream.setPosition (0);
                        stream.truncate();
                        juce::PNGImageFormat().writeImageToStream (image, stream);
                    }
                    quit();
                });
            });
        }
    }
    void shutdown() override { window.reset(); }

    void systemRequestedQuit() override
    {
        if (window == nullptr) { quit(); return; }
        window->main->confirmDiscard ([] { juce::JUCEApplication::quit(); });
    }

    struct MainWindow : juce::DocumentWindow
    {
        MainWindow() : DocumentWindow ("Fuga", theme::bg, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            main = new MainComponent();
            main->onTitleChanged = [this] (const juce::String& t) { setName (t); };
            setContentOwned (main, true);
            setResizable (true, true);
            setResizeLimits (900, 560, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
        MainComponent* main = nullptr;
    };

private:
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (FugaApplication)
