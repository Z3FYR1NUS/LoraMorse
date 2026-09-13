#include "ReplayGuard.h"

ReplayGuard::ReplayGuard() { reset(); }

void ReplayGuard::reset() {
    for (auto& e : entries_) {
        e.used = false;
        e.peerId = 0;
        e.highestSeq = 0;
    }
}

ReplayGuard::Entry* ReplayGuard::find(uint8_t peerId) {
    for (auto& e : entries_) {
        if (e.used && e.peerId == peerId) return &e;
    }
    return nullptr;
}

ReplayGuard::Entry* ReplayGuard::findOrAllocate(uint8_t peerId) {
    if (Entry* e = find(peerId)) return e;
    for (auto& e : entries_) {
        if (!e.used) {
            e.used = true;
            e.peerId = peerId;
            e.highestSeq = 0;
            return &e;
        }
    }
    return nullptr;
}

ReplayGuard::Verdict ReplayGuard::check(uint8_t peerId, uint32_t seq) {
    Entry* existing = find(peerId);
    if (!existing) {
        Entry* fresh = findOrAllocate(peerId);
        if (!fresh) return Verdict::TABLE_FULL;
        fresh->highestSeq = seq;
        return Verdict::NEW;
    }
    if (seq > existing->highestSeq) {
        existing->highestSeq = seq;
        return Verdict::NEW;
    }
    if (seq == existing->highestSeq) return Verdict::DUPLICATE;
    return Verdict::REPLAY;
}
