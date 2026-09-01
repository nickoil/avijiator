#include "PresetBrowserUI.h"

#include <vector>

#include <juce_gui_extra/juce_gui_extra.h>

#include "PresetSerialization.h"
#include "../UI/PanelLookAndFeel.h"

namespace PresetBrowserUI
{
    namespace
    {
        // Shared by both dialogs' Save/Cancel/Load buttons - documents/
        // settings-persistence-design.md section 6 reuses MainComponent's
        // own showAudioSettings() DialogWindow mechanism, whose comment
        // ("Self-owning: the window deletes itself when closed") is exactly
        // what LaunchOptions::launchAsync()'s deleteWhenDismissed modal
        // state gives every window it creates - exitModalState() here is
        // the same call the native close button itself triggers.
        void dismiss (juce::Component& contentChild)
        {
            if (auto* window = contentChild.findParentComponentOfClass<juce::DialogWindow>())
                window->exitModalState (0);
        }

        void launch (juce::Component* content, const juce::String& title, int width, int height)
        {
            content->setSize (width, height);

            juce::DialogWindow::LaunchOptions options;
            options.dialogTitle = title;
            options.content.setOwned (content);
            options.dialogBackgroundColour = PanelLookAndFeel::background;
            options.escapeKeyTriggersCloseButton = true;
            options.useNativeTitleBar = true;
            options.resizable = false;
            options.launchAsync();
        }

        //======================================================================
        // A plain vector bin - handle, lid, body - rather than an embedded
        // image asset, matching this codebase's existing "hand-painted over
        // images" convention (PianoKey::paint, MomentaryButton, TitleMark -
        // see their own comments in SynthPanel.h/.cpp). juce::Button, not
        // juce::TextButton: there is no text to fall back on, only
        // paintButton.
        struct TrashIconButton final : public juce::Button
        {
            TrashIconButton() : juce::Button ("Delete") {}

            void paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override
            {
                const auto colour = shouldDrawButtonAsDown    ? juce::Colours::red
                                    : shouldDrawButtonAsHighlighted ? juce::Colours::orangered
                                                                     : juce::Colours::darkgrey;

                // The button's own frame - a rounded-rectangle outline, so
                // this reads as a clickable button rather than a bare icon
                // floating in the row. Drawn first, at the full bounds
                // (inset by half its own stroke width so the line isn't
                // clipped at the edge); the icon itself is inset further
                // still, so it sits with visible breathing room inside it.
                g.setColour (colour);
                g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 1.2f);

                auto area = getLocalBounds().toFloat().reduced (8.0f);

                // Handle - a short bar centred above the lid.
                const auto handleHeight = area.getHeight() * 0.1f;
                g.fillRect (area.withSizeKeepingCentre (area.getWidth() * 0.35f, handleHeight)
                                .withY (area.getY()));
                area.removeFromTop (handleHeight + 2.0f);

                // Lid - spans the bin's full width, flush with the body's
                // own top edge below it.
                const auto lidHeight = area.getHeight() * 0.14f;
                g.fillRect (area.removeFromTop (lidHeight));
                area.removeFromTop (2.0f);

                // Body - a TRAPEZOID, narrower at the bottom than the top.
                // The first version left this a straight-sided rounded
                // rectangle, which is exactly what read as a cookie jar
                // rather than a bin - the taper is what a trash-can
                // silhouette actually depends on, not the lid/handle alone.
                const auto bottomInset = area.getWidth() * 0.14f;
                const auto bodyBottomLeft = area.getX() + bottomInset;
                const auto bodyBottomRight = area.getRight() - bottomInset;

                juce::Path body;
                body.startNewSubPath (area.getX(), area.getY());
                body.lineTo (area.getRight(), area.getY());
                body.lineTo (bodyBottomRight, area.getBottom());
                body.lineTo (bodyBottomLeft, area.getBottom());
                body.closeSubPath();
                g.fillPath (body);

