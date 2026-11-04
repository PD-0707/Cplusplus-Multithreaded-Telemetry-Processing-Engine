#pragma once

#include <cstdint>
#include <string>
#include <sstream>
#include <iomanip>
#include <random>
#include <chrono>

// Telemetry sensor types
enum class SensorType : uint8_t {
    TEMPERATURE = 0x01,  // Engine / coolant temp in °C
    PRESSURE    = 0x02,  // Fuel / oil pressure in PSI
    VOLTAGE     = 0x03,  // Bus voltage in Volts
    ACCELERATION= 0x04   // G-force / vibration in m/s^2
};

// Represents a single 32-byte cache-aligned telemetry packet
#pragma pack(push, 1)
struct TelemetryPacket {
    uint32_t device_id;     // Source hardware / sensor identifier
    uint64_t timestamp_ns;  // Epoch timestamp in nanoseconds
    uint32_t sequence_id;   // Monotonic packet sequence counter
    SensorType type;        // Type of sensor telemetry
    float value;            // Sensor reading (float32)
    uint16_t checksum;      // Data integrity checksum
    uint8_t flags;          // Status flags (e.g., bit 0: calibrated, bit 1: simulated)

    // Calculate Fletcher-16 checksum over device_id, timestamp, seq, type, value
    static uint16_t calculate_checksum(uint32_t dev, uint64_t ts, uint32_t seq, SensorType st, float val) {
        uint16_t sum1 = 0, sum2 = 0;
        const uint8_t* ptr = nullptr;

        auto feed_bytes = [&](const void* data, size_t len) {
            ptr = reinterpret_cast<const uint8_t*>(data);
            for (size_t i = 0; i < len; ++i) {
                sum1 = (sum1 + ptr[i]) % 255;
                sum2 = (sum2 + sum1) % 255;
            }
        };

        feed_bytes(&dev, sizeof(dev));
        feed_bytes(&ts, sizeof(ts));
        feed_bytes(&seq, sizeof(seq));
        feed_bytes(&st, sizeof(st));
        feed_bytes(&val, sizeof(val));

        return (sum2 << 8) | sum1;
    }

    bool verify_checksum() const {
        return checksum == calculate_checksum(device_id, timestamp_ns, sequence_id, type, value);
    }
};
#pragma pack(pop)

// Helper utility to generate realistic synthetic telemetry packets
class TelemetryGenerator {
private:
    std::mt19937 rng_;
    std::uniform_real_distribution<float> temp_dist_{10.0f, 130.0f};  // > 105.0 is critical anomaly
    std::uniform_real_distribution<float> press_dist_{20.0f, 95.0f};  // > 85.0 is critical
    std::uniform_real_distribution<float> volt_dist_{11.0f, 14.8f};   // Normal 12V bus
    std::uniform_real_distribution<float> accel_dist_{-5.0f, 5.0f};
    std::uniform_int_distribution<int> type_dist_{1, 4};
    std::uniform_int_distribution<int> error_inject_dist_{1, 100};    // 5% chance of corrupt checksum

public:
    explicit TelemetryGenerator(uint32_t seed = 42) : rng_(seed) {}

    TelemetryPacket generate(uint32_t device_id, uint32_t sequence_id) {
        TelemetryPacket pkt{};
        pkt.device_id = device_id;
        pkt.timestamp_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
        pkt.sequence_id = sequence_id;
        pkt.type = static_cast<SensorType>(type_dist_(rng_));
        pkt.flags = 0x01; // Calibrated

        switch (pkt.type) {
            case SensorType::TEMPERATURE:
                pkt.value = temp_dist_(rng_);
                break;
            case SensorType::PRESSURE:
                pkt.value = press_dist_(rng_);
                break;
            case SensorType::VOLTAGE:
                pkt.value = volt_dist_(rng_);
                break;
            case SensorType::ACCELERATION:
            default:
                pkt.value = accel_dist_(rng_);
                break;
        }

        pkt.checksum = TelemetryPacket::calculate_checksum(
            pkt.device_id, pkt.timestamp_ns, pkt.sequence_id, pkt.type, pkt.value
        );

        // Inject intentional anomaly / corrupted packet in ~5% of packets for testing
        if (error_inject_dist_(rng_) <= 5) {
            pkt.checksum ^= 0xA5A5; // Corrupt the checksum
        }

        return pkt;
    }
};

inline const char* to_string(SensorType type) {
    switch (type) {
        case SensorType::TEMPERATURE:  return "TEMP";
        case SensorType::PRESSURE:     return "PRESSURE";
        case SensorType::VOLTAGE:      return "VOLTAGE";
        case SensorType::ACCELERATION: return "ACCEL";
        default:                       return "UNKNOWN";
    }
}

