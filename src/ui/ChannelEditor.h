#pragma once

#include "model/ConsoleModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace vc
{

class ChannelEditor : public juce::Component
{
public:
    explicit ChannelEditor(ConsoleModel& modelToUse);
    ~ChannelEditor() override;

    void setChannel(int channelIndex);
    int getChannel() const noexcept { return index; }
    void refresh();

    void resized() override;

private:
    class Content;

    ConsoleModel& model;
    int index = 0;
    juce::Label channelLabel { {}, "Channel" };
    juce::ComboBox channelBox;
    juce::TextButton linkButton { "Link" };
    juce::TextButton copyButton { "Copy" };
    juce::TextButton pasteButton { "Paste" };
    juce::Viewport viewport;
    std::unique_ptr<Content> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelEditor)
};

} // namespace vc