                // Ridge lines - three verticals, a shade darker so they read
                // as grooves rather than pure decoration. Positioned across
                // the NARROWER bottom edge, not the wider top, so they never
                // poke outside the taper.
                g.setColour (colour.darker (0.4f));
                const auto ridgeTop = area.getY() + area.getHeight() * 0.15f;
                const auto ridgeBottom = area.getBottom() - area.getHeight() * 0.1f;
                for (int i = 1; i <= 3; ++i)
                {
                    const auto x = bodyBottomLeft + (bodyBottomRight - bodyBottomLeft) * ((float) i / 4.0f);
                    g.drawLine (x, ridgeTop, x, ridgeBottom, 1.3f);
                }
            }
        };

        //======================================================================
        // "Are you sure?" - a plain DialogWindow, same launch()/dismiss()
        // mechanism as the Save/Load dialogs, rather than juce::AlertWindow/
        // NativeMessageBox - keeps every dialog in this file on one proven
        // mechanism instead of introducing a second one this project hasn't
        // used before.
        class ConfirmDeleteContent final : public juce::Component
        {
        public:
            ConfirmDeleteContent (const juce::String& presetName, std::function<void()> onConfirmedIn)
                : onConfirmed (std::move (onConfirmedIn))
            {
                messageLabel.setText ("Delete \"" + presetName + "\"?", juce::dontSendNotification);
                messageLabel.setJustificationType (juce::Justification::centred);
                addAndMakeVisible (messageLabel);

                deleteButton.setButtonText ("Delete");
                deleteButton.onClick = [this]
                {
                    if (onConfirmed != nullptr)
                        onConfirmed();
                    dismiss (*this);
                };
                addAndMakeVisible (deleteButton);

                cancelButton.setButtonText ("Cancel");
                cancelButton.onClick = [this] { dismiss (*this); };
                addAndMakeVisible (cancelButton);
            }

            void resized() override
            {
                auto area = getLocalBounds().reduced (12);
                messageLabel.setBounds (area.removeFromTop (area.getHeight() - 28));

                auto buttonRow = area;
                cancelButton.setBounds (buttonRow.removeFromRight (80));
                buttonRow.removeFromRight (8);
                deleteButton.setBounds (buttonRow.removeFromRight (80));
            }

        private:
            std::function<void()> onConfirmed;
            juce::Label messageLabel;
            juce::TextButton deleteButton, cancelButton;
        };

        void confirmAndDelete (const juce::File& file, std::function<void()> onDeleted)
        {
            launch (new ConfirmDeleteContent (file.getFileNameWithoutExtension(), std::move (onDeleted)),
                    "Delete Preset", 300, 110);
        }

        //======================================================================
        class SaveDialogContent final : public juce::Component
        {
        public:
            SaveDialogContent (VoiceParameters& paramsIn, const Arpeggiator& arpIn)
                : params (paramsIn), arp (arpIn)
            {
                promptLabel.setText ("Preset name", juce::dontSendNotification);
                addAndMakeVisible (promptLabel);

                addAndMakeVisible (nameEditor);

                errorLabel.setColour (juce::Label::textColourId, juce::Colours::orangered);
                errorLabel.setJustificationType (juce::Justification::centredLeft);
                addAndMakeVisible (errorLabel);

                saveButton.setButtonText ("Save");
                saveButton.onClick = [this] { attemptSave(); };
                addAndMakeVisible (saveButton);

                cancelButton.setButtonText ("Cancel");
                cancelButton.onClick = [this] { dismiss (*this); };
                addAndMakeVisible (cancelButton);
            }

            void resized() override
            {
                auto area = getLocalBounds().reduced (12);
                promptLabel.setBounds (area.removeFromTop (20));
                nameEditor.setBounds (area.removeFromTop (28));
                area.removeFromTop (6);
                errorLabel.setBounds (area.removeFromTop (32));

                auto buttonRow = area.removeFromBottom (28);
                cancelButton.setBounds (buttonRow.removeFromRight (80));
                buttonRow.removeFromRight (8);
                saveButton.setBounds (buttonRow.removeFromRight (80));
            }

