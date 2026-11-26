#pragma once

// SPDX-License-Identifier: AGPL-3.0-or-later

#include "model/ConsoleModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace vc
{

// Assigns one FX slot as an insert and edits a 31-band graphic EQ.
class InsertEditor : public juce::Component
{
public:
    explicit InsertEditor(ConsoleModel& modelToUse);
    ~InsertEditor() override;

    void setRoot(const juce::String& newRoot);
    void refresh();
    void resized() override;

private:
    struct Bands;

    ConsoleModel& model;
    juce::String root;
    bool applying = false;
    bool editingRight = false;
    int slot = -1;
    juce::ComboBox slotBox;
    juce::TextButton position { "Pre EQ" };
    juce::ComboBox typeBox;
    juce::TextButton enabled { "On" };
    juce::TextButton side { "Left" };
    juce::TextButton flat { "Flat" };
    juce::Label hint;
    std::unique_ptr<Bands> bands;

    void applySlot(int newSlot);
    void showBands(int type);
    int paramIndex(int band) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InsertEditor)
};

} // namespace vc
