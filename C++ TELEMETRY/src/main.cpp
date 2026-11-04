#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <string>
#include <iomanip>

#include "../include/RingBuffer.hpp"
#include "../include/TelemetryPacket.hpp"
#include "../include/TelemetryValidator.hpp"
#include "../include/StatisticsAggregator.hpp"

constexpr size_t BUFFER_CAPACITY = 4096;

// Producer thread worker
void producer_worker(int producer_id, 
                     RingBuffer<TelemetryPacket, BUFFER_CAPACITY>& buffer, 
                     uint32_t num_packets, 
                     bool simulate_io_delay) {
    TelemetryGenerator gen(1337 + producer_id * 101);

    for (uint32_t seq = 1; seq <= num_packets; ++seq) {
        TelemetryPacket pkt = gen.generate(producer_id, seq);

        if (!buffer.push(std::move(pkt))) {
            // Buffer was shut down externally
            break;
        }

        if (simulate_io_delay) {
            // Simulate 200 microseconds hardware polling interval
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    }
}

// Consumer thread worker
void consumer_worker(int consumer_id, 
                     RingBuffer<TelemetryPacket, BUFFER_CAPACITY>& buffer, 
                     StatisticsAggregator& aggregator,
                     bool verbose_log) {
    while (true) {
        // pop() blocks until data arrives or buffer is shut down and empty
        auto opt_pkt = buffer.pop();
        if (!opt_pkt.has_value()) {
            break; // Clean shutdown received and queue drained
        }

        const TelemetryPacket& pkt = *opt_pkt;

        // Calculate end-to-end latency
        uint64_t current_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count()
        );
        uint64_t latency_ns = (current_ns >= pkt.timestamp_ns) ? (current_ns - pkt.timestamp_ns) : 0;

        // Validate packet checksum and threshold limits
        ValidationStatus status = TelemetryValidator::validate(pkt);

        aggregator.record_packet(pkt, status, latency_ns);

        if (verbose_log && (pkt.sequence_id % 250 == 0 || status != ValidationStatus::VALID)) {
            std::cout << "[Consumer " << consumer_id << "] Device " << pkt.device_id
                      << " | Type: " << std::setw(5) << to_string(pkt.type)
                      << " | Val: " << std::fixed << std::setprecision(2) << std::setw(6) << pkt.value
                      << " | Status: " << to_string(status)
                      << " | Latency: " << (latency_ns / 1000.0) << " us\n";
        }
    }
}

void run_live_simulation(int num_producers, int num_consumers, uint32_t packets_per_producer) {
    std::cout << "=======================================================\n";
    std::cout << "     C++ MULTITHREADED TELEMETRY PROCESSING ENGINE    \n";
    std::cout << "=======================================================\n";
    std::cout << "Configuration:\n";
    std::cout << "  Ring Buffer Capacity : " << BUFFER_CAPACITY << " slots\n";
    std::cout << "  Producer Threads     : " << num_producers << "\n";
    std::cout << "  Consumer Threads     : " << num_consumers << "\n";
    std::cout << "  Packets Per Producer : " << packets_per_producer << "\n";
    std::cout << "  Total Packets        : " << (num_producers * packets_per_producer) << "\n";
    std::cout << "=======================================================\n\n";

    RingBuffer<TelemetryPacket, BUFFER_CAPACITY> ring_buffer;
    StatisticsAggregator aggregator;

    auto start_time = std::chrono::steady_clock::now();

    // 1. Spawn Consumer threads first so they are ready to process incoming data
    std::vector<std::thread> consumers;
    consumers.reserve(num_consumers);
    for (int i = 0; i < num_consumers; ++i) {
        consumers.emplace_back(consumer_worker, i + 1, std::ref(ring_buffer), std::ref(aggregator), true);
    }

    // 2. Spawn Producer threads
    std::vector<std::thread> producers;
    producers.reserve(num_producers);
    for (int i = 0; i < num_producers; ++i) {
        producers.emplace_back(producer_worker, i + 1, std::ref(ring_buffer), packets_per_producer, true);
    }

    // 3. Wait for all producers to finish generating data
    for (auto& t : producers) {
        t.join();
    }
    std::cout << "\n[Info] All producers completed transmission. Initiating buffer shutdown...\n";

    // 4. Signal buffer shutdown. This unblocks consumers once the queue is fully drained
    ring_buffer.shutdown();

    // 5. Wait for all consumers to finish processing and exit
    for (auto& t : consumers) {
        t.join();
    }
    std::cout << "[Info] All consumers exited cleanly with zero data loss.\n";

    auto end_time = std::chrono::steady_clock::now();
    double elapsed_sec = std::chrono::duration<double>(end_time - start_time).count();

    // 6. Print final comprehensive metrics
    aggregator.print_report(elapsed_sec);
}

void run_benchmark_suite() {
    std::cout << "\n=======================================================\n";
    std::cout << "          CONCURRENCY PERFORMANCE BENCHMARK            \n";
    std::cout << "=======================================================\n";
    std::cout << "Running throughput stress test across thread topologies...\n\n";

    struct Config {
        int producers;
        int consumers;
        uint32_t packets;
    };

    std::vector<Config> configs = {
        {1, 1, 100000},
        {2, 2, 200000},
        {4, 4, 400000},
        {8, 4, 400000}
    };

    for (const auto& cfg : configs) {
        RingBuffer<TelemetryPacket, BUFFER_CAPACITY> buffer;
        StatisticsAggregator aggregator;
        uint32_t per_producer = cfg.packets / cfg.producers;

        auto start = std::chrono::steady_clock::now();

        std::vector<std::thread> consumers;
        for (int i = 0; i < cfg.consumers; ++i) {
            consumers.emplace_back(consumer_worker, i + 1, std::ref(buffer), std::ref(aggregator), false);
        }

        std::vector<std::thread> producers;
        for (int i = 0; i < cfg.producers; ++i) {
            producers.emplace_back(producer_worker, i + 1, std::ref(buffer), per_producer, false);
        }

        for (auto& t : producers) t.join();
        buffer.shutdown();
        for (auto& t : consumers) t.join();

        auto end = std::chrono::steady_clock::now();
        double sec = std::chrono::duration<double>(end - start).count();
        double throughput = (cfg.packets) / sec;

        std::cout << "Topology [" << cfg.producers << "P x " << cfg.consumers << "C] "
                  << "| Total Packets: " << std::setw(6) << cfg.packets
                  << " | Elapsed: " << std::setw(6) << std::fixed << std::setprecision(3) << sec << " s"
                  << " | Throughput: " << std::setw(10) << std::fixed << std::setprecision(0) << throughput << " pkt/sec\n";
    }
    std::cout << "=======================================================\n\n";
}

int main(int argc, char* argv[]) {
    bool run_bench = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--benchmark") {
            run_bench = true;
        }
    }

    if (run_bench) {
        run_benchmark_suite();
    } else {
        // Live simulation: 4 producers (sensors), 4 consumers (processors), 1,000 packets per producer
        run_live_simulation(4, 4, 1000);
        std::cout << "Tip: Run with '--benchmark' flag to benchmark high-load throughput!\n";
    }

    return 0;
}

