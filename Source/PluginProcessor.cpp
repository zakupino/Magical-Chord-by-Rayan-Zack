#include "PluginProcessor.h"
#include "PluginEditor.h"

MagicalChordsAudioProcessor::MagicalChordsAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void MagicalChordsAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    analysisWorker.prepare (sampleRate);
    monoScratch.resize (static_cast<size_t> (juce::jmax (1, samplesPerBlock)), 0.0f);
}

void MagicalChordsAudioProcessor::releaseResources()
{
    stopAudioCapture();
}

bool MagicalChordsAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();
    return mainIn == mainOut
        && (mainOut == juce::AudioChannelSet::mono()
            || mainOut == juce::AudioChannelSet::stereo());
}

void MagicalChordsAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                  juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midi);

    if (auto* currentPlayHead = getPlayHead())
    {
        if (const auto pos = currentPlayHead->getPosition())
        {
            if (const auto bpm = pos->getBpm())
            {
                if (*bpm >= 20.0 && *bpm <= 300.0)
                {
                    hostBpm.store (*bpm, std::memory_order_relaxed);
                    validHostBpm.store (true, std::memory_order_relaxed);
                }
            }
        }
    }

    if (! analysisWorker.isCapturing() || buffer.getNumSamples() <= 0)
        return;

    const int samples = buffer.getNumSamples();
    if (monoScratch.size() < static_cast<size_t> (samples))
        return;

    const int channels = buffer.getNumChannels();
    auto* left = buffer.getReadPointer (0);
    auto* right = channels > 1 ? buffer.getReadPointer (1) : nullptr;
    for (int i = 0; i < samples; ++i)
        monoScratch[static_cast<size_t> (i)] = right != nullptr
            ? 0.5f * (left[i] + right[i]) : left[i];

    analysisWorker.pushAudio (monoScratch.data(), samples);
}

void MagicalChordsAudioProcessor::startAudioCapture() noexcept { analysisWorker.setCapturing (true); }
void MagicalChordsAudioProcessor::stopAudioCapture() noexcept  { analysisWorker.setCapturing (false); }

void MagicalChordsAudioProcessor::resetAudioAnalysis() noexcept
{
    // Fully clear analysis state (buffers, FIFO, histogram, pitch, key estimate)
    // so a new capture / import starts from a clean slate.
    analysisWorker.reset();
}

bool MagicalChordsAudioProcessor::isAudioCapturing() const noexcept { return analysisWorker.isCapturing(); }
double MagicalChordsAudioProcessor::getDetectedPitchHz() const noexcept { return analysisWorker.latestFrequencyHz(); }
float MagicalChordsAudioProcessor::getPitchConfidence() const noexcept { return analysisWorker.latestConfidence(); }
int MagicalChordsAudioProcessor::getAnalyzedWindowCount() const noexcept { return analysisWorker.analyzedWindowCount(); }

int MagicalChordsAudioProcessor::getDetectedTonic() const noexcept
{
    // Estimated key tonic (pitch class 0..11) via Krumhansl-Schmuckler.
    return analysisWorker.detectedTonic();
}

bool MagicalChordsAudioProcessor::isDetectedMinor() const noexcept
{
    return analysisWorker.detectedMinor();
}

void MagicalChordsAudioProcessor::copyLiveWaveform (std::array<float, 256>& dest) const noexcept
{
    analysisWorker.copyLiveWaveform (dest);
}

double MagicalChordsAudioProcessor::getHostBpm() const noexcept { return hostBpm.load (std::memory_order_relaxed); }
bool MagicalChordsAudioProcessor::hasValidHostBpm() const noexcept { return validHostBpm.load (std::memory_order_relaxed); }

bool MagicalChordsAudioProcessor::loadAudioFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return false;

    const int numChans   = static_cast<int> (reader->numChannels);
    const int numSamples = static_cast<int> (reader->lengthInSamples);
    if (numChans <= 0 || numSamples <= 0)
        return false;

    juce::AudioBuffer<float> fileBuffer (numChans, numSamples);
    reader->read (&fileBuffer, 0, numSamples, 0, true, numChans > 1);

    // Reset previous analysis state and arm the worker so pushAudio accepts
    // the offline data through the same path the live input uses.
    analysisWorker.reset();
    analysisWorker.setCapturing (true);

    // Down-mix to mono.
    std::vector<float> mono (static_cast<size_t> (numSamples), 0.0f);
    for (int i = 0; i < numSamples; ++i)
    {
        float s = fileBuffer.getSample (0, i);
        if (numChans > 1)
            s = 0.5f * (s + fileBuffer.getSample (1, i));
        mono[static_cast<size_t> (i)] = s;
    }

    // Feed in chunks matching the prepared block size (falls back to 256 if
    // prepareToPlay has not run yet).
    const int chunk = juce::jmax (1, static_cast<int> (monoScratch.size()));
    for (int off = 0; off < numSamples; off += chunk)
    {
        const int n = std::min (chunk, numSamples - off);
        analysisWorker.pushAudio (mono.data() + off, n);
    }

    return true;
}

juce::AudioProcessorEditor* MagicalChordsAudioProcessor::createEditor()
{
    return new MagicalChordsAudioProcessorEditor (*this);
}

void MagicalChordsAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("MagicalChordsState");
    state.setProperty ("genre", savedGenreIndex, nullptr);
    state.setProperty ("key", savedKeyIndex, nullptr);
    state.setProperty ("scale", savedScaleIndex, nullptr);
    state.setProperty ("dawSync", savedDawSync, nullptr);
    state.setProperty ("manualBpm", savedManualBpm, nullptr);
    state.setProperty ("pianoVar", savedPianoVar, nullptr);
    state.setProperty ("bassVar", savedBassVar, nullptr);
    state.setProperty ("leadVar", savedLeadVar, nullptr);
    state.setProperty ("drumsVar", savedDrumsVar, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MagicalChordsAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (state.isValid())
        {
            savedGenreIndex = state.getProperty ("genre", 0);
            savedKeyIndex = state.getProperty ("key", 0);
            savedScaleIndex = state.getProperty ("scale", 0);
            savedDawSync = state.getProperty ("dawSync", true);
            savedManualBpm = state.getProperty ("manualBpm", 120.0);
            savedPianoVar = state.getProperty ("pianoVar", 0);
            savedBassVar = state.getProperty ("bassVar", 0);
            savedLeadVar = state.getProperty ("leadVar", 0);
            savedDrumsVar = state.getProperty ("drumsVar", 0);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MagicalChordsAudioProcessor();
}
