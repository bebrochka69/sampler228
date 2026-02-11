#pragma once

#include <cmath>

struct MTSClient {
};

inline MTSClient *MTS_RegisterClient() {
    return nullptr;
}

inline void MTS_DeregisterClient(MTSClient *) {
}

inline bool MTS_HasMaster(MTSClient *) {
    return false;
}

inline double MTS_NoteToFrequency(MTSClient *, int midinote, int /*channel*/) {
    return 440.0 * std::pow(2.0, (midinote - 69) / 12.0);
}
