#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <SSL/CertificateManager.h>
#include <SSL/Connection.h>

// Define a simple package type for the benchmark
enum class BenchmarkPackageType : uint16_t {
    BENCHMARK_PACKET,
};

// Global atomic counter for received packets
std::atomic<uint64_t> g_packetsReceived = 0;
// Global flag to stop the benchmark
std::atomic<bool> g_stopBenchmark = false;

// Server's callback when a client connects
void on_server_connection(std::shared_ptr<Connection<BenchmarkPackageType>> connection) {
    std::cout << "Client connected with ID: " << connection->GetConnectionID() << std::endl;
}

// Client's callback when connected to the server
void on_client_connection(std::shared_ptr<Connection<BenchmarkPackageType>> connection) {
    std::cout << "Connected to server with ID: " << connection->GetConnectionID() << std::endl;
    // Start sending packets
    std::thread sender_thread([connection]() {
        while (!g_stopBenchmark) {
            // Send a simple packet with no payload
            connection->Send(BenchmarkPackageType::BENCHMARK_PACKET, PackageFlag::NONE);
        }
    });
    sender_thread.detach();
}

int main() {
    // 1. Setup
    const std::filesystem::path certPath = "./certs";
    const int benchmark_duration_seconds = 10;

    // Generate certificates if they don't exist or are invalid
    if (!CertificateManager::IsCertificateValid(certPath)) {
        std::cout << "Generating new SSL certificates..." << std::endl;
        CertificateManager::GenerateCertificate(certPath);
    }

    // Create IO contexts for server and client
    asio::io_context server_io_context;
    asio::io_context client_io_context;

    // Create SSL contexts
    auto server_ssl_context = Connection<BenchmarkPackageType>::CreateSSLContext(certPath, true);
    auto client_ssl_context = Connection<BenchmarkPackageType>::CreateSSLContext(certPath, false);

    // Create thread-safe deques for incoming packages
    ts::deque<PackageIn<BenchmarkPackageType>> server_in_deque;
    ts::deque<PackageIn<BenchmarkPackageType>> client_in_deque; // Not used in this benchmark

    // 2. Create Server
    auto server_connection = Connection<BenchmarkPackageType>::Create(server_io_context, server_ssl_context, server_in_deque);
    Acceptor connection_acceptor(server_io_context, Endpoint(asio::ip::tcp::v4(), SSL_CONNECTION_PORT));
    Acceptor file_stream_acceptor(server_io_context, Endpoint(asio::ip::tcp::v4(), SSL_FILE_STREAM_PORT));
    server_connection->Seek(connection_acceptor, file_stream_acceptor, on_server_connection);

    // Run the server io_context in a separate thread
    std::thread server_thread1([&server_io_context]() {
        server_io_context.run();
    });

    std::thread server_thread2([&server_io_context]() {
        server_io_context.run();
    });


    // 3. Create Client
    auto client_connection = Connection<BenchmarkPackageType>::Create(client_io_context, client_ssl_context, client_in_deque);
    IPAddress server_address = asio::ip::make_address("127.0.0.1");
    client_connection->Start(server_address, SSL_CONNECTION_PORT, SSL_FILE_STREAM_PORT, on_client_connection);

    // Run the client io_context in a separate thread
    std::thread client_thread1([&client_io_context]() {
        client_io_context.run();
    });

    // 4. Run Benchmark
    std::cout << "Starting benchmark for " << benchmark_duration_seconds << " seconds..." << std::endl;

    auto start_time = std::chrono::high_resolution_clock::now();
    while (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start_time).count() < benchmark_duration_seconds) {
        // Pop and count packets on the server side
        if (!server_in_deque.empty()) {
            server_in_deque.pop_front();
            g_packetsReceived++;
        }
    }
    g_stopBenchmark = true;

    while (!server_in_deque.empty()) {
        server_in_deque.pop_front();
        g_packetsReceived++;
    }

    // 5. Results
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    std::cout << "Benchmark finished." << std::endl;
    std::cout << "Total packets received: " << g_packetsReceived << std::endl;
    std::cout << "Total time: " << duration / 1000.0 << " seconds" << std::endl;
    std::cout << "Packets per second: " << (g_packetsReceived * 1000.0) / duration << std::endl;

    // 6. Cleanup
    client_connection->Disconnect();
    server_connection->Disconnect();

    server_io_context.stop();
    client_io_context.stop();


    return 0;
}