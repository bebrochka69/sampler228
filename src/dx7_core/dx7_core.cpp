#include "dx7_core.h"

#include "aligned_buf.h"
#include "dx7note.h"
#include "env.h"
#include "exp2.h"
#include "fm_core.h"
#include "freqlut.h"
#include "lfo.h"
#include "pitchenv.h"
#include "porta.h"
#include "sin.h"
#include "synth.h"
#include "tuning.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr int kDefaultChannel = 1;

const uint8_t kInitVoice[155] = {
    99, 99, 99, 99, 99, 99, 99, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 7,
    99, 99, 99, 99, 99, 99, 99, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 7,
    99, 99, 99, 99, 99, 99, 99, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 7,
    99, 99, 99, 99, 99, 99, 99, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 7,
    99, 99, 99, 99, 99, 99, 99, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 7,
    99, 99, 99, 99, 99, 99, 99, 0, 0, 0, 0, 0, 0, 0, 0, 0, 99, 0, 1, 0, 7,
    99, 99, 99, 99, 50, 50, 50, 50, 0, 0, 1, 35, 0, 0, 0, 1, 0, 3, 24,
    73, 78, 73, 84, 32, 86, 79, 73, 67, 69
};

const uint8_t kVoiceMaxes[156] = {
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, // osc6
    3, 3, 7, 3, 7, 99, 1, 31, 99, 14,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, // osc5
    3, 3, 7, 3, 7, 99, 1, 31, 99, 14,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, // osc4
    3, 3, 7, 3, 7, 99, 1, 31, 99, 14,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, // osc3
    3, 3, 7, 3, 7, 99, 1, 31, 99, 14,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, // osc2
    3, 3, 7, 3, 7, 99, 1, 31, 99, 14,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, // osc1
    3, 3, 7, 3, 7, 99, 1, 31, 99, 14,
    99, 99, 99, 99, 99, 99, 99, 99, // pitch eg rate & level
    31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48, // algorithm etc
    126, 126, 126, 126, 126, 126, 126, 126, 126, 126, // name
    127 // operator on/off
};

float clampAudio(float v) {
    if (v > 1.0f) return 1.0f;
    if (v < -1.0f) return -1.0f;
    return v;
}

uint8_t sysexChecksum(const uint8_t *sysex, int size) {
    int sum = 0;
    for (int i = 0; i < size; ++i) {
        sum -= sysex[i];
    }
    return static_cast<uint8_t>(sum & 0x7F);
}

void clampVoice(uint8_t *voice, size_t size) {
    size_t count = std::min<size_t>(size, sizeof(kVoiceMaxes));
    for (size_t i = 0; i < count; ++i) {
        if (voice[i] > kVoiceMaxes[i]) {
            voice[i] = kVoiceMaxes[i];
        }
    }
}

void decodePackedVoice(const uint8_t *packed, std::array<uint8_t, 156> &out) {
    std::fill(out.begin(), out.end(), 0);
    for (unsigned op = 0; op < 6; ++op) {
        std::memcpy(&out[op * 21], &packed[op * 17], 11);

        uint8_t leftrightcurves = packed[op * 17 + 11];
        out[op * 21 + 11] = leftrightcurves & 3;
        out[op * 21 + 12] = (leftrightcurves >> 2) & 3;

        uint8_t detune_rs = packed[op * 17 + 12];
        out[op * 21 + 13] = detune_rs & 7;
        out[op * 21 + 20] = detune_rs >> 3;

        uint8_t kvs_ams = packed[op * 17 + 13];
        out[op * 21 + 14] = kvs_ams & 3;
        out[op * 21 + 15] = kvs_ams >> 2;
        out[op * 21 + 16] = packed[op * 17 + 14];

        uint8_t fcoarse_mode = packed[op * 17 + 15];
        out[op * 21 + 17] = fcoarse_mode & 1;
        out[op * 21 + 18] = fcoarse_mode >> 1;
        out[op * 21 + 19] = packed[op * 17 + 16];
    }

    std::memcpy(&out[126], &packed[102], 9);
    uint8_t oks_fb = packed[111];
    out[135] = oks_fb & 7;
    out[136] = oks_fb >> 3;
    std::memcpy(&out[137], &packed[112], 4);
    uint8_t lpms_lfw_lks = packed[116];
    out[141] = lpms_lfw_lks & 1;
    out[142] = (lpms_lfw_lks >> 1) & 7;
    out[143] = lpms_lfw_lks >> 4;
    std::memcpy(&out[144], &packed[117], 11);
    out[155] = 0x3f;

    clampVoice(out.data(), out.size());
}

