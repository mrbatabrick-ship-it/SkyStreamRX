#pragma once

#if JUCE_ANDROID
#define JUCE_CORE_INCLUDE_JNI_HELPERS 1
#endif

#include <JuceHeader.h>
#include <atomic>
#include <cmath>
#include <thread>
#include <chrono>

#if JUCE_ANDROID
#include <jni.h>
#include <sys/socket.h>

namespace juce
{
#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (createMulticastLock, "createMulticastLock", "(Ljava/lang/String;)Landroid/net/wifi/WifiManager$MulticastLock;") \
        METHOD (createWifiLock,      "createWifiLock",      "(ILjava/lang/String;)Landroid/net/wifi/WifiManager$WifiLock;")
    DECLARE_JNI_CLASS (WifiManager, "android/net/wifi/WifiManager")
#undef JNI_CLASS_MEMBERS

#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (acquire, "acquire", "()V") \
        METHOD (release, "release", "()V")
    DECLARE_JNI_CLASS (MulticastLock, "android/net/wifi/WifiManager$MulticastLock")
#undef JNI_CLASS_MEMBERS

#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (acquire, "acquire", "()V") \
        METHOD (release, "release", "()V") \
        METHOD (setReferenceCounted, "setReferenceCounted", "(Z)V")
    DECLARE_JNI_CLASS (WifiLock,      "android/net/wifi/WifiManager$WifiLock")
#undef JNI_CLASS_MEMBERS

#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (addFlags,   "addFlags",   "(I)V") \
        METHOD (clearFlags, "clearFlags", "(I)V") \
        METHOD (setNavigationBarColor, "setNavigationBarColor", "(I)V") \
        METHOD (setStatusBarColor,     "setStatusBarColor",     "(I)V") \
        METHOD (setSustainedPerformanceMode, "setSustainedPerformanceMode", "(Z)V")
    DECLARE_JNI_CLASS (AndroidWindowExt, "android/view/Window")
#undef JNI_CLASS_MEMBERS

#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (getWindow, "getWindow", "()Landroid/view/Window;")
    DECLARE_JNI_CLASS (AndroidActivityExt, "android/app/Activity")
#undef JNI_CLASS_MEMBERS

#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (newWakeLock, "newWakeLock", "(ILjava/lang/String;)Landroid/os/PowerManager$WakeLock;")
    DECLARE_JNI_CLASS (PowerManager, "android/os/PowerManager")
#undef JNI_CLASS_MEMBERS

#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        METHOD (acquire, "acquire", "()V") \
        METHOD (release, "release", "()V") \
        METHOD (setReferenceCounted, "setReferenceCounted", "(Z)V")
    DECLARE_JNI_CLASS (WakeLock, "android/os/PowerManager$WakeLock")
#undef JNI_CLASS_MEMBERS

#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
        STATICMETHOD (setThreadPriority, "setThreadPriority", "(I)V")
    DECLARE_JNI_CLASS (AndroidProcess, "android/os/Process")
#undef JNI_CLASS_MEMBERS
}
#endif

class CRC32 {
public:
    static uint32_t calculate(const void* data, size_t length) {
        static uint32_t table[256];
        static bool tableComputed = false;
        if (!tableComputed) {
            for (uint32_t i = 0; i < 256; i++) {
                uint32_t c = i;
                for (int j = 0; j < 8; j++)
                    c = (c & 1) ? 0xedb88320L ^ (c >> 1) : (c >> 1);
                table[i] = c;
            }
            tableComputed = true;
        }
        uint32_t crc = 0xffffffffL;
        const unsigned char* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < length; i++)
            crc = table[(crc ^ bytes[i]) & 0xff] ^ (crc >> 8);
        return crc ^ 0xffffffffL;
    }
};

#pragma pack(push, 1)
struct MultiChannelHeader {
    uint32_t sequenceID;
    uint16_t segmentIndex;
    uint16_t totalSegments;
    uint64_t audioTimestamp;
    float sampleRate;
    uint16_t numChannels;
    uint16_t numSamples;
    uint16_t channelStartIndex;
    uint16_t channelCount;
    uint16_t hasMetadata;
    uint32_t crc32;
};
struct ChannelMetadata { char names[64][20]; };
#pragma pack(pop)

