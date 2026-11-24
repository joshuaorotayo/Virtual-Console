#pragma once

// SPDX-License-Identifier: AGPL-3.0-or-later

#include "model/ConsoleModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace vc
{

class FxEditor : public juce::Component
{
public:
    explicit FxEditor(ConsoleModel& modelToUse);

    void setReturn(int returnIndex);
    void refresh();
    void resized() override;

private:
    ConsoleModel& model;
    int slot = 0;
    int fxReturn = 0;
    bool applying = false;
    juce::Label title;
    juce::Label sendLabel;
    juce::ComboBox typeBox;
    juce::TextButton enabled { "On" };
    std::array<juce::Label, 4> paramLabels;
    std::array<juce::Slider, 4> params;

    void applyType(int type);
    void showParameters(int type);
};

} // namespace vc