std::string normalizeName(const uint8_t *voice) {
    char buffer[11];
    for (int i = 0; i < 10; ++i) {
        char c = static_cast<char>(voice[145 + i] & 0x7F);
        switch (c) {
            case 92:
                c = 'Y';
                break;
            case 126:
                c = '>';
                break;
            case 127:
                c = '<';
                break;
            default:
                if (c < 32 || c > 127) {
                    c = ' ';
                }
                break;
        }
        buffer[i] = c;
    }
    buffer[10] = 0;
    std::string name(buffer);
    while (!name.empty() && name.back() == ' ') {
        name.pop_back();
    }
    return name;
}

std::string makeProgramName(int index, const uint8_t *voice) {
    std::string name = normalizeName(voice);
    if (!name.empty()) {
        return name;
    }
    char fallback[16];
    std::snprintf(fallback, sizeof(fallback), "PROGRAM %02d", index + 1);
    return std::string(fallback);
}

bool voiceDiffersFromInit(const uint8_t *voice) {
    return !std::equal(std::begin(kInitVoice), std::end(kInitVoice), voice);
}

std::array<uint8_t, 156> makeVoiceFromInit() {
    std::array<uint8_t, 156> voice{};
    std::copy(std::begin(kInitVoice), std::end(kInitVoice), voice.begin());
    voice[155] = 0x3f;
    return voice;
}

void setVoiceName(std::array<uint8_t, 156> &voice, const char *name) {
    constexpr int kNameOffset = 145;
    constexpr int kNameLen = 10;
    for (int i = 0; i < kNameLen; ++i) {
        voice[kNameOffset + i] = ' ';
    }
    if (!name) {
        return;
    }
    for (int i = 0; i < kNameLen && name[i]; ++i) {
        voice[kNameOffset + i] = static_cast<uint8_t>(name[i]);
    }
}

void setOpEnv(std::array<uint8_t, 156> &voice, int op, int r1, int r2, int r3, int r4,
              int l1, int l2, int l3, int l4) {
    const int off = op * 21;
    voice[off + 0] = static_cast<uint8_t>(r1);
    voice[off + 1] = static_cast<uint8_t>(r2);
    voice[off + 2] = static_cast<uint8_t>(r3);
    voice[off + 3] = static_cast<uint8_t>(r4);
    voice[off + 4] = static_cast<uint8_t>(l1);
    voice[off + 5] = static_cast<uint8_t>(l2);
    voice[off + 6] = static_cast<uint8_t>(l3);
    voice[off + 7] = static_cast<uint8_t>(l4);
}

void setOpOutput(std::array<uint8_t, 156> &voice, int op, int level) {
    const int off = op * 21;
    voice[off + 16] = static_cast<uint8_t>(level);
}

void setOpFreq(std::array<uint8_t, 156> &voice, int op, int coarse, int fine, int detune) {
    const int off = op * 21;
    voice[off + 17] = 0;  // ratio mode
    voice[off + 18] = static_cast<uint8_t>(coarse);
    voice[off + 19] = static_cast<uint8_t>(fine);
    voice[off + 20] = static_cast<uint8_t>(detune);
}

std::array<uint8_t, 156> makePianoVoice() {
    auto voice = makeVoiceFromInit();
    setVoiceName(voice, "PIANO 1");
    voice[134] = 31;  // algorithm
    voice[135] = 0;   // feedback
    const int outputs[6] = {99, 80, 60, 50, 40, 30};
    for (int op = 0; op < 6; ++op) {
        setOpEnv(voice, op, 99, 50, 45, 99, 99, 70, 35, 0);
        setOpOutput(voice, op, outputs[op]);
        setOpFreq(voice, op, op + 1, 0, 7);
    }
    clampVoice(voice.data(), voice.size());
    return voice;
}

