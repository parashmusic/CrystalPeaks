#include "AudioEngine.h"

AudioEngine::AudioEngine() = default;
AudioEngine::~AudioEngine() { shutdown(); }

juce::String AudioEngine::initialise()
{
    // ----------------------------------------------------------------
    // Step 1: Initialise the device manager with the *output* device
    // open. WASAPI only exposes loopback input devices after the output
    // stream is created internally — so we need numOutputChannels > 0
    // on first initialise, then we switch to input-only.
    // ----------------------------------------------------------------
    {
        auto err = deviceManager.initialise (0, 2, nullptr, true);
        if (err.isNotEmpty())
            return "Init(out) failed: " + err;
    }

    // ----------------------------------------------------------------
    // Step 2: Find the WASAPI (Windows Audio) device type and scan.
    // JUCE names it "Windows Audio" or "Windows Audio (Exclusive Mode)".
    // ----------------------------------------------------------------
    juce::AudioIODeviceType* wasapiType = nullptr;
    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        type->scanForDevices(); // populate device list

        if (type->getTypeName().startsWithIgnoreCase ("Windows Audio")
            && !type->getTypeName().containsIgnoreCase ("Exclusive"))
        {
            wasapiType = type;
            break;
        }
    }

    if (wasapiType == nullptr)
        return "WASAPI (Windows Audio) not available on this system.";

    // ----------------------------------------------------------------
    // Step 3: Enumerate ALL input device names and look for loopback.
    // ----------------------------------------------------------------
    const auto inputNames = wasapiType->getDeviceNames (true /*inputs*/);

    // Build diagnostic info (shorter so it fits)
    deviceInfo = "[";
    for (int i = 0; i < inputNames.size(); ++i)
        deviceInfo += inputNames[i] + (i < inputNames.size() - 1 ? ", " : "");
    deviceInfo += "]";

    juce::String loopbackName;
    for (const auto& name : inputNames)
    {
        if (name.containsIgnoreCase ("loopback"))
        {
            loopbackName = name;
            break;
        }
    }

    if (loopbackName.isEmpty())
    {
        for (const auto& name : inputNames)
        {
            if (name.containsIgnoreCase ("stereo mix")
                || name.containsIgnoreCase ("what u hear")
                || name.containsIgnoreCase ("wave out"))
            {
                loopbackName = name;
                break;
            }
        }

        if (loopbackName.isEmpty())
            return "No loopback device found.\n" + deviceInfo;
    }

    deviceInfo = "Using: \"" + loopbackName + "\" | " + deviceInfo;

    // ----------------------------------------------------------------
    // Step 4: Register callback first, then open the device.
    //
    // KEY FIX: Stereo Mix captures the *output* stream, so the output
    // device MUST remain open. Closing it (outputDeviceName={}) makes
    // Stereo Mix go silent and deliver zero callbacks.
    // We MUST use the currently active output device, otherwise it captures silence.
    // ----------------------------------------------------------------
    deviceManager.addAudioCallback (this);

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup (setup);

    const juce::String activeOutput = setup.outputDeviceName; // The actual default selected by initialise()
    deviceInfo += " | output=\"" + activeOutput + "\"";

    setup.inputDeviceName  = loopbackName;  // Stereo Mix input
    setup.outputDeviceName = activeOutput;  // keep output open — required for Stereo Mix
    
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setRange (0, 2, true); // bits 0 and 1
    
    setup.useDefaultOutputChannels = false;
    setup.outputChannels.clear();
    setup.outputChannels.setRange (0, 2, true); // bits 0 and 1
    
    setup.sampleRate       = 0;             // 0 = let driver choose native rate
    setup.bufferSize       = 0;             // 0 = let driver choose native buffer size

    auto err2 = deviceManager.setAudioDeviceSetup (setup, true);
    if (err2.isNotEmpty())
    {
        deviceManager.removeAudioCallback (this);
        return "setAudioDeviceSetup failed: " + err2 + " | " + deviceInfo;
    }

    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
    {
        deviceManager.removeAudioCallback (this);
        return "Device is null after setup. " + deviceInfo;
    }

    deviceInfo = "loop=\"" + loopbackName + "\" out=\"" + activeOutput + "\" SR=" + juce::String (device->getCurrentSampleRate())
                + " ch=" + juce::String (device->getActiveInputChannels().countNumberOfSetBits());

    // NOTE: running is set in audioDeviceAboutToStart(), not here.
    return {};
}

