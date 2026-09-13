#pragma once
// SeqStore -- persists the outgoing sequence counter in NVS (via the
// Preferences library) so a reboot can never reuse a sequence number the
// peer has already seen. Reusing a sequence number would let ReplayGuard on
// the far end either wrongly reject a legitimate fresh packet as a replay,
// or -- worse -- accept a genuinely replayed old packet as new.
//
// Writing NVS on every single dot/dash would wear the flash down over a
// device's lifetime, so this reserves sequence numbers in batches: it
// persists a "ceiling" value and hands out numbers up to that ceiling from
// RAM, only touching flash again once the batch is exhausted. On an unclean
// reboot, up to (SEQ_RESERVE_BATCH - 1) sequence numbers are permanently
// skipped -- harmless, since the sequence number is a 32-bit counter with
// effectively unlimited headroom for this device's lifetime.

#include <Preferences.h>
#include <stdint.h>

class SeqStore {
public:
    static constexpr uint32_t SEQ_RESERVE_BATCH = 32;

    // Opens the NVS namespace and reserves the first batch. Sequence numbers
    // start at 1 (0 is never issued, so it can be used as a "not yet seen"
    // sentinel elsewhere).
    void begin(const char* namespaceName = "lora-cw");

    // Returns the next sequence number to use for an outgoing DATA packet.
    uint32_t nextTxSeq();

private:
    Preferences prefs_;
    uint32_t next_ = 1;
    uint32_t ceiling_ = 0;

    void persistCeiling(uint32_t ceiling);
};