        private:
            void attemptSave()
            {
                const auto name = nameEditor.getText().trim();

                if (name.isEmpty())
                {
                    errorLabel.setText ("Enter a name.", juce::dontSendNotification);
                    return;
                }

                const auto folder = PresetSerialization::getPresetsFolder();
                const auto file = folder.getChildFile (name + PresetSerialization::fileExtension);

                // No silent overwrite, no auto-suffixing - rejected inline,
                // dialog stays open, per section 6's explicit user call.
                if (file.existsAsFile())
                {
                    errorLabel.setText ("A preset named \"" + name + "\" already exists"
                                             " - choose a different name.",
                                         juce::dontSendNotification);
                    return;
                }

                folder.createDirectory();
                file.replaceWithText (PresetSerialization::toXml (params, arp)->toString());
                dismiss (*this);
            }

            VoiceParameters& params;
            const Arpeggiator& arp;

            juce::Label promptLabel, errorLabel;
            juce::TextEditor nameEditor;
            juce::TextButton saveButton, cancelButton;
        };

        //======================================================================
        class LoadDialogContent final : public juce::Component
        {
        public:
            LoadDialogContent (VoiceParameters& paramsIn, Arpeggiator& arpIn, std::function<void()> onLoadedIn)
                : params (paramsIn), arp (arpIn), onLoaded (std::move (onLoadedIn))
            {
                addAndMakeVisible (viewport);
                viewport.setViewedComponent (&listContainer, false);
                refreshList();

                openFolderButton.onClick = [] { openPresetsFolder(); };
                addAndMakeVisible (openFolderButton);
            }

            void resized() override
            {
                auto area = getLocalBounds();
                auto bottomRow = area.removeFromBottom (32).reduced (8, 2);
                openFolderButton.setBounds (bottomRow);

                viewport.setBounds (area);
                layoutRows();
            }

        private:
            static void openPresetsFolder()
            {
                const auto folder = PresetSerialization::getPresetsFolder();
                folder.createDirectory(); // harmless if it already exists - guards a not-yet-first-launch edge case
                folder.startAsProcess();  // opens in Explorer (or the OS's default handler elsewhere)
            }

            struct Row
            {
                juce::File file;
                juce::Label label;
                juce::TextButton loadButton { "Load" };
                TrashIconButton deleteButton;
            };

            static constexpr int rowHeight = 32;

            void refreshList()
            {
                rows.clear(); // destroys the old rows' Components - clears listContainer's children too

                const auto pattern = juce::String ("*") + PresetSerialization::fileExtension;
                for (const auto& file : PresetSerialization::getPresetsFolder()
                                             .findChildFiles (juce::File::findFiles, false, pattern))
                {
                    auto row = std::make_unique<Row>();
                    row->file = file;
                    row->label.setText (file.getFileNameWithoutExtension(), juce::dontSendNotification);
                    listContainer.addAndMakeVisible (row->label);

                    row->loadButton.onClick = [this, file] { loadAndClose (file); };
                    listContainer.addAndMakeVisible (row->loadButton);

                    row->deleteButton.onClick = [this, file] { confirmAndDelete (file, [this] { refreshList(); }); };
                    listContainer.addAndMakeVisible (row->deleteButton);

                    rows.push_back (std::move (row));
                }

                layoutRows();
            }

            void layoutRows()
            {
                const auto width = juce::jmax (1, viewport.getWidth() - viewport.getScrollBarThickness());
                listContainer.setSize (width, juce::jmax (rowHeight, rowHeight * (int) rows.size()));

                auto y = 0;
                for (auto& row : rows)
                {
                    auto area = juce::Rectangle<int> (0, y, width, rowHeight).reduced (2);
                    row->deleteButton.setBounds (area.removeFromRight (28));
                    area.removeFromRight (8);
                    row->loadButton.setBounds (area.removeFromRight (56));
                    row->label.setBounds (area);
                    y += rowHeight;
                }
            }