std::array<uint8_t, 156> makeEPianoVoice() {
    auto voice = makeVoiceFromInit();
    setVoiceName(voice, "E.PIANO");
    voice[134] = 5;
    voice[135] = 4;
    const int outputs[6] = {90, 70, 0, 0, 60, 0};
    for (int op = 0; op < 6; ++op) {
        setOpEnv(voice, op, 99, 60, 45, 99, 99, 65, 25, 0);
        setOpOutput(voice, op, outputs[op]);
        setOpFreq(voice, op, (op % 2) ? 2 : 1, 0, 7);
    }
    clampVoice(voice.data(), voice.size());
    return voice;
}

void buildInternalPrograms(std::vector<std::array<uint8_t, 156>> &programs,
                           std::vector<std::string> &names) {
    programs.clear();
    names.clear();
    auto init = makeVoiceFromInit();
    setVoiceName(init, "INIT");
    programs.push_back(init);
    names.emplace_back("INIT");

    auto piano = makePianoVoice();
    programs.push_back(piano);
    names.emplace_back("PIANO 1");

    auto epiano = makeEPianoVoice();
    programs.push_back(epiano);
    names.emplace_back("E.PIANO");
}

bool parseSysexMessage(const uint8_t *msg, size_t len, std::vector<std::array<uint8_t, 156>> &programs,
                       std::vector<std::string> &names) {
    if (len < 7 || msg[0] != 0xF0 || msg[len - 1] != 0xF7) {
        return false;
    }
    if (msg[1] != 0x43) {
        return false;
    }

    int substatus = msg[2] >> 4;
    if (substatus != 0) {
        return false;
    }

    if (msg[3] == 9 && len >= 4104) {
        const uint8_t *payload = msg + 6;
        const uint8_t checksum = msg[6 + 4096];
        if (sysexChecksum(payload, 4096) != checksum) {
            // Still accept but flagging is left to caller.
        }
        programs.clear();
        names.clear();
        programs.reserve(32);
        names.reserve(32);
        for (int i = 0; i < 32; ++i) {
            std::array<uint8_t, 156> decoded{};
            decodePackedVoice(payload + (i * 128), decoded);
            programs.push_back(decoded);
            names.push_back(makeProgramName(i, decoded.data()));
        }
        return true;
    }

    if (msg[3] == 0 && len >= 163) {
        const uint8_t *payload = msg + 6;
        programs.clear();
        names.clear();
        std::array<uint8_t, 156> voice{};
        std::copy(payload, payload + 155, voice.begin());
        clampVoice(voice.data(), voice.size());
        programs.push_back(voice);
        names.push_back(makeProgramName(0, voice.data()));
        return true;
    }

    return false;
}

bool parseSysexBuffer(const std::vector<uint8_t> &data, std::vector<std::array<uint8_t, 156>> &programs,
                      std::vector<std::string> &names) {
    if (data.empty()) {
        return false;
    }

    if (data.size() == 4096) {
        programs.clear();
        names.clear();
        programs.reserve(32);
        names.reserve(32);
        for (int i = 0; i < 32; ++i) {
            std::array<uint8_t, 156> decoded{};
            decodePackedVoice(data.data() + (i * 128), decoded);
            programs.push_back(decoded);
            names.push_back(makeProgramName(i, decoded.data()));
        }
        return true;
    }

    if (data.size() >= 155 && data.size() <= 160 && data[0] != 0xF0) {
        programs.clear();
        names.clear();
        std::array<uint8_t, 156> voice{};
        std::copy(data.data(), data.data() + 155, voice.begin());
        clampVoice(voice.data(), voice.size());
        programs.push_back(voice);
        names.push_back(makeProgramName(0, voice.data()));
        return true;
    }

    size_t pos = 0;
    while (pos < data.size()) {
        if (data[pos] != 0xF0) {
            ++pos;
            continue;
        }
        size_t end = pos + 1;
        while (end < data.size() && data[end] != 0xF7) {
            ++end;
        }
        if (end >= data.size()) {
            break;
        }
        size_t len = end - pos + 1;
        if (parseSysexMessage(data.data() + pos, len, programs, names)) {
            return true;
        }
        pos = end + 1;
    }

    return false;
}
} // namespace

