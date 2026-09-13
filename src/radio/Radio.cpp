#include "Radio.h"

#include <LoRa.h>
#include <SPI.h>

#include "config.h"

Radio* Radio::instance_ = nullptr;

bool Radio::begin() {
    SPI.begin(pins::LORA_SCK, pins::LORA_MISO, pins::LORA_MOSI, pins::LORA_CS);
    LoRa.setPins(pins::LORA_CS, pins::LORA_RST, pins::LORA_DIO0);

    ready_ = LoRa.begin(radio_cfg::FREQUENCY_HZ) != 0;
    if (!ready_) {
        return false;
    }

    LoRa.setTxPower(radio_cfg::TX_POWER_DBM);
    LoRa.setSpreadingFactor(radio_cfg::SPREADING_FACTOR);
    LoRa.setSignalBandwidth(radio_cfg::BANDWIDTH_HZ);
    LoRa.setCodingRate4(radio_cfg::CODING_RATE_DENOM);
    LoRa.setSyncWord(radio_cfg::SYNC_WORD);
    LoRa.setPreambleLength(radio_cfg::PREAMBLE_LENGTH);

    if (radio_cfg::ENABLE_CRC) {
        LoRa.enableCrc();
    } else {
        LoRa.disableCrc();
    }

    instance_ = this;
    LoRa.onTxDone(onTxDoneStatic);

    transmitting_ = false;
    LoRa.parsePacket();

    return true;
}

bool Radio::beginPacket() {
    return LoRa.beginPacket() != 0;
}

bool Radio::sendPacket(const uint8_t* data, size_t len) {
    if (transmitting_) {
        return false;
    }

    if (LoRa.write(data, len) != len) {
        return false;
    }

    if (LoRa.endPacket(/*async=*/true) == 0) {
        return false;
    }

    transmitting_ = true;
    return true;
}

bool Radio::isTransmitting() {
    return transmitting_;
}

void Radio::onTxDoneStatic() {
    if (instance_ != nullptr) {
        instance_->onTxDone();
    }
}

void Radio::onTxDone() {
    transmitting_ = false;
}

void Radio::idle() {
    LoRa.idle();
    transmitting_ = false;
}

int Radio::poll() {
    return LoRa.parsePacket();
}

int Radio::available() {
    return LoRa.available();
}

int Radio::read() {
    return LoRa.read();
}

int Radio::lastRssi() const {
    return LoRa.packetRssi();
}

float Radio::lastSnr() const {
    return LoRa.packetSnr();
}
