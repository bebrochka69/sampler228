#pragma once

#include <cstdint>
#include <memory>
#include <string>

class Dx7Core {
public:
    Dx7Core();
    ~Dx7Core();

    Dx7Core(const Dx7Core &) = delete;
    Dx7Core &operator=(const Dx7Core &) = delete;

    void init(int sampleRate, int voices);
    void noteOn(int note, int velocity);
    void noteOff(int note);
    void render(float *outL, float *outR, int frames);

    bool loadVoiceParameters(const uint8_t *data, int size);
    bool loadSysexFile(const std::string &path);
    bool selectProgram(int index);
    int programCount() const;
    const char *programName(int index) const;
    bool lastLoadChanged() const;

private:
    struct Voice;
    struct Impl;

    void resetState();
    int findVoiceForNote(int note) const;
    int findFreeVoice();
    void cleanupVoices();

    std::unique_ptr<Impl> impl_;
};
