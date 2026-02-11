#include "tuning.h"

namespace {
class StandardTuning : public TuningState {
public:
    StandardTuning() {
        const int base = 50857777;  // (1 << 24) * (log(440) / log(2) - 69/12)
        const int step = (1 << 24) / 12;
        for (int mn = 0; mn < 128; ++mn) {
            current_logfreq_table_[mn] = base + step * mn;
        }
    }

    int32_t midinote_to_logfreq(int midinote) override {
        if (midinote < 0) midinote = 0;
        if (midinote > 127) midinote = 127;
        return current_logfreq_table_[midinote];
    }

private:
    int current_logfreq_table_[128]{};
};
} // namespace

std::shared_ptr<TuningState> createStandardTuning() {
    return std::make_shared<StandardTuning>();
}

std::shared_ptr<TuningState> createTuningFromSCLData(const std::string &) {
    return nullptr;
}

std::shared_ptr<TuningState> createTuningFromKBMData(const std::string &) {
    return nullptr;
}

std::shared_ptr<TuningState> createTuningFromSCLAndKBMData(const std::string &, const std::string &) {
    return nullptr;
}
