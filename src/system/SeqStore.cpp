#include "SeqStore.h"

void SeqStore::begin(const char* namespaceName) {
    prefs_.begin(namespaceName, /*readOnly=*/false);
    const uint32_t persistedCeiling = prefs_.getUInt("seq_ceil", 0);
    next_ = persistedCeiling + 1;
    persistCeiling(persistedCeiling + SEQ_RESERVE_BATCH);
}

void SeqStore::persistCeiling(uint32_t ceiling) {
    ceiling_ = ceiling;
    prefs_.putUInt("seq_ceil", ceiling);
}

uint32_t SeqStore::nextTxSeq() {
    if (next_ > ceiling_) {
        persistCeiling(ceiling_ + SEQ_RESERVE_BATCH);
    }
    return next_++;
}
