#pragma once

#include "model/ConsoleModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace vc
{

// One shared channel surface. Selecting another channel keeps the current page.
class ChannelEditor : public juce::Component
{
public:
    explicit ChannelEditor(ConsoleModel& modelToUse);
    ~ChannelEditor() override;

    void setChannel(int channelIndex);
    int getChannel() const noexcept;
    void refresh();
    void setSourceNames(const juce::StringArray& names);

    std::function<void(int)> onChannelChanged;

    void resized() override;

private:
    class Surface;

    ConsoleModel& model;
    std::unique_ptr<Surface> surface;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelEditor)
};

} // namespace vc
