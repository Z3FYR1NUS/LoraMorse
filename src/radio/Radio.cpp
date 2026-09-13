#include "Radio.h"

#include <LoRa.h>
#include <SPI.h>

#include "config.h"

bool Radio::begin() {
    SPI.begin(pins::LORA_SCK, pins::LORA_MISO, pins::LORA_MOSI, pins::LORA_CS);
    LoRa.setPins(pins::LORA_CS, pins::LORA_RST, pins::LORA_DIO0);

    ready_ = LoRa.begin(radio_cfg::FREQUENCY_HZ) != 0;
    if (!ready_) return false;

    // Explicit link parameters -- both ends of the link must agree on all of
    // these. Previously these were left at the library's defaults, so a
    // library upgrade that changed a default would silently change the
    // on-air behavior.
    LoRa.setTxPower(radio_cfg::TX_POWER_DBM);
    LoRa.setSpreadingFactor(radio_cfg::SPREADING_FACTOR);
    LoRa.setSignalBandwidth(radio_cfg::BANDWIDTH_HZ);
    LoRa.setCodingRate4(radio_cfg::CODING_RATE_DENOM);
    LoRa.setSyncWord(radio_cfg::SYNC_WORD);
    LoRa.setPreambleLength(radio_cfg::PREAMBLE_LENGTH);
    if (radio_cfg::ENABLE_CRC) LoRa.enableCrc();
    else LoRa.disableCrc();

    LoRa.parsePacket();  // Arm reception immediately.
    return true;
}

bool Radio::beginPacket() { return LoRa.beginPacket() != 0; }

bool Radio::sendPacket(const uint8_t* data, size_t len) {
    if (LoRa.write(data, len) != len) return false;
    return LoRa.endPacket(/*async=*/true) != 0;
}

bool Radio::isTransmitting() { return LoRa.isTransmitting(); }

void Radio::idle() { LoRa.idle(); }

int Radio::poll() { return LoRa.parsePacket(); }

int Radio::available() { return LoRa.available(); }

int Radio::read() { return LoRa.read(); }

int Radio::lastRssi() const { return LoRa.packetRssi(); }

float Radio::lastSnr() const { return LoRa.packetSnr(); }