void AudioEngine::shutdown()
{
    running = false;
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

// ---- Ring buffer consumer ----

int AudioEngine::getNumAvailableSamples() const noexcept
{
    return fifo.getNumReady();
}

int AudioEngine::readSamples (float* destL, float* destR, int numSamples)
{
    int actualRead = 0;
    const int available = fifo.getNumReady();
    numSamples = std::min (numSamples, available);

    if (numSamples <= 0)
        return 0;

    int start1, size1, start2, size2;
    fifo.prepareToRead (numSamples, start1, size1, start2, size2);

    // Copy first segment
    if (size1 > 0)
    {
        juce::FloatVectorOperations::copy (destL + actualRead, ring.getReadPointer (0, start1), size1);
        juce::FloatVectorOperations::copy (destR + actualRead, ring.getReadPointer (1, start1), size1);
        actualRead += size1;
    }
    // Copy wrap-around segment
    if (size2 > 0)
    {
        juce::FloatVectorOperations::copy (destL + actualRead, ring.getReadPointer (0, start2), size2);
        juce::FloatVectorOperations::copy (destR + actualRead, ring.getReadPointer (1, start2), size2);
        actualRead += size2;
    }

    fifo.finishedRead (actualRead);
    return actualRead;
}

// ---- AudioIODeviceCallback ----

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    ++dbgAboutToStartFired;
    const double sr    = device->getCurrentSampleRate();
    const double useSR = (sr > 0.0) ? sr : 48000.0;
    const int capacity = static_cast<int> (useSR) * kRingBufferSeconds;

    sampleRate.store (useSR);
    fifo.setTotalSize (capacity);
    ring.setSize (2, capacity, false, true, false);

    // Set running HERE so it's always true when the device is live.
    running = true;
}

void AudioEngine::audioDeviceStopped()
{
    running = false;
}

void AudioEngine::audioDeviceIOCallbackWithContext (
    const float* const* inputChannelData,
    int numInputChannels,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& /*context*/)
{
    ++dbgCallbackFired;

    // Zero the output — we keep output open for Stereo Mix to work
    // but we don't want to play any audio through the speakers.
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData != nullptr && outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);

    if (!running) { dbgFailReason = 1; return; }
    if (numInputChannels < 1) { dbgFailReason = 2; return; }
    if (inputChannelData == nullptr) { dbgFailReason = 3; return; }

    // How much room is left in the ring?
    const int space = fifo.getFreeSpace();
    const int toWrite = std::min (numSamples, space);

    if (toWrite <= 0) { dbgFailReason = 4; return; }

    int start1, size1, start2, size2;
    fifo.prepareToWrite (toWrite, start1, size1, start2, size2);

    const float* srcL = inputChannelData[0];
    const float* srcR = (numInputChannels > 1) ? inputChannelData[1] : inputChannelData[0];

    if (size1 > 0)
    {
        juce::FloatVectorOperations::copy (ring.getWritePointer (0, start1), srcL,         size1);
        juce::FloatVectorOperations::copy (ring.getWritePointer (1, start1), srcR,         size1);
    }
    if (size2 > 0)
    {
        juce::FloatVectorOperations::copy (ring.getWritePointer (0, start2), srcL + size1, size2);
        juce::FloatVectorOperations::copy (ring.getWritePointer (1, start2), srcR + size1, size2);
    }

    fifo.finishedWrite (size1 + size2);
    dbgSamplesReceived += (size1 + size2);
}