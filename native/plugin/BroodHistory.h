#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <deque>
#include <functional>
#include <map>
#include <vector>
#include "dsp/brood.h"

// Brood's UNDO and REDO: up to four actions, in the order they happened, whatever they were.
//
// One action is one finished gesture on Brood's own controls (a knob from press to release,
// a click on a switch, a modulation depth dragged), a burst of one MIDI controller, a preset
// loaded, or something done to the loop (a new take, a layer, a CLEAR, a RESAMPLE) together
// with every control Brood moved for it. Host automation and the host's own controls arrive
// without Brood's gesture and aren't recorded: the host keeps its own undo for those, and
// automation playing back would push four real steps off the end in a second.
//
// Message thread only. The cores report their versions of the take through the processor
// (versionEvent), and the history tells them which to swap back (swapVersion / releaseVersion
// hand a command to the audio thread).
class BroodHistory final : private juce::AudioProcessorParameter::Listener {
public:
    static constexpr int kDepth = 4;

    struct Change { juce::String id; float before = 0, after = 0; };
    struct Entry {
        std::vector<Change> changes;
        int audioId = -1;          // a version of the take the cores swap, or -1
        juce::String label;        // what the screen says when it's undone
        int midiCc = -1;           // a MIDI burst still open for more of the same controller
        juce::uint32 lastTouch = 0;
    };

    // `params`: every parameter the history may record; `excluded`: ids that are gestures, not settings.
    BroodHistory(juce::AudioProcessorValueTreeState& state, juce::StringArray excludedIds,
                 std::function<void(int)> swapVersion, std::function<void(int)> releaseVersion)
        : apvts(state), excluded(std::move(excludedIds)), swap(std::move(swapVersion)), release(std::move(releaseVersion)) {
        for (auto* p : apvts.processor.getParameters()) p->addListener(this);
    }
    ~BroodHistory() override {
        for (auto* p : apvts.processor.getParameters()) p->removeListener(this);
    }

    int undoDepth() const { return static_cast<int>(undoStack.size()); }
    int redoDepth() const { return static_cast<int>(redoStack.size()); }
    juce::String nextUndoLabel() const { return undoStack.empty() ? juce::String() : undoStack.back().label; }
    juce::String nextRedoLabel() const { return redoStack.empty() ? juce::String() : redoStack.back().label; }

    // UNDO / REDO: the controls go back (or forward) through the host, as gestures, so a host
    // writing automation hears them; a version of the take is swapped by the cores.
    bool undo() { return step(true); }
    bool redo() { return step(false); }

    // A preset loaded, or anything else that moves many controls as one action.
    void beginGroup(const juce::String& label) { if (groupDepth++ == 0) { group = Entry{}; group.label = label; } }
    void endGroup() {
        if (groupDepth == 0 || --groupDepth > 0) return;
        if (!group.changes.empty() || group.audioId >= 0) push(std::move(group));
        group = Entry{};
    }

    // A controller moved a parameter (audio thread hands these over through the processor). A
    // burst of the same controller is one action until it's been still for half a second.
    void midiMoved(int cc, const juce::String& id, float before, float after) {
        if (applying || excluded.contains(id)) return;
        const auto now = juce::Time::getMillisecondCounter();
        if (!undoStack.empty()) {
            Entry& top = undoStack.back();
            if (top.midiCc == cc && now - top.lastTouch < 500 && top.changes.size() == 1 && top.changes[0].id == id) {
                top.changes[0].after = after; top.lastTouch = now;
                return;
            }
        }
        Entry e; e.changes.push_back({id, before, after}); e.label = labelOf(id); e.midiCc = cc; e.lastTouch = now;
        push(std::move(e));
    }

    // The cores' news (see brood::VersionEvent). `autos` are applied here, with the action.
    void versionEvent(const brood::VersionEvent& ev, const std::function<juce::String(brood::AutoParam)>& idOf,
                      const std::function<float(brood::AutoParam, double)>& normalised) {
        if (ev.type == brood::VersionEvent::Reset) { clear(); return; }
        if (ev.type == brood::VersionEvent::Released) { dropAudio(ev.id, false); return; }
        if (ev.type == brood::VersionEvent::Evicted) { dropAudio(ev.id, true); return; }
        Entry e;
        e.audioId = ev.id;
        e.label = ev.kind == brood::VersionKind::Take ? "NEW TAKE" : ev.kind == brood::VersionKind::Clear ? "CLEAR"
                : ev.kind == brood::VersionKind::Layer ? "OVERDUB LAYER" : "RESAMPLE";
        const juce::ScopedValueSetter<bool> busy(applying, true);
        for (int i = 0; i < ev.nAuto; ++i) {
            const auto id = idOf(ev.autos[i].which);
            auto* p = apvts.getParameter(id);
            if (p == nullptr) continue;
            const float before = p->getValue(), after = normalised(ev.autos[i].which, ev.autos[i].value);
            if (std::fabs(before - after) > 1e-6f) e.changes.push_back({id, before, after});
            p->beginChangeGesture(); p->setValueNotifyingHost(after); p->endChangeGesture();
        }
        // The layer's own action began when the layer did; the cores already dropped the redo side.
        redoStack.clear();
        push(std::move(e), false);
    }

