#pragma once

#include "TelemetryPacket.hpp"
#include <string>

enum class ValidationStatus {
    VALID = 0,
    CORRUPT_CHECKSUM = 1,
    OUT_OF_BOUNDS_HIGH = 2,
    OUT_OF_BOUNDS_LOW = 3
};

inline const char* to_string(ValidationStatus status) {
    switch (status) {
        case ValidationStatus::VALID:              return "VALID";
        case ValidationStatus::CORRUPT_CHECKSUM:   return "CORRUPT_CHECKSUM";
        case ValidationStatus::OUT_OF_BOUNDS_HIGH: return "OUT_OF_BOUNDS_HIGH";
        case ValidationStatus::OUT_OF_BOUNDS_LOW:  return "OUT_OF_BOUNDS_LOW";
        default:                                   return "UNKNOWN_STATUS";
    }
}

class TelemetryValidator {
public:
    static ValidationStatus validate(const TelemetryPacket& pkt) {
        // Step 1: Checksum verification
        if (!pkt.verify_checksum()) {
            return ValidationStatus::CORRUPT_CHECKSUM;
        }

        // Step 2: Physical bounds checking based on sensor type
        switch (pkt.type) {
            case SensorType::TEMPERATURE:
                if (pkt.value < 0.0f) return ValidationStatus::OUT_OF_BOUNDS_LOW;
                if (pkt.value > 105.0f) return ValidationStatus::OUT_OF_BOUNDS_HIGH;
                break;

            case SensorType::PRESSURE:
                if (pkt.value < 20.0f) return ValidationStatus::OUT_OF_BOUNDS_LOW;
                if (pkt.value > 85.0f) return ValidationStatus::OUT_OF_BOUNDS_HIGH;
                break;

            case SensorType::VOLTAGE:
                if (pkt.value < 11.2f) return ValidationStatus::OUT_OF_BOUNDS_LOW;
                if (pkt.value > 14.6f) return ValidationStatus::OUT_OF_BOUNDS_HIGH;
                break;

            case SensorType::ACCELERATION:
                if (pkt.value < -4.0f) return ValidationStatus::OUT_OF_BOUNDS_LOW;
                if (pkt.value > 4.0f) return ValidationStatus::OUT_OF_BOUNDS_HIGH;
                break;
        }

        return ValidationStatus::VALID;
    }
};