struct RXTrack {
    juce::AbstractFifo fifo { 131072 };
    juce::AudioBuffer<float> buffer { 1, 131072 };
    std::atomic<float> currentLevelDb { -100.0f };
    char trackName[20];
    float lastGain = 0.0f;
    std::atomic<bool> isInitialized { false };
    std::atomic<uint32_t> lastSequenceReceived { 0 };
    std::atomic<int> adaptivePrebuffer { 720 };
    const int minPrebuffer = 128;
    const int maxPrebuffer = 4800;
    std::atomic<int> healthStatus { 0 };
    std::atomic<bool> lowLatencyMode { false };
    const int normalTarget = 720;
    const int lowLatencyTarget = 256;
    const int fallbackTarget = 480;
    std::atomic<double> fractionalReadPos { 0.0 };
    std::atomic<double> playbackRate { 1.0 };
    std::atomic<uint64_t> totalSamplesPlayed { 0 };
    std::atomic<uint32_t> startTimeMs { 0 };
    std::atomic<uint32_t> lastAdjustMs { 0 };
    std::atomic<int> underflowCounter { 0 };
    std::atomic<uint32_t> lastUnderflowMs { 0 };
    std::atomic<bool> fallbackActive { false };

    RXTrack() {
        buffer.clear(); std::memset(trackName, 0, 20);
        startTimeMs.store(juce::Time::getMillisecondCounter());
        lastAdjustMs.store(startTimeMs.load());
    }

    void reset() {
        fifo.reset();
        isInitialized.store(false);
        lastSequenceReceived.store(0);
        fractionalReadPos.store(0.0);
        playbackRate.store(1.0);
        totalSamplesPlayed.store(0);
        uint32_t now = juce::Time::getMillisecondCounter();
        startTimeMs.store(now);
        lastAdjustMs.store(now);
        fallbackActive.store(false);
        underflowCounter.store(0);
        adaptivePrebuffer.store(lowLatencyMode.load() ? lowLatencyTarget : normalTarget);
    }
};