void dexed_trace(const char * /*source*/, const char * /*fmt*/, ...) {
    // Intentionally no-op for the core build.
}

struct Dx7Core::Voice {
    int midi_note = -1;
    int velocity = 0;
    bool keydown = false;
    bool active = false;
    std::unique_ptr<Dx7Note> note;
};

struct Dx7Core::Impl {
    bool initialized = false;
    int sampleRate = 0;
    int maxVoices = 0;
    int voiceCursor = 0;
    int extraSize = 0;

    std::array<float, N> extraBuf{};
    std::array<uint8_t, 156> patch{};
    std::vector<Voice> voices;
    std::vector<std::array<uint8_t, 156>> programs;
    std::vector<std::string> programNames;
    bool lastLoadChanged = false;

    FmCore core;
    Controllers controllers;
    Lfo lfo;
    std::shared_ptr<TuningState> tuning;
    MTSClient *mts = nullptr;
    AlignedBuf<int32_t, N> audioBuf;
};

Dx7Core::Dx7Core() : impl_(std::make_unique<Impl>()) {}

Dx7Core::~Dx7Core() = default;

void Dx7Core::resetState() {
    if (!impl_) {
        return;
    }
    impl_->extraSize = 0;
    impl_->voiceCursor = 0;
    for (auto &voice : impl_->voices) {
        voice.midi_note = -1;
        voice.velocity = 0;
        voice.keydown = false;
        voice.active = false;
    }
}

void Dx7Core::init(int sampleRate, int voices) {
    if (!impl_) {
        return;
    }
    if (sampleRate <= 0) {
        sampleRate = 48000;
    }
    if (voices <= 0) {
        voices = 8;
    }

    impl_->sampleRate = sampleRate;
    impl_->maxVoices = voices;

    Exp2::init();
    Tanh::init();
    Sin::init();
    Freqlut::init(sampleRate);
    Lfo::init(sampleRate);
    PitchEnv::init(sampleRate);
    Env::init_sr(sampleRate);
    Porta::init_sr(sampleRate);

    impl_->tuning = createStandardTuning();

    impl_->voices.clear();
    impl_->voices.resize(static_cast<size_t>(voices));
    for (auto &voice : impl_->voices) {
        voice.note = std::make_unique<Dx7Note>(impl_->tuning, impl_->mts);
    }

    std::fill(impl_->patch.begin(), impl_->patch.end(), 0);
    std::copy(std::begin(kInitVoice), std::end(kInitVoice), impl_->patch.begin());

    std::fill(std::begin(impl_->controllers.values_), std::end(impl_->controllers.values_), 0);
    impl_->controllers.values_[kControllerPitch] = 0x2000;
    impl_->controllers.values_[kControllerPitchRangeUp] = 3;
    impl_->controllers.values_[kControllerPitchRangeDn] = 3;
    impl_->controllers.values_[kControllerPitchStep] = 0;
    impl_->controllers.masterTune = 0;
    impl_->controllers.modwheel_cc = 0;
    impl_->controllers.foot_cc = 0;
    impl_->controllers.breath_cc = 0;
    impl_->controllers.aftertouch_cc = 0;
    impl_->controllers.portamento_enable_cc = false;
    impl_->controllers.portamento_cc = 0;
    impl_->controllers.portamento_gliss_cc = false;
    impl_->controllers.core = &impl_->core;
    impl_->controllers.refresh();

    impl_->lfo.reset(impl_->patch.data() + 137);

    impl_->programs.clear();
    impl_->programNames.clear();
    buildInternalPrograms(impl_->programs, impl_->programNames);
    impl_->lastLoadChanged = false;

    resetState();
    if (!impl_->programs.empty()) {
        loadVoiceParameters(impl_->programs[0].data(), 155);
    }
    impl_->initialized = true;
}

