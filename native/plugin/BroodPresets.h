#pragma once
#include "PluginProcessor.h"
#include "kit/Presets.h"

// Brood's sounds for the shared preset browser. A preset is every control except the
// footswitches, as normalised values, so it never engages a channel; captured audio is
// not part of it. A factory preset is a whole state: every control goes back to its
// default, then the preset sets what it cares about, in real units through each
// parameter's own conversion.
class BroodPresets final : public kit::PresetLibrary {
public:
    explicit BroodPresets(BroodProcessor& p, juce::File folderToUse = defaultFolder(),
                          const juce::File& oldSlots = defaultOldSlots())
        : kit::PresetLibrary(std::move(folderToUse), "BroodPreset", ".broodpreset"), proc(p) {
        migrate(oldSlots);
    }

    static juce::File defaultFolder() {
        const auto data = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
       #if JUCE_MAC
        return data.getChildFile("Audio/Presets/Brood");
       #else
        return data.getChildFile("Brood").getChildFile("Presets");
       #endif
    }
    static juce::File defaultOldSlots() {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Brood").getChildFile("patches.xml");
    }

    const juce::StringArray& categoryList() const override {
        static const juce::StringArray list {"SPACES", "ECHOES", "LOOPS", "USER"};
        return list;
    }
    int factoryCount() const override { return static_cast<int>(factories().size()); }
    juce::String factoryName(int i) const override { return factories()[static_cast<size_t>(i)].name; }
    juce::String factoryCategory(int i) const override { return factories()[static_cast<size_t>(i)].category; }

    std::unique_ptr<juce::XmlElement> factoryXml(int i) const override {
        const auto& f = factories()[static_cast<size_t>(juce::jlimit(0, factoryCount() - 1, i))];
        auto xml = std::make_unique<juce::XmlElement>("BroodPreset");
        xml->setAttribute("name", f.name);
        xml->setAttribute("category", f.category);
        auto* values = xml->createNewChildElement("PARAMS");
        for (const auto& [id, value] : f.values)
            if (auto* prm = proc.apvts.getParameter(id)) values->setAttribute(id, prm->convertTo0to1(value));
        return xml;
    }

    std::unique_ptr<juce::XmlElement> capture(const juce::String& name) const override {
        auto xml = std::make_unique<juce::XmlElement>("BroodPreset");
        xml->setAttribute("name", name);
        auto* values = xml->createNewChildElement("PARAMS");
        for (auto* prm : proc.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(prm))
                if (isSound(*ranged)) values->setAttribute(ranged->getParameterID(), ranged->getValue());
        return xml;
    }

    void apply(const juce::XmlElement& xml, const juce::String& name) override {
        const auto* values = xml.getChildByName("PARAMS");
        for (auto* prm : proc.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(prm))
                if (isSound(*ranged)) {
                    const auto id = ranged->getParameterID();
                    const auto v = values != nullptr && values->hasAttribute(id) ? static_cast<float>(values->getDoubleAttribute(id)) : ranged->getDefaultValue();
                    ranged->beginChangeGesture();
                    ranged->setValueNotifyingHost(v);
                    ranged->endChangeGesture();
                }
        proc.presetName = name;
    }
    void applyQuietly(const juce::XmlElement& xml, const juce::String& name) override { apply(xml, name); }
    void commit(std::unique_ptr<juce::XmlElement>, const juce::String&) override {}   // the host's undo is the only undo here
    juce::String currentName() const override { return proc.presetName; }
    void setCurrentName(const juce::String& name) override { proc.presetName = name; }

private:
    struct Factory { const char* name; const char* category; std::vector<std::pair<const char*, float>> values; };
    static const std::vector<Factory>& factories() {
        static const std::vector<Factory> list {
            {"A Little Daydream", "SPACES", {}},
            {"Soft Focus", "SPACES", {{"time", .73f}, {"mix", .56f}, {"modWet", .83f}, {"length", .65f}, {"clock", .80f}, {"loopMode", 2}, {"modLoop", .62f}}},
            {"Glass Echo", "ECHOES", {{"time", .46f}, {"mix", .48f}, {"modWet", .70f}, {"wetMode", 1}, {"routing", 0}}},
            {"Little Ghost", "ECHOES", {{"time", .45f}, {"mix", .54f}, {"modWet", .83f}, {"wetMode", 2}, {"clock", .80f}}},
            {"Melt Away", "LOOPS", {{"time", .67f}, {"mix", .58f}, {"length", .35f}, {"modWet", .75f}, {"clock", .70f}, {"modLoop", .57f}, {"routing", 2}, {"loopMode", 2}}},
        };
        return list;
    }

    static bool isSound(const juce::RangedAudioParameter& p) {
        return p.getParameterID() != "wetSwitch" && p.getParameterID() != "loopSwitch";
    }

    // The four user slots became ordinary presets. Runs once: the old file is set aside.
    void migrate(const juce::File& oldSlots) {
        if (!oldSlots.existsAsFile()) return;
        const auto root = juce::XmlDocument::parse(oldSlots);
        if (root != nullptr && root->hasTagName("BROOD_PATCHES") && getFolder().createDirectory()) {
            for (const auto* patch : root->getChildIterator()) {
                const auto name = "User " + patch->getStringAttribute("slot");
                juce::XmlElement xml("BroodPreset");
                xml.setAttribute("name", name);
                xml.setAttribute("category", "USER");
                auto* values = xml.createNewChildElement("PARAMS");
                for (const auto* entry : patch->getChildIterator())
                    values->setAttribute(entry->getStringAttribute("id"), entry->getDoubleAttribute("v"));
                xml.writeTo(getFolder().getChildFile(name + ".broodpreset"));
            }
        }
        oldSlots.moveFileTo(oldSlots.withFileExtension("old"));
    }

    BroodProcessor& proc;
};
