#pragma once

#include "TelemetryPacket.hpp"
#include "TelemetryValidator.hpp"
#include <atomic>
#include <mutex>
#include <iostream>
#include <iomanip>
#include <limits>
#include <map>

struct SensorMetricStats {
    float min_val = std::numeric_limits<float>::max();
    float max_val = std::numeric_limits<float>::lowest();
    double sum_val = 0.0;
    uint64_t count = 0;

    void update(float val) {
        if (val < min_val) min_val = val;
        if (val > max_val) max_val = val;
        sum_val += val;
        ++count;
    }

    float average() const {
        return count > 0 ? static_cast<float>(sum_val / count) : 0.0f;
    }
};

class StatisticsAggregator {
private:
    std::atomic<uint64_t> total_processed_{0};
    std::atomic<uint64_t> valid_packets_{0};
    std::atomic<uint64_t> corrupt_packets_{0};
    std::atomic<uint64_t> anomaly_packets_{0};

    // Latency tracking in nanoseconds
    std::atomic<uint64_t> total_latency_ns_{0};
    std::atomic<uint64_t> min_latency_ns_{std::numeric_limits<uint64_t>::max()};
    std::atomic<uint64_t> max_latency_ns_{0};

    mutable std::mutex stats_mtx_;
    std::map<SensorType, SensorMetricStats> sensor_stats_;

public:
    void record_packet(const TelemetryPacket& pkt, ValidationStatus status, uint64_t latency_ns) {
        total_processed_.fetch_add(1, std::memory_order_relaxed);

        if (status == ValidationStatus::VALID) {
            valid_packets_.fetch_add(1, std::memory_order_relaxed);
        } else if (status == ValidationStatus::CORRUPT_CHECKSUM) {
            corrupt_packets_.fetch_add(1, std::memory_order_relaxed);
        } else {
            anomaly_packets_.fetch_add(1, std::memory_order_relaxed);
        }

        // Update latency stats
        total_latency_ns_.fetch_add(latency_ns, std::memory_order_relaxed);

        // Atomic min update
        uint64_t current_min = min_latency_ns_.load(std::memory_order_relaxed);
        while (latency_ns < current_min &&
               !min_latency_ns_.compare_exchange_weak(current_min, latency_ns, std::memory_order_relaxed)) {
            // retry until either condition fails or swap succeeds
        }

        // Atomic max update
        uint64_t current_max = max_latency_ns_.load(std::memory_order_relaxed);
        while (latency_ns > current_max &&
               !max_latency_ns_.compare_exchange_weak(current_max, latency_ns, std::memory_order_relaxed)) {
            // retry
        }

        // Only aggregate sensor values if checksum was valid
        if (status != ValidationStatus::CORRUPT_CHECKSUM) {
            std::lock_guard<std::mutex> lock(stats_mtx_);
            sensor_stats_[pkt.type].update(pkt.value);
        }
    }

    uint64_t total_processed() const { return total_processed_.load(); }
    uint64_t valid_packets() const { return valid_packets_.load(); }
    uint64_t corrupt_packets() const { return corrupt_packets_.load(); }
    uint64_t anomaly_packets() const { return anomaly_packets_.load(); }

    void print_report(double elapsed_seconds) const {
        uint64_t total = total_processed_.load();
        uint64_t valid = valid_packets_.load();
        uint64_t corrupt = corrupt_packets_.load();
        uint64_t anomaly = anomaly_packets_.load();
        uint64_t min_lat = min_latency_ns_.load();
        uint64_t max_lat = max_latency_ns_.load();
        uint64_t tot_lat = total_latency_ns_.load();

        double throughput = elapsed_seconds > 0.0 ? (total / elapsed_seconds) : 0.0;
        double avg_lat_us = total > 0 ? (tot_lat / static_cast<double>(total)) / 1000.0 : 0.0;

        std::cout << "\n=======================================================\n";
        std::cout << "        TELEMETRY PROCESSING PERFORMANCE REPORT        \n";
        std::cout << "=======================================================\n";
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Elapsed Time              : " << elapsed_seconds * 1000.0 << " ms\n";
        std::cout << "Throughput                : " << throughput << " packets/sec\n";
        std::cout << "Total Packets Processed   : " << total << "\n";
        std::cout << "Valid Packets             : " << valid 
                  << " (" << (total > 0 ? (valid * 100.0 / total) : 0.0) << "%)\n";
        std::cout << "Checksum Corrupted Drops  : " << corrupt 
                  << " (" << (total > 0 ? (corrupt * 100.0 / total) : 0.0) << "%)\n";
        std::cout << "Out-of-Bounds Anomalies   : " << anomaly 
                  << " (" << (total > 0 ? (anomaly * 100.0 / total) : 0.0) << "%)\n";
        std::cout << "-------------------------------------------------------\n";
        std::cout << "Pipeline Latency (Ingest -> Processed):\n";
        std::cout << "  Min Latency             : " << (min_lat == std::numeric_limits<uint64_t>::max() ? 0.0 : min_lat / 1000.0) << " us\n";
        std::cout << "  Avg Latency             : " << avg_lat_us << " us\n";
        std::cout << "  Max Latency             : " << max_lat / 1000.0 << " us\n";
        std::cout << "-------------------------------------------------------\n";
        std::cout << "Sensor Value Aggregation Summary:\n";

        std::lock_guard<std::mutex> lock(stats_mtx_);
        for (const auto& [type, stats] : sensor_stats_) {
            std::cout << "  [" << std::setw(11) << std::left << to_string(type) << "]"
                      << " Count: " << std::setw(7) << stats.count
                      << " Min: " << std::setw(7) << stats.min_val
                      << " Avg: " << std::setw(7) << stats.average()
                      << " Max: " << std::setw(7) << stats.max_val << "\n";
        }
        std::cout << "=======================================================\n\n";
    }
};

