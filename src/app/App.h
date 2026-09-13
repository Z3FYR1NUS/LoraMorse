#pragma once
// App -- owns all runtime state and orchestrates the modules. This replaces
// the original single main.cpp: setup()/loop() here just call App::begin()/
// App::loop().
//
// The main behavioral addition versus the original firmware is real
// delivery semantics for the radio link:
//
//   KEY PRESS -> QUEUE -> TX -> WAIT ACK -> ACK = DELIVERED
//                                 |
//                                 +-- timeout -> RETRY (up to link_cfg::MAX_RETRIES)
//                                 +-- retries exhausted -> FAILED
//
// TX_LED reflects RF transmission only, exactly as before -- it is
// explicitly NOT a delivery indicator. Delivery/failure are reported
// through the status line instead ("DELIVERED", "RETRY n", "FAILED").

#include <stdint.h>

#include "config.h"
#include "crypto/Crypto.h"
#include "display/Display.h"
#include "input/Button.h"
#include "morse/Morse.h"
#include "protocol/Packet.h"
#include "protocol/ReplayGuard.h"
#include "radio/Radio.h"
#include "system/OtaManager.h"
#include "system/SeqStore.h"
#include "system/WifiManager.h"

class App {
public:
    void begin();
    void loop();

private:
    // --- Reliability state machine for the single outstanding DATA packet ---
    enum class TxState { IDLE, SENDING, WAIT_ACK };

    struct QueuedToken {
        char token = '\0';    // '.', '-', '/', or ' '
        char decoded = '\0';  // decoded letter for status text, or '\0'/' '
    };

    struct Diagnostics {
        uint32_t txAttempts = 0;
        uint32_t delivered = 0;
        uint32_t retries = 0;
        uint32_t failed = 0;
        uint32_t badPackets = 0;
        uint32_t replayedPackets = 0;
        uint32_t acksSent = 0;
    };

    // --- Setup helpers ---
    void loadSecrets();
    void configureOta();

    // --- Per-loop services (mirrors the original sketch's service* split) ---
    void serviceInputs(uint32_t now);
    void serviceRadioRx(uint32_t now);
    void serviceRadioTx(uint32_t now);
    void serviceOutputs(uint32_t now);
    void serviceTimers(uint32_t now);
    void serviceDisplay(uint32_t now);

    // --- TX queue / Morse helpers ---
    bool queueToken(char token, char decoded, bool reserveDelimiter);
    bool finishOutgoingCharacter();
    void sendWordSpace();
    void clearBuffers();
    void resetInputs(uint32_t now);
    void stopOutputs();

    // --- RX helpers ---
    void handleReceivedToken(char token, uint32_t now);
    void finishReceivedCharacter();
    void appendReceivedLetter(char letter);
    void queueBeep(char token);

    // --- Status / diagnostics text ---
    void setStatus(const char* text);
    void letterStatus(const char* prefix, char letter);
    void composeFooter(uint32_t now, char* out, size_t outCap) const;

    // --- Radio send helpers ---
    void sendFramed(PacketType type, uint8_t dst, uint32_t seq, const uint8_t* payload,
                     uint8_t payloadLen);
    void beginTxFromQueue(uint32_t now);
    void randomNonce(uint8_t out[Crypto::NONCE_LEN]);

    // Modules
    Radio radio_;
    Crypto crypto_;
    ReplayGuard replay_;
    SeqStore seqStore_;
    WifiManager wifi_;
    OtaManager ota_;
    Display display_;
    Button key_{pins::KEY_INPUT};
    Button control_{pins::CONTROL_INPUT};

    // Addressing (loaded from secrets.h)
    uint8_t thisDeviceId_ = 0;
    uint8_t peerDeviceId_ = 0;

    bool radioReady_ = false;
    bool otaActive_ = false;
    uint8_t otaPercent_ = 0;

    // Morse state
    morse::Marks outgoing_, incoming_;
    char lastTxChar_ = '\0', lastRxChar_ = '\0';
    bool rxDiscarding_ = false;
    bool ignoreKeyUntilRelease_ = false, controlHandled_ = false;
    uint32_t keyStartedAt_ = 0, controlStartedAt_ = 0, lastMarkAt_ = 0, lastRxMarkAt_ = 0;

    // TX queue
    QueuedToken txQueue_[app_cfg::TX_QUEUE_SIZE] = {};
    uint8_t txHead_ = 0, txTail_ = 0, txCount_ = 0;

    // Reliability state for the item currently being sent
    TxState txState_ = TxState::IDLE;
    QueuedToken inFlight_;
    uint32_t currentSeq_ = 0;
    uint8_t retryCount_ = 0;
    uint32_t txStartedAt_ = 0;    // when the current SENDING/WAIT_ACK cycle began
    uint32_t lastSendAt_ = 0;     // when the frame currently in flight was last put on air
    uint8_t framedPacket_[Packet::MAX_WIRE_LEN] = {};
    size_t framedPacketLen_ = 0;

    // Received text log
    char receivedText_[app_cfg::LOG_CAPACITY + 1] = "";
    uint8_t receivedLength_ = 0;

    // RSSI/SNR
    bool hasRssi_ = false;
    int lastRssi_ = 0;
    float lastSnr_ = 0.0f;

    // LEDs / beeper
    bool txPulse_ = false, rxPulse_ = false;
    bool beepOn_ = false, beepGap_ = false, buzzerHigh_ = false;
    uint16_t beepQueue_[app_cfg::BEEP_QUEUE_SIZE] = {};
    uint8_t beepHead_ = 0, beepTail_ = 0, beepCount_ = 0;
    uint32_t txPulseAt_ = 0, rxPulseAt_ = 0, beepAt_ = 0, beepDuration_ = 0;

    // Status / footer
    char statusText_[32] = "";
    bool statusActive_ = false;
    uint32_t statusAt_ = 0;
    bool footerShowsIp_ = false;
    uint32_t footerAt_ = 0;

    Diagnostics diag_;
};