            void loadAndClose (const juce::File& file)
            {
                if (auto xml = juce::parseXML (file))
                {
                    if (xml->hasTagName (PresetSerialization::rootTagName))
                    {
                        PresetSerialization::fromXml (*xml, params, arp);

                        // fromXml only writes the VoiceParameters/Arpeggiator
                        // atomics - exactly right for the audio thread, but
                        // it leaves every slider/combo/toggle on whatever
                        // panel called this showing its PRE-load position
                        // until something pushes the new values back into
                        // them. See SynthPanel::refreshControlsFromParameters'
                        // own comment for the bug report that found this gap.
                        if (onLoaded != nullptr)
                            onLoaded();
                    }
                }

                dismiss (*this);
            }

            VoiceParameters& params;
            Arpeggiator& arp;
            std::function<void()> onLoaded;

            juce::Viewport viewport;
            juce::Component listContainer;
            std::vector<std::unique_ptr<Row>> rows;
            juce::TextButton openFolderButton { "Open Presets Folder" };
        };
    }

    void showSaveDialog (VoiceParameters& params, const Arpeggiator& arp)
    {
        launch (new SaveDialogContent (params, arp), "Save Preset", 340, 130);
    }

    void showLoadDialog (VoiceParameters& params, Arpeggiator& arp, std::function<void()> onLoaded)
    {
        // 320 (the list's own viewport) + 32 for the "Open Presets Folder"
        // row LoadDialogContent::resized() carves off the bottom.
        launch (new LoadDialogContent (params, arp, std::move (onLoaded)), "Load Preset", 360, 352);
    }

    //==========================================================================
    // Section 9. Content is deliberately small - the doc leaves "which
    // patches, how many" as implementation-time work, not fixed there.
    void writeFactoryPresetsIfMissing()
    {
        const auto folder = PresetSerialization::getPresetsFolder();
        if (folder.exists())
            return; // not first launch - never overwrite what the user has since edited/deleted

        folder.createDirectory();

        // "Default" - every field at its own in-class-initializer default,
        // useful as a known-good reset point. A fresh VoiceParameters/
        // Arpeggiator already ARE that default, so this is toXml applied
        // straight to freshly-constructed instances.
        {
            VoiceParameters defaults;
            Arpeggiator arp;
            folder.getChildFile (juce::String ("Default") + PresetSerialization::fileExtension)
                .replaceWithText (PresetSerialization::toXml (defaults, arp)->toString());
        }

        // "Wide Saw Arp" - a simple, audibly-distinct second patch so the
        // Load dialog isn't a list of one on first launch: a brighter,
        // slightly resonant saw with the arpeggiator already running.
        {
            VoiceParameters wideSawArp;
            wideSawArp.sawLevel.store (1.0f, std::memory_order_relaxed);
            wideSawArp.pulseLevel.store (0.0f, std::memory_order_relaxed);
            wideSawArp.cutoffLog2Hz.store (std::log2 (3500.0f), std::memory_order_relaxed);
            wideSawArp.resonance.store (0.35f, std::memory_order_relaxed);
            wideSawArp.releaseSeconds.store (0.6f, std::memory_order_relaxed);
            wideSawArp.arpEnabled.store (1, std::memory_order_relaxed);
            wideSawArp.arpPattern.store ((int) ArpPattern::UpDown, std::memory_order_relaxed);

            Arpeggiator arp;
            folder.getChildFile (juce::String ("Wide Saw Arp") + PresetSerialization::fileExtension)
                .replaceWithText (PresetSerialization::toXml (wideSawArp, arp)->toString());
        }
    }
}
