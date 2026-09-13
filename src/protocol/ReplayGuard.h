#pragma once
// ReplayGuard -- rejects replayed/out-of-order DATA packets per sender.
//
// The link is stop-and-wait (one outstanding DATA packet at a time, see
// App/reliability notes in the README), so a simple monotonically
// increasing highest-sequence-seen check per peer is sufficient -- no
// sliding-window bitmap is needed here. If this project ever grows to allow
// multiple outstanding packets per peer, this is the place a replay
// window/bitmap would replace the single counter.
//
// The previous protocol had no sequence numbers at all: a captured packet
// could be re-sent by an attacker and would decrypt to a valid token again.

#include <stdint.h>

class ReplayGuard {
public:
    static constexpr uint8_t MAX_PEERS = 4;

    enum class Verdict {
        NEW,        // first time this peer's sequence has reached this value or higher -- accept and process
        DUPLICATE,  // exactly equal to the last accepted sequence -- already processed; safe to re-ACK, don't reprocess
        REPLAY,     // strictly less than the last accepted sequence -- reject outright
        TABLE_FULL, // an unrecognized peer arrived and there's no free slot -- reject
    };

    ReplayGuard();

    // Checks `seq` from `peerId` against that peer's history. On NEW, the
    // peer's highest-seen sequence is updated as a side effect; on
    // DUPLICATE/REPLAY/TABLE_FULL, state is left unchanged.
    Verdict check(uint8_t peerId, uint32_t seq);

    // Clears all learned peer state (e.g. on a deliberate local reset).
    void reset();

private:
    struct Entry {
        bool used = false;
        uint8_t peerId = 0;
        uint32_t highestSeq = 0;
    };
    Entry entries_[MAX_PEERS];

    Entry* find(uint8_t peerId);
    Entry* findOrAllocate(uint8_t peerId);
};