    void clear() {
        undoStack.clear(); redoStack.clear(); group = Entry{}; groupDepth = 0;
        if (onChange) onChange();
    }

    std::function<void()> onChange;   // the depth changed (the editor redraws)

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::StringArray excluded;
    std::function<void(int)> swap, release;
    std::deque<Entry> undoStack, redoStack;
    Entry group; int groupDepth = 0;
    bool applying = false;
    // Gestures under way: each parameter's value when it began.
    std::map<int, float> started;
    // The last gesture's end, to merge the mouse wheel's string of tiny gestures into one.
    juce::String lastGestureId; juce::uint32 lastGestureEnd = 0;

    juce::String labelOf(const juce::String& id) const {
        if (auto* p = apvts.getParameter(id)) return p->getName(32).toUpperCase();
        return id.toUpperCase();
    }
    juce::RangedAudioParameter* byIndex(int index) const {
        auto params = apvts.processor.getParameters();
        return index >= 0 && index < params.size() ? dynamic_cast<juce::RangedAudioParameter*>(params[index]) : nullptr;
    }

    void parameterValueChanged(int, float) override {}
    void parameterGestureChanged(int index, bool starting) override {
        if (applying) return;
        auto* p = byIndex(index);
        if (p == nullptr || excluded.contains(p->getParameterID())) return;
        // Gestures arrive on the thread that made them; the editor's are on the message thread.
        if (!juce::MessageManager::getInstance()->isThisTheMessageThread()) return;
        if (starting) { started[index] = p->getValue(); return; }
        const auto it = started.find(index);
        if (it == started.end()) return;
        const float before = it->second, after = p->getValue();
        started.erase(it);
        if (std::fabs(before - after) < 1e-6f) return;
        const auto id = p->getParameterID();
        if (groupDepth > 0) { addTo(group, id, before, after); return; }
        const auto now = juce::Time::getMillisecondCounter();
        if (!undoStack.empty() && lastGestureId == id && now - lastGestureEnd < 400) {
            Entry& top = undoStack.back();
            if (top.audioId < 0 && top.changes.size() == 1 && top.changes[0].id == id) {
                top.changes[0].after = after; lastGestureEnd = now;
                return;
            }
        }
        lastGestureId = id; lastGestureEnd = now;
        Entry e; e.changes.push_back({id, before, after}); e.label = labelOf(id);
        push(std::move(e));
    }
    static void addTo(Entry& e, const juce::String& id, float before, float after) {
        for (auto& c : e.changes) if (c.id == id) { c.after = after; return; }
        e.changes.push_back({id, before, after});
    }

    // A new action: the redo side goes (its versions released), and the oldest drops off past four.
    void push(Entry e, bool dropRedo = true) {
        if (dropRedo) {
            for (const auto& r : redoStack) if (r.audioId >= 0) release(r.audioId);
            redoStack.clear();
        }
        undoStack.push_back(std::move(e));
        while (static_cast<int>(undoStack.size()) > kDepth) {
            if (undoStack.front().audioId >= 0) release(undoStack.front().audioId);
            undoStack.pop_front();
        }
        if (onChange) onChange();
    }
    // A version the cores let go of: its action goes (Evicted: and every older one with it).
    void dropAudio(int id, bool andOlder) {
        for (auto it = undoStack.begin(); it != undoStack.end(); ++it)
            if (it->audioId == id) {
                undoStack.erase(andOlder ? undoStack.begin() : it, std::next(it));
                break;
            }
        for (auto it = redoStack.begin(); it != redoStack.end(); ++it)
            if (it->audioId == id) { redoStack.erase(it); break; }
        if (onChange) onChange();
    }

    bool step(bool back) {
        auto& from = back ? undoStack : redoStack;
        auto& to = back ? redoStack : undoStack;
        if (from.empty()) return false;
        Entry e = std::move(from.back()); from.pop_back();
        {
            const juce::ScopedValueSetter<bool> busy(applying, true);
            for (const auto& c : e.changes)
                if (auto* p = apvts.getParameter(c.id)) {
                    p->beginChangeGesture(); p->setValueNotifyingHost(back ? c.before : c.after); p->endChangeGesture();
                }
        }
        if (e.audioId >= 0) swap(e.audioId);
        e.midiCc = -1;
        to.push_back(std::move(e));
        lastGestureId.clear();
        if (onChange) onChange();
        return true;
    }
};