class MusicianLNF : public juce::LookAndFeel_V4 {
public:
    MusicianLNF() {
        setDefaultSansSerifTypefaceName("Verdana");
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff222222));
        setColour(juce::TextButton::buttonOnColourId, juce::Colours::cyan.withAlpha(0.6f));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           const juce::Slider::SliderStyle, juce::Slider& slider) override {
        auto knobWidth = (float)width * 0.25f;
        auto knobHeight = 32.0f;
        juce::Rectangle<float> knob ((float)slider.getLocalBounds().getX() + ((float)slider.getLocalBounds().getWidth() / 2.0f) - (knobWidth * 0.5f),
                                     sliderPos - (knobHeight * 0.5f), knobWidth, knobHeight);

        // Deep Neumorphic knob shadow
        g.setColour(juce::Colour(0xff050505));
        g.fillRoundedRectangle(knob.translated(4.5f, 4.5f), 4.0f);
        g.setColour(juce::Colour(0xff3a3a3a));
        g.fillRoundedRectangle(knob.translated(-2.0f, -2.0f), 4.0f);

        juce::ColourGradient grad(juce::Colour(0xff3d3d3d), knob.getX(), knob.getY(),
                                  juce::Colour(0xff151515), knob.getRight(), knob.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(knob, 4.0f);

        g.setColour(juce::Colours::cyan.withAlpha(0.8f));
        g.fillRect(knob.getX() + 4.0f, knob.getCentreY() - 1.0f, knob.getWidth() - 8.0f, 2.0f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override {
        auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
        auto cornerSize = 6.0f;
        juce::Colour lightShadow = juce::Colour(0xff3a3a3a);
        juce::Colour darkShadow = juce::Colour(0xff050505);

        if (shouldDrawButtonAsDown || button.getToggleState()) {
            g.setColour(darkShadow);
            g.drawRoundedRectangle(bounds.translated(0.5f, 0.5f), cornerSize, 2.0f);
            g.setColour(lightShadow);
            g.drawRoundedRectangle(bounds.translated(-0.5f, -0.5f), cornerSize, 1.0f);
            g.setColour(button.getToggleState() ? backgroundColour.withAlpha(0.5f) : juce::Colours::black.withAlpha(0.4f));
            g.fillRoundedRectangle(bounds, cornerSize);
        } else {
            // Elevated 3D Shadow
            g.setColour(darkShadow);
            g.fillRoundedRectangle(bounds.translated(5.0f, 5.0f), cornerSize);
            g.setColour(lightShadow);
            g.fillRoundedRectangle(bounds.translated(-2.5f, -2.5f), cornerSize);

            // Subtle Surface Gradient for metallic feel
            juce::ColourGradient surface(juce::Colour(0xff2d2d2d), bounds.getX(), bounds.getY(),
                                         juce::Colour(0xff1a1a1a), bounds.getRight(), bounds.getBottom(), false);
            g.setGradientFill(surface);
            g.fillRoundedRectangle(bounds, cornerSize);

            // Fine bezel highlight line
            g.setColour(juce::Colours::white.withAlpha(0.08f));
            g.drawRoundedRectangle(bounds.reduced(0.5f), cornerSize, 1.0f);

            if (shouldDrawButtonAsHighlighted) {
                g.setColour(juce::Colours::white.withAlpha(0.05f));
                g.fillRoundedRectangle(bounds, cornerSize);
            }
        }
    }
};

class MainComponent : public juce::AudioAppComponent, public juce::Thread, public juce::Timer
{
public:
    MainComponent() : juce::Thread ("RXNetworkThread") {
        setOpaque(true);
        setBufferedToImage(false);
#if JUCE_ANDROID
        acquireAndroidLocks(true);
        updateAndroidUI();
        setScreenKeepOn(true);
#endif

        setLookAndFeel(&lnf);
        for (int i = 0; i < 64; ++i) rxTracks.add(new RXTrack());

        addAndMakeVisible(titleLabel);
        titleLabel.setText("SkyStream (RX)", juce::dontSendNotification);
        titleLabel.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
        titleLabel.setColour(juce::Label::textColourId, juce::Colours::orange);

        addAndMakeVisible(statsLabel);
        statsLabel.setFont(juce::FontOptions(11.0f));
        statsLabel.setColour(juce::Label::textColourId, juce::Colours::whitesmoke.withAlpha(0.4f));

        addAndMakeVisible(latencyButton);
        latencyButton.setButtonText("ORCH");
        latencyButton.setClickingTogglesState(true);
        latencyButton.onClick = [this] {
            int idx = currentTrackIdx.load();
            if (idx >= 0) {
                rxTracks[idx]->lowLatencyMode.store(latencyButton.getToggleState());
                latencyButton.setButtonText(rxTracks[idx]->lowLatencyMode.load() ? "RHYTHM" : "ORCH");
                rxTracks[idx]->reset();
            }
        };

        addAndMakeVisible(channelDisplayLabel);
        channelDisplayLabel.setText("SELECT", juce::dontSendNotification);
        channelDisplayLabel.setJustificationType(juce::Justification::centred);
        channelDisplayLabel.setFont(juce::FontOptions(40.0f).withStyle("Medium"));
        channelDisplayLabel.setColour(juce::Label::textColourId, juce::Colours::cyan);
        channelDisplayLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        channelDisplayLabel.addMouseListener(this, false);

        addAndMakeVisible(mainFader);
        mainFader.setSliderStyle(juce::Slider::LinearVertical);
        mainFader.setRange(-70.0, 12.0, 0.1);
        mainFader.setValue(0.0);
        mainFader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        mainFader.setDoubleClickReturnValue(true, 0.0);
        mainFader.setVelocityBasedMode(false);

        addAndMakeVisible(faderLevelLabel);
        faderLevelLabel.setText("0.0 dB", juce::dontSendNotification);
        faderLevelLabel.setJustificationType(juce::Justification::centred);
        faderLevelLabel.setFont(juce::FontOptions(16.0f).withStyle("Bold"));
        faderLevelLabel.setColour(juce::Label::textColourId, juce::Colours::whitesmoke);
        mainFader.onValueChange = [this] {
            faderLevelLabel.setText(juce::String(mainFader.getValue(), 1) + " dB", juce::dontSendNotification);
        };

        addAndMakeVisible(muteButton);
        muteButton.setButtonText("MUTE"); muteButton.setClickingTogglesState(true);
        muteButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red.withAlpha(0.6f));

        addAndMakeVisible(lockButton);
        lockButton.setButtonText("LOCK"); lockButton.setClickingTogglesState(true);
        lockButton.onClick = [this] { updateAODState(); };

        if (udpSocket.bindToPort(54321, "")) {
            for (int i = 1; i <= 4; ++i) udpSocket.joinMulticast("239.255.0." + juce::String(i));
            int handle = udpSocket.getRawSocketHandle();
            int rcvBufSize = 4 * 1024 * 1024;
#if !JUCE_IOS
            setsockopt(handle, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvBufSize, sizeof(rcvBufSize));
#endif
        }

        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager.getAudioDeviceSetup(setup);
        setup.bufferSize = 128;
        deviceManager.setAudioDeviceSetup(setup, true);

        setAudioChannels(0, 2);
        startThread(juce::Thread::Priority::highest);
        startTimerHz(20); setSize(400, 750);
#if JUCE_ANDROID
        setScreenKeepOn(true);
#endif
    }

    ~MainComponent() override {
#if JUCE_ANDROID
        acquireAndroidLocks(false);
        setScreenKeepOn(false);
#endif
        stopThread(2000); shutdownAudio(); setLookAndFeel(nullptr);
    }

    void buildBackgroundCache() {
        backgroundCache = juce::Image(juce::Image::RGB, getWidth(), getHeight(), true);
        juce::Graphics g(backgroundCache);

        // 1. Base Deep Grey
        g.fillAll(juce::Colour(0xff121212));

        // 2. Grain Texture (Enhanced visibility)
        juce::Random r;
        for (int i = 0; i < 40000; ++i) {
            g.setColour(juce::Colours::white.withAlpha(r.nextFloat() * 0.035f));
            g.fillRect(r.nextInt(getWidth()), r.nextInt(getHeight()), 1, 1);
        }

        // 3. Vignette Effect (Stronger depth)
        juce::ColourGradient vignette (juce::Colours::transparentBlack, (float)getWidth()/2.0f, (float)getHeight()/2.0f,
                                       juce::Colours::black.withAlpha(0.7f), 0.0f, 0.0f, true);
        g.setGradientFill(vignette);
        g.fillAll();

        // 4. Layout lines
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.drawLine(0, 80, (float)getWidth(), 80, 2.0f);
        g.setColour(juce::Colours::white.withAlpha(0.03f));
        g.drawLine(0, 81, (float)getWidth(), 81, 1.0f);

        if (dbScaleImage.isValid()) {
            int centerX = (int)(mainFader.getX() + (mainFader.getWidth() / 2));
            g.setOpacity(0.6f);
            g.drawImageAt(dbScaleImage, centerX - 35, mainFader.getY());
        }
    }

    void updateAndroidUI() {
#if JUCE_ANDROID
        auto* env = juce::getEnv(); auto activity = juce::getMainActivity();
        if (env != nullptr && activity != nullptr) {
            auto windowObj = env->CallObjectMethod (activity.get(), juce::AndroidActivityExt.getWindow);
            if (windowObj != nullptr) {
                const int bgCol = (int) 0xff121212;
                env->CallVoidMethod (windowObj, juce::AndroidWindowExt.setNavigationBarColor, bgCol);
                env->CallVoidMethod (windowObj, juce::AndroidWindowExt.setStatusBarColor, bgCol);
                env->CallVoidMethod (windowObj, juce::AndroidWindowExt.setSustainedPerformanceMode, true);
                env->DeleteLocalRef(windowObj);
            }
        }
#endif
    }

    void run() override {
#if JUCE_ANDROID
        auto* env = juce::getEnv();
        if (env != nullptr) env->CallStaticVoidMethod (juce::AndroidProcess, juce::AndroidProcess.setThreadPriority, -16);
#endif
        setPriority (juce::Thread::Priority::highest);
        juce::MemoryBlock packetData(16384);
        while (!threadShouldExit()) {
            if (udpSocket.waitUntilReady(true, 5) > 0) {
                int processed = 0;
                while (processed < 64) {
                    int bytes = udpSocket.read(packetData.getData(), (int)packetData.getSize(), false);
                    if (bytes <= 0) break;
                    processed++;
                    if (bytes < (int)sizeof(MultiChannelHeader)) continue;
                    auto* h = reinterpret_cast<MultiChannelHeader*>(packetData.getData());
                    uint32_t receivedCRC = h->crc32;
                    h->crc32 = 0;
                    uint32_t calculatedCRC = CRC32::calculate(packetData.getData(), (size_t)bytes);
                    h->crc32 = receivedCRC;
                    if (receivedCRC != calculatedCRC) continue;
                    int offset = sizeof(MultiChannelHeader);
                    if (h->hasMetadata) {
                        auto* meta = reinterpret_cast<const ChannelMetadata*>((const char*)packetData.getData() + offset);
                        for (int i = 0; i < h->numChannels && i < 64; ++i)
                            if (meta->names[i][0] != 0) std::memcpy(rxTracks[i]->trackName, meta->names[i], 20);
                        offset += sizeof(ChannelMetadata);
                    }
                    int selected = currentTrackIdx.load();
                    if (selected >= h->channelStartIndex && selected < (h->channelStartIndex + h->channelCount)) {
                        auto* t = rxTracks[selected];
                        uint32_t lastSeq = t->lastSequenceReceived.load();
                        if (h->sequenceID <= lastSeq && lastSeq != 0) continue;
                        if (t->isInitialized.load() && h->sequenceID > lastSeq + 100) t->reset();
                        t->lastSequenceReceived.store(h->sequenceID);
                        if (t->fifo.getFreeSpace() >= h->numSamples) {
                            int internalIdx = selected - h->channelStartIndex;
                            const float* audioSrc = reinterpret_cast<const float*>((const char*)packetData.getData() + offset) + (internalIdx * h->numSamples);
                            int s1, z1, s2, z2;
                            t->fifo.prepareToWrite(h->numSamples, s1, z1, s2, z2);
                            if (z1 > 0) juce::FloatVectorOperations::copy(t->buffer.getWritePointer(0, s1), audioSrc, z1);
                            if (z2 > 0) juce::FloatVectorOperations::copy(t->buffer.getWritePointer(0, s2), audioSrc + z1, z2);
                            t->fifo.finishedWrite(z1 + z2);
                        }
                    }
                    isReceiving.store(true); lastPacketTime.store((uint32_t)juce::Time::getMillisecondCounter());
                }
            } else { juce::Thread::sleep(1); }
            if (juce::Time::getMillisecondCounter() > lastPacketTime.load() + 1500) isReceiving.store(false);
        }
    }

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override {
        bufferToFill.clearActiveBufferRegion();
        if (muteButton.getToggleState()) {
            int idx = currentTrackIdx.load();
            if (idx >= 0 && idx < 64) rxTracks[idx]->fifo.finishedRead(rxTracks[idx]->fifo.getNumReady());
            return;
        }
        int idx = currentTrackIdx.load();
        if (idx < 0 || idx >= 64) return;
        auto* t = rxTracks[idx];
        int ready = t->fifo.getNumReady();
        int targetPrebuffer = t->adaptivePrebuffer.load();
        uint32_t now = juce::Time::getMillisecondCounter();
        if (ready < targetPrebuffer / 2) {
            if (now - t->lastUnderflowMs.load() > 200) {
                int newTarget = juce::jmin(targetPrebuffer + 64, t->maxPrebuffer);
                t->adaptivePrebuffer.store(newTarget);
                t->lastUnderflowMs.store(now);
                targetPrebuffer = newTarget;
            }
        } else if (ready > targetPrebuffer + 512) {
            int newTarget = juce::jmax(targetPrebuffer - 32, t->minPrebuffer);
            t->adaptivePrebuffer.store(newTarget);
            targetPrebuffer = newTarget;
        }
        if (!t->isInitialized.load()) {
            if (ready >= targetPrebuffer) t->isInitialized.store(true);
            else { t->lastGain = 0.0f; return; }
        }
        if (ready < bufferToFill.numSamples) { t->lastGain = 0.0f; t->healthStatus.store(2); return; }

        if (t->lowLatencyMode.load()) {
            int driftLimit = targetPrebuffer + 128;
            if (ready > driftLimit) { t->fifo.finishedRead(ready - targetPrebuffer); ready = t->fifo.getNumReady(); }
            float targetGain = juce::Decibels::decibelsToGain((float)mainFader.getValue());
            auto* outL = bufferToFill.buffer->getWritePointer(0, bufferToFill.startSample);
            int s1, z1, s2, z2;
            t->fifo.prepareToRead(bufferToFill.numSamples, s1, z1, s2, z2);
            if (z1 > 0) juce::FloatVectorOperations::copy(outL, t->buffer.getReadPointer(0, s1), z1);
            if (z2 > 0) juce::FloatVectorOperations::copy(outL + z1, t->buffer.getReadPointer(0, s2), z2);
            for (int i = 0; i < bufferToFill.numSamples; ++i) {
                float g = t->lastGain + (targetGain - t->lastGain) * ((float)i / (float)bufferToFill.numSamples);
                outL[i] *= g;
            }
            t->fifo.finishedRead(bufferToFill.numSamples); t->lastGain = targetGain;
            bufferToFill.buffer->copyFrom(1, bufferToFill.startSample, *bufferToFill.buffer, 0, bufferToFill.startSample, bufferToFill.numSamples);
        } else {
            int catchupLimit = targetPrebuffer + 256;
            if (ready > catchupLimit) { t->fifo.finishedRead(ready - targetPrebuffer); ready = t->fifo.getNumReady(); }
            float targetGain = juce::Decibels::decibelsToGain((float)mainFader.getValue());
            auto* outL = bufferToFill.buffer->getWritePointer(0, bufferToFill.startSample);
            auto* fifoData = t->buffer.getReadPointer(0);
            int fifoSize = t->buffer.getNumSamples();
            int s1_raw, z1_raw, s2_raw, z2_raw;
            t->fifo.prepareToRead(ready, s1_raw, z1_raw, s2_raw, z2_raw);
            int fifoReadStart = s1_raw;
            double error = (double)ready - (double)targetPrebuffer;
            double correction = (std::abs(error) < 150.0) ? error * 0.0000025 : error * 0.0000080;
            t->playbackRate.store(juce::jlimit(0.9985, 1.0015, 1.0 + correction));
            double currentPlaybackRate = t->playbackRate.load();
            double currentReadPos = t->fractionalReadPos.load();
            for (int i = 0; i < bufferToFill.numSamples; ++i) {
                int idx1 = (int)currentReadPos; int idx2 = idx1 + 1;
                if (idx2 >= ready) break;
                float samp1 = fifoData[(fifoReadStart + idx1) % fifoSize];
                float samp2 = fifoData[(fifoReadStart + idx2) % fifoSize];
                float frac = (float)(currentReadPos - (double)idx1);
                float sample = samp1 + (samp2 - samp1) * frac;
                float g = t->lastGain + (targetGain - t->lastGain) * ((float)i / (float)bufferToFill.numSamples);
                outL[i] = sample * g;
                currentReadPos += currentPlaybackRate;
            }
            int consumed = (int)currentReadPos;
            t->fractionalReadPos.store(currentReadPos - (double)consumed);
            t->fifo.finishedRead(consumed);
            t->lastGain = targetGain;
            bufferToFill.buffer->copyFrom(1, bufferToFill.startSample, *bufferToFill.buffer, 0, bufferToFill.startSample, bufferToFill.numSamples);
        }
        t->currentLevelDb.store(juce::Decibels::gainToDecibels(bufferToFill.buffer->getRMSLevel(0, bufferToFill.startSample, bufferToFill.numSamples)));
        t->healthStatus.store(ready < targetPrebuffer / 2 ? 1 : 0);
    }

    void acquireAndroidLocks (bool acquire) {
#if JUCE_ANDROID
        auto* env = juce::getEnv(); auto context = juce::getAppContext();
        if (env == nullptr || context == nullptr) return;
        jclass contextClass = env->GetObjectClass(context.get());
        jmethodID getSystemServiceMethod = env->GetMethodID(contextClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
        juce::LocalRef<jstring> wifiStr (env->NewStringUTF ("wifi"));
        juce::LocalRef<jobject> wifiMgr (env->CallObjectMethod (context.get(), getSystemServiceMethod, wifiStr.get()));
        juce::LocalRef<jstring> pwrStr (env->NewStringUTF ("power"));
        juce::LocalRef<jobject> pwrMgr (env->CallObjectMethod (context.get(), getSystemServiceMethod, pwrStr.get()));
        if (acquire) {
            if (wifiMgr != nullptr && multicastLock == nullptr) {
                juce::LocalRef<jstring> tag (env->NewStringUTF ("SkyStreamMLock"));
                juce::LocalRef<jobject> lock (env->CallObjectMethod (wifiMgr.get(), juce::WifiManager.createMulticastLock, tag.get()));
                if (lock != nullptr) { multicastLock = env->NewGlobalRef (lock.get()); env->CallVoidMethod (multicastLock, juce::MulticastLock.acquire); }
            }
            if (wifiMgr != nullptr && wifiLock == nullptr) {
                juce::LocalRef<jstring> tag (env->NewStringUTF ("SkyStreamWifiLock"));
                juce::LocalRef<jobject> lock (env->CallObjectMethod (wifiMgr.get(), juce::WifiManager.createWifiLock, 4, tag.get()));
                if (lock != nullptr) {
                    wifiLock = env->NewGlobalRef (lock.get());
                    env->CallVoidMethod (wifiLock, juce::WifiLock.setReferenceCounted, false);
                    env->CallVoidMethod (wifiLock, juce::WifiLock.acquire);
                }
            }
            if (pwrMgr != nullptr && wakeLock == nullptr) {
                juce::LocalRef<jstring> wtag (env->NewStringUTF ("SkyStream:WakeLock"));
                juce::LocalRef<jobject> lock (env->CallObjectMethod (pwrMgr.get(), juce::PowerManager.newWakeLock, 0x0000000a, wtag.get()));
                if (lock != nullptr) {
                    wakeLock = env->NewGlobalRef(lock.get());
                    env->CallVoidMethod (wakeLock, juce::WakeLock.setReferenceCounted, false);
                    env->CallVoidMethod(wakeLock, juce::WakeLock.acquire);
                }
            }
        } else {
            if (multicastLock != nullptr) { env->CallVoidMethod (multicastLock, juce::MulticastLock.release); env->DeleteGlobalRef (multicastLock); multicastLock = nullptr; }
            if (wifiLock != nullptr) { env->CallVoidMethod (wifiLock, juce::WifiLock.release); env->DeleteGlobalRef (wifiLock); wifiLock = nullptr; }
            if (wakeLock != nullptr) { env->CallVoidMethod (wakeLock, juce::WakeLock.release); env->DeleteGlobalRef (wakeLock); wakeLock = nullptr; }
        }
#endif
    }

    void setScreenKeepOn (bool shouldBeOn) {
        juce::MessageManager::callAsync([this, shouldBeOn]() {
            juce::Desktop::getInstance().setScreenSaverEnabled(!shouldBeOn);
#if JUCE_ANDROID
            auto* env = juce::getEnv(); auto activity = juce::getMainActivity();
            if (env != nullptr && activity != nullptr) {
                auto windowObj = env->CallObjectMethod (activity.get(), juce::AndroidActivityExt.getWindow);
                if (windowObj != nullptr) {
                    const int FLAG_KEEP_SCREEN_ON = 128;
                    if (shouldBeOn) env->CallVoidMethod (windowObj, juce::AndroidWindowExt.addFlags, FLAG_KEEP_SCREEN_ON);
                    else            env->CallVoidMethod (windowObj, juce::AndroidWindowExt.clearFlags, FLAG_KEEP_SCREEN_ON);
                    env->DeleteLocalRef(windowObj);
                }
            }
#endif
        });
    }

    void updateAODState() {
        bool isLocked = lockButton.getToggleState();
        mainFader.setEnabled(!isLocked); muteButton.setEnabled(!isLocked); latencyButton.setEnabled(!isLocked);
        channelDisplayLabel.setInterceptsMouseClicks(!isLocked, !isLocked);
        lockButton.setButtonText(isLocked ? "UNLOCK" : "LOCK");
        setScreenKeepOn(true); repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.eventComponent == &channelDisplayLabel && !lockButton.getToggleState()) {
            juce::PopupMenu m;
            for (int i = 0; i < 64; ++i) {
                if (rxTracks[i]->trackName[0] != 0 || rxTracks[i]->fifo.getNumReady() > 0) {
                    juce::String name = (rxTracks[i]->trackName[0] == 0) ? "CH " + juce::String(i+1) : juce::String(rxTracks[i]->trackName);
                    m.addItem(i + 100, name, true, (i == currentTrackIdx.load()));
                }
            }
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&channelDisplayLabel), [this](int r) {
                if (r >= 100) {
                    int newIdx = r - 100;
                    int oldIdx = currentTrackIdx.load();
                    if (newIdx != oldIdx) {
                        if (oldIdx >= 0) rxTracks[oldIdx]->reset();
                        rxTracks[newIdx]->reset();
                        currentTrackIdx.store(newIdx);
                        latencyButton.setToggleState(rxTracks[newIdx]->lowLatencyMode.load(), juce::dontSendNotification);
                        latencyButton.setButtonText(rxTracks[newIdx]->lowLatencyMode.load() ? "RHYTHM" : "ORCH");
                    }
                }
            });
        }
    }

    void timerCallback() override {
        int idx = currentTrackIdx.load();
        if (idx >= 0 && isReceiving.load()) {
            auto* t = rxTracks[idx];
            mainFader.getProperties().set("level", t->currentLevelDb.load());
            juce::String name = (t->trackName[0] == 0) ? "CH " + juce::String(idx+1) : juce::String(t->trackName);
            if (channelDisplayLabel.getText() != name) channelDisplayLabel.setText(name, juce::dontSendNotification);
            static int lastDispReady = 0;
            int currentReady = t->fifo.getNumReady();
            if (std::abs(currentReady - lastDispReady) > 10) {
                statsLabel.setText("Buf: " + juce::String(currentReady) + " | Tgt: " + juce::String(t->adaptivePrebuffer.load()), juce::dontSendNotification);
                lastDispReady = currentReady;
            }
            repaint();
        } else {
            channelDisplayLabel.setText(isReceiving.load() ? "SELECT" : "NO SIGNAL", juce::dontSendNotification);
            statsLabel.setText("", juce::dontSendNotification);
            repaint();
        }
    }

    void paint (juce::Graphics& g) override {
        if (!backgroundCache.isValid()) buildBackgroundCache();
        g.drawImageAt(backgroundCache, 0, 0);

        juce::Colour lightShadow = juce::Colour(0xff303030);
        juce::Colour darkShadow = juce::Colour(0xff0d0d0d);

        // Display Area Inset (Neumorphic)
        auto displayArea = channelDisplayLabel.getBounds().toFloat().expanded(10, 5);
        g.setColour(darkShadow);
        g.fillRoundedRectangle(displayArea.translated(1, 1), 10.0f);
        g.setColour(lightShadow);
        g.fillRoundedRectangle(displayArea.translated(-1, -1), 10.0f);
        g.setColour(juce::Colours::black.withAlpha(0.3f));
        g.fillRoundedRectangle(displayArea, 10.0f);

        // Meter Logic Integrated with Fader Area
        auto fBounds = mainFader.getBounds().toFloat();
        float meterW = 12.0f;
        float meterX = fBounds.getX() + (fBounds.getWidth() / 2.0f) - (meterW / 2.0f);
        float meterY = fBounds.getY() + 10.0f;
        float meterH = fBounds.getHeight() - 20.0f;

        // Meter "Well" Inset
        g.setColour(darkShadow);
        g.fillRoundedRectangle(meterX + 1, meterY + 1, meterW, meterH, 3.0f);
        g.setColour(lightShadow);
        g.fillRoundedRectangle(meterX - 1, meterY - 1, meterW, meterH, 3.0f);
        g.setColour(juce::Colours::black);
        g.fillRoundedRectangle(meterX, meterY, meterW, meterH, 3.0f);

        int idx = currentTrackIdx.load();
        if (idx >= 0 && isReceiving.load()) {
            float level = rxTracks[idx]->currentLevelDb.load();
            static float smoothedLevel = -100.0f;
            if (level > smoothedLevel) smoothedLevel = smoothedLevel * 0.5f + level * 0.5f;
            else                    smoothedLevel = smoothedLevel * 0.8f + level * 0.2f;

            float lvlGain = juce::jlimit(0.0f, 1.0f, juce::jmap(smoothedLevel, -60.0f, 6.0f, 0.0f, 1.0f));
            int fillH = (int)(lvlGain * meterH);
            juce::ColourGradient meterGrad(juce::Colours::green, meterX, meterY + meterH, juce::Colours::red, meterX, meterY, false);
            meterGrad.addColour(0.6, juce::Colours::yellow);
            g.setGradientFill(meterGrad);
            g.fillRect(meterX, (meterY + meterH) - (float)fillH, meterW, (float)fillH);
        }

        // Health Dot
        juce::Colour healthCol = juce::Colours::grey;
        if (isReceiving.load() && idx >= 0) {
            auto* t = rxTracks[idx];
            int status = t->healthStatus.load();
            healthCol = (status == 0) ? juce::Colours::green : (status == 1) ? juce::Colours::yellow : juce::Colours::red;
        }
        g.setColour(healthCol);
        g.fillEllipse(15.0f, 25.0f, 8.0f, 8.0f);

        if (lockButton.getToggleState()) {
            g.fillAll(juce::Colours::black);
            g.setColour(juce::Colours::white.withAlpha(0.8f)); g.setFont(juce::FontOptions(90.0f).withStyle("Light"));
            g.drawText(juce::Time::getCurrentTime().toString(false, true, false, true), 0, (getHeight()/2) - 120, getWidth(), 120, juce::Justification::centred);
        }
    }

    void prepareToPlay (int, double) override {}
    void releaseResources() override {}
    void resized() override {
        int topM = 25;
        titleLabel.setBounds(28, topM + 5, 150, 25);
        statsLabel.setBounds(30, topM + 25, 150, 15);
        latencyButton.setBounds((getWidth() - 90) / 2, topM + 12, 90, 32);
        lockButton.setBounds(getWidth() - 85, topM + 12, 70, 32);
        channelDisplayLabel.setBounds(0, topM + 70, getWidth(), 60);
        auto fBounds = juce::Rectangle<int>(40, topM + 140, getWidth() - 80, getHeight() - 310);
        mainFader.setBounds(fBounds);
        faderLevelLabel.setBounds(0, mainFader.getBottom() + 2, getWidth(), 25);
        muteButton.setBounds(50, getHeight() - 85, getWidth() - 100, 55);

        int stickerW = 70;
        dbScaleImage = juce::Image(juce::Image::ARGB, stickerW, mainFader.getHeight(), true);
        juce::Graphics imgG(dbScaleImage);
        imgG.setColour(juce::Colours::whitesmoke.withAlpha(0.7f));
        for (float dbVal : { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -60.0f }) {
            float normalizedVal = (dbVal - (-70.0f)) / (12.0f - (-70.0f));
            float y = (1.0f - normalizedVal) * (float)mainFader.getHeight();
            imgG.drawLine(0, y, (float)stickerW, y, 0.5f);
            imgG.setFont(10.0f);
            imgG.drawText(juce::String(dbVal, 0), 2, (int)y - 12, 30, 10, juce::Justification::bottomLeft);
        }
        buildBackgroundCache();
    }

private:
    MusicianLNF lnf; juce::Label titleLabel, channelDisplayLabel, statsLabel, faderLevelLabel;
    juce::Slider mainFader; juce::TextButton muteButton, lockButton, latencyButton;
    juce::DatagramSocket udpSocket { false }; juce::OwnedArray<RXTrack> rxTracks;
    std::atomic<int> currentTrackIdx { -1 };
    std::atomic<bool> isReceiving { false };
    std::atomic<uint32_t> lastPacketTime { 0 };
    juce::Image dbScaleImage;
    juce::Image backgroundCache;
#if JUCE_ANDROID
    jobject multicastLock = nullptr; jobject wakeLock = nullptr; jobject wifiLock = nullptr;
#endif
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
