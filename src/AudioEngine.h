#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <functional>

// How many seconds of audio we buffer.
// At 48kHz stereo float32 = 48000 * 2 * 4 * 2 = ~768 KB — fine.
static constexpr int kRingBufferSeconds = 2;

class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    // Call once at startup. Returns empty string on success, error message on failure.
    juce::String initialise();

    void shutdown();

    // Returns a human-readable string of what device was opened + all input names found.
    juce::String getDeviceInfo() const { return deviceInfo; }

    // Debug counters — readable from any thread
    int getDbgCallbackCount()    const noexcept { return dbgCallbackFired.load(); }
    int getDbgAboutToStart()     const noexcept { return dbgAboutToStartFired.load(); }
    int64_t getDbgSamplesIn()    const noexcept { return dbgSamplesReceived.load(); }
    int getDbgFailReason()       const noexcept { return dbgFailReason.load(); }

    // ---- Ring buffer consumer API (call from any thread) ----

    // How many stereo sample-frames are available to read right now.
    int getNumAvailableSamples() const noexcept;

    // Copy up to numSamples frames out of the ring buffer into dest[0] (L) and dest[1] (R).
    // Returns the actual number of frames copied.
    int readSamples (float* destL, float* destR, int numSamples);

    // Current sample rate (set after device opens)
    double getSampleRate() const noexcept { return sampleRate.load(); }

    // ---- AudioIODeviceCallback ----
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext (
        const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;

private:
    juce::AudioDeviceManager deviceManager;

    // Lock-free FIFO index manager. Actual audio data lives in ringL / ringR.
    juce::AbstractFifo fifo { 1 }; // resized in audioDeviceAboutToStart
    juce::AudioBuffer<float> ring;  // ring.getNumChannels()==2, size = fifo capacity

    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<bool>   running    { false };

    juce::String deviceInfo;

    // Debug
    std::atomic<int>     dbgCallbackFired   { 0 };
    std::atomic<int>     dbgAboutToStartFired { 0 };
    std::atomic<int64_t> dbgSamplesReceived { 0 };
    std::atomic<int>     dbgFailReason { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};