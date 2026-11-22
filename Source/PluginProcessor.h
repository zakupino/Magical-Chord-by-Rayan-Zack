#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>
#include "Analysis/AudioAnalysisWorker.h"

class MagicalChordsAudioProcessor final : public juce::AudioProcessor
{
public:
    MagicalChordsAudioProcessor();
    ~MagicalChordsAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    void startAudioCapture() noexcept;
    void stopAudioCapture() noexcept;
    void resetAudioAnalysis() noexcept;
    bool isAudioCapturing() const noexcept;

    double getDetectedPitchHz() const noexcept;
    float getPitchConfidence() const noexcept;
    int getAnalyzedWindowCount() const noexcept;
    int getDetectedTonic() const noexcept;
    bool isDetectedMinor() const noexcept;
    void copyLiveWaveform (std::array<float, 256>& dest) const noexcept;

    double getHostBpm() const noexcept;
    bool hasValidHostBpm() const noexcept;

    // Decodes an audio file off disk and feeds it through the same analysis
    // path as live input (used by the editor's IMPORT AUDIO button).
    bool loadAudioFile (const juce::File& file);

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Magical Chords by Ryan Zack"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    int savedGenreIndex = 0;
    int savedKeyIndex = 0;
    int savedScaleIndex = 0;
    bool savedDawSync = true;
    double savedManualBpm = 120.0;
    int savedPianoVar = 0;
    int savedBassVar = 0;
    int savedLeadVar = 0;
    int savedDrumsVar = 0;

private:
    magicalchords::AudioAnalysisWorker analysisWorker;
    std::vector<float> monoScratch;
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<bool> validHostBpm { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MagicalChordsAudioProcessor)
};