bool Dx7Core::loadVoiceParameters(const uint8_t *data, int size) {
    if (!impl_ || !data || size < 155) {
        return false;
    }

    std::array<uint8_t, 156> voice{};
    std::copy(data, data + 155, voice.begin());
    clampVoice(voice.data(), voice.size());

    impl_->patch = voice;

    if (impl_->initialized) {
        impl_->lfo.reset(impl_->patch.data() + 137);
        for (auto &v : impl_->voices) {
            if (v.active && v.note) {
                v.note->update(impl_->patch.data(), v.midi_note, v.velocity, kDefaultChannel);
            }
        }
    }

    impl_->lastLoadChanged = voiceDiffersFromInit(impl_->patch.data());
    return impl_->lastLoadChanged;
}

bool Dx7Core::loadSysexFile(const std::string &path) {
    if (!impl_) {
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::vector<std::array<uint8_t, 156>> programs;
    std::vector<std::string> names;
    if (!parseSysexBuffer(buffer, programs, names)) {
        return false;
    }
    impl_->programs = std::move(programs);
    impl_->programNames = std::move(names);
    return selectProgram(0);
}

bool Dx7Core::selectProgram(int index) {
    if (!impl_) {
        return false;
    }
    if (index < 0 || index >= static_cast<int>(impl_->programs.size())) {
        return false;
    }
    return loadVoiceParameters(impl_->programs[index].data(), 155);
}

int Dx7Core::programCount() const {
    if (!impl_) {
        return 0;
    }
    return static_cast<int>(impl_->programs.size());
}

const char *Dx7Core::programName(int index) const {
    static const char kEmpty[] = "";
    if (!impl_) {
        return kEmpty;
    }
    if (index < 0 || index >= static_cast<int>(impl_->programNames.size())) {
        return kEmpty;
    }
    return impl_->programNames[index].c_str();
}

bool Dx7Core::lastLoadChanged() const {
    if (!impl_) {
        return false;
    }
    return impl_->lastLoadChanged;
}

int Dx7Core::voiceParam(int index) const {
    if (!impl_) {
        return 0;
    }
    if (index < 0 || index >= static_cast<int>(impl_->patch.size())) {
        return 0;
    }
    return static_cast<int>(impl_->patch[static_cast<size_t>(index)]);
}

bool Dx7Core::setVoiceParam(int index, int value) {
    if (!impl_) {
        return false;
    }
    if (index < 0 || index >= static_cast<int>(impl_->patch.size())) {
        return false;
    }
    uint8_t v = static_cast<uint8_t>(std::max(0, value));
    if (index < static_cast<int>(sizeof(kVoiceMaxes))) {
        v = std::min<uint8_t>(v, kVoiceMaxes[index]);
    }
    impl_->patch[static_cast<size_t>(index)] = v;

    if (impl_->initialized) {
        impl_->lfo.reset(impl_->patch.data() + 137);
        for (auto &vce : impl_->voices) {
            if (vce.active && vce.note) {
                vce.note->update(impl_->patch.data(), vce.midi_note, vce.velocity,
                                 kDefaultChannel);
            }
        }
    }
    impl_->lastLoadChanged = voiceDiffersFromInit(impl_->patch.data());
    return true;
}

int Dx7Core::findVoiceForNote(int note) const {
    if (!impl_) {
        return -1;
    }
    for (size_t i = 0; i < impl_->voices.size(); ++i) {
        const auto &voice = impl_->voices[i];
        if (voice.active && voice.midi_note == note) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int Dx7Core::findFreeVoice() {
    if (!impl_) {
        return -1;
    }
    for (size_t i = 0; i < impl_->voices.size(); ++i) {
        auto &voice = impl_->voices[i];
        if (!voice.active) {
            return static_cast<int>(i);
        }
        if (voice.note && !voice.note->isPlaying()) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void Dx7Core::cleanupVoices() {
    if (!impl_) {
        return;
    }
    for (auto &voice : impl_->voices) {
        if (voice.active && !voice.keydown && voice.note && !voice.note->isPlaying()) {
            voice.active = false;
            voice.midi_note = -1;
            voice.velocity = 0;
        }
    }
}

void Dx7Core::noteOn(int note, int velocity) {
    if (!impl_ || !impl_->initialized) {
        return;
    }
    if (velocity <= 0) {
        noteOff(note);
        return;
    }

    int index = findFreeVoice();
    if (index < 0 && !impl_->voices.empty()) {
        index = impl_->voiceCursor++ % static_cast<int>(impl_->voices.size());
    }
    if (index < 0 || index >= static_cast<int>(impl_->voices.size())) {
        return;
    }

    auto &voice = impl_->voices[index];
    bool voiceSteal = voice.note && voice.note->isPlaying();
    voice.midi_note = note;
    voice.velocity = velocity;
    voice.keydown = true;
    voice.active = true;

    if (voice.note) {
        voice.note->init(impl_->patch.data(), note, velocity, kDefaultChannel, &impl_->controllers);
        if (impl_->patch[136] && !voiceSteal) {
            voice.note->oscSync();
        }
    }

    impl_->lfo.keydown();
}

void Dx7Core::noteOff(int note) {
    if (!impl_ || !impl_->initialized) {
        return;
    }
    for (auto &voice : impl_->voices) {
        if (voice.active && voice.keydown && voice.midi_note == note) {
            voice.keydown = false;
            if (voice.note) {
                voice.note->keyup();
            }
            break;
        }
    }
}

void Dx7Core::render(float *outL, float *outR, int frames) {
    if (!outL || !outR || frames <= 0) {
        return;
    }

    if (!impl_ || !impl_->initialized) {
        std::fill(outL, outL + frames, 0.0f);
        std::fill(outR, outR + frames, 0.0f);
        return;
    }

    int offset = 0;

    if (impl_->extraSize > 0) {
        int n = std::min(frames, impl_->extraSize);
        for (int i = 0; i < n; ++i) {
            outL[i] = impl_->extraBuf[i];
            outR[i] = impl_->extraBuf[i];
        }
        if (n < impl_->extraSize) {
            int remaining = impl_->extraSize - n;
            std::memmove(impl_->extraBuf.data(), impl_->extraBuf.data() + n, sizeof(float) * remaining);
            impl_->extraSize = remaining;
            return;
        }
        impl_->extraSize = 0;
        offset = n;
    }

    while (offset < frames) {
        float sumBuf[N];
        std::fill(std::begin(sumBuf), std::end(sumBuf), 0.0f);

        int32_t lfoValue = impl_->lfo.getsample();
        int32_t lfoDelay = impl_->lfo.getdelay();

        int32_t *audio = impl_->audioBuf.get();
        std::fill(audio, audio + N, 0);

        for (auto &voice : impl_->voices) {
            if (!voice.active || !voice.note) {
                continue;
            }
            voice.note->compute(audio, lfoValue, lfoDelay, &impl_->controllers);
            for (int j = 0; j < N; ++j) {
                int32_t val = audio[j];
                val = val >> 4;
                int clipVal = val < -(1 << 24) ? 0x8000 : val >= (1 << 24) ? 0x7fff : val >> 9;
                float f = clampAudio(static_cast<float>(clipVal) / 32768.0f);
                sumBuf[j] += f;
                audio[j] = 0;
            }
        }

        cleanupVoices();

        int remaining = frames - offset;
        int ncopy = std::min(N, remaining);
        for (int j = 0; j < ncopy; ++j) {
            outL[offset + j] = sumBuf[j];
            outR[offset + j] = sumBuf[j];
        }
        if (ncopy < N) {
            int extra = N - ncopy;
            std::copy(sumBuf + ncopy, sumBuf + N, impl_->extraBuf.data());
            impl_->extraSize = extra;
        }
        offset += ncopy;
    }
}
