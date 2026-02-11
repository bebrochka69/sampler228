#pragma once

#include "synth.h"
#include <memory>
#include <string>

class TuningState {
public:
    virtual ~TuningState() = default;
    virtual int32_t midinote_to_logfreq(int midinote) = 0;
    virtual bool is_standard_tuning() { return true; }
    virtual int scale_length() { return 12; }
    virtual std::string display_tuning_str() { return "Standard Tuning"; }
};

std::shared_ptr<TuningState> createStandardTuning();

// Stubbed helpers for compatibility; custom tunings are not supported in dx7_core.
std::shared_ptr<TuningState> createTuningFromSCLData(const std::string &sclData);
std::shared_ptr<TuningState> createTuningFromKBMData(const std::string &kbmData);
std::shared_ptr<TuningState> createTuningFromSCLAndKBMData(const std::string &sclData, const std::string &kbmData);
