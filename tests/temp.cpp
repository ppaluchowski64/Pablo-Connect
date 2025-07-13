#include <iostream>
#include <chrono>
#include <fstream>
#include <thread>
#include <vector>
#include <iomanip>
#include <future>
#include <atomic>
#include <SSL/Connection.h>
#include <SSL/CertificateManager.h>

// Define a simple package enum for the benchmark
enum class BenchmarkPackageType : uint16_t {
    FileTransfer
};

// Generates a test file of an exact size in bytes
void GenerateTestFile(const std::filesystem::path& path, size_t size_in_bytes) {
    std::ofstream ofs(path, std::ios::binary | std::ios::out);
    if (!ofs) {
        std::cerr << "Failed to open file for writing: " << path << std::endl;
        return;
    }

    // Write in chunks for larger files
    if (size_in_bytes > 0) {
        size_t remaining = size_in_bytes;
        const size_t chunk_size = 1024 * 1024; // 1MB
        std::vector<char> buffer(chunk_size, 0);

        while (remaining > 0) {
            size_t to_write = std::min(remaining, chunk_size);
            ofs.write(buffer.data(), to_write);
            remaining -= to_write;
        }
    }
}

// Helper function to format file sizes for printing
std::string FormatSize(size_t bytes) {
    std::stringstream ss;
    if (bytes < 1024) {
        ss << bytes << " B";
    } else if (bytes < 1024 * 1024) {
        ss << std::fixed << std::setprecision(2) << (bytes / 1024.0) << " KB";
    } else if (bytes < 1024 * 1024 * 1024) {
        ss << std::fixed << std::setprecision(2) << (bytes / (1024.0 * 1024.0)) << " MB";
    } else {
        ss << std::fixed << std::setprecision(2) << (bytes / (1024.0 * 1024.0 * 1024.0)) << " GB";
    }
    return ss.str();
}

int main() {
    const std::string certPath = "certs";
    const std::string testFilename = "test_file.bin";
    const std::string receivedFilename = "received_test_file.bin";

    // --- Generate SSL certificate ---
    std::filesystem::create_directory(certPath);
    CertificateManager::GenerateCertificate(certPath);

    // --- Setup a single ASIO context and connections ---
    asio::io_context ioContext;
    ts::deque<PackageIn<BenchmarkPackageType>> serverInDeque, clientInDeque;
    auto serverSSLContext = Connection<BenchmarkPackageType>::CreateSSLContext(certPath, true);
    auto clientSSLContext = Connection<BenchmarkPackageType>::CreateSSLContext(certPath, false);

    Acceptor connectionAcceptor(ioContext, Endpoint(asio::ip::tcp::v4(), SSL_CONNECTION_PORT));
    Acceptor fileStreamAcceptor(ioContext, Endpoint(asio::ip::tcp::v4(), SSL_FILE_STREAM_PORT));

    // Promises to ensure connections are established before testing
    std::promise<std::shared_ptr<Connection<BenchmarkPackageType>>> serverPromise;
    std::promise<std::shared_ptr<Connection<BenchmarkPackageType>>> clientPromise;
    auto serverFuture = serverPromise.get_future();
    auto clientFuture = clientPromise.get_future();

    // Create and run the server
    auto serverConn = Connection<BenchmarkPackageType>::Create(ioContext, serverSSLContext, serverInDeque);
    serverConn->Seek(connectionAcceptor, fileStreamAcceptor, [&](auto conn) {
        serverPromise.set_value(conn);
    });

    // Create and run the client
    auto clientConn = Connection<BenchmarkPackageType>::Create(ioContext, clientSSLContext, clientInDeque);
    clientConn->Start(
        Endpoint(asio::ip::make_address_v4("127.0.0.1"), SSL_CONNECTION_PORT),
        Endpoint(asio::ip::make_address_v4("127.0.0.1"), SSL_FILE_STREAM_PORT),
        [&](auto conn) {
            clientPromise.set_value(conn);
        }
    );

    // Run the IO context in a background thread
    std::thread asioThread([&]() { ioContext.run(); });

    // Wait for both client and server to confirm connection
    std::cout << "Waiting for client-server connection..." << std::endl;
    auto server = serverFuture.get();
    auto client = clientFuture.get();
    std::cout << "Connection established. Starting benchmark." << std::endl;

    // --- Define the file sizes for the benchmark ---
    std::vector<size_t> test_sizes;
    const size_t max_size = 2ULL * 1024 * 1024 * 1024; // 2 GB
    for (size_t size = 1; size <= max_size; size *= 2) {
        test_sizes.push_back(size);
    }

    // --- Print table header ---
    std::cout << "\n+-----------------+-----------------+--------------------+" << std::endl;
    std::cout << "| File Size       | Transfer Time   | Speed (MB/s)       |" << std::endl;
    std::cout << "+-----------------+-----------------+--------------------+" << std::endl;

    // --- Run benchmark for each file size on the SAME connection ---
    for (const auto& size : test_sizes) {
        GenerateTestFile(testFilename, size);

        auto start = std::chrono::high_resolution_clock::now();

        // Request the file
        client->RequestFile(testFilename, receivedFilename);

        // Wait for the received file to match the source size
        while (true) {
            std::error_code ec;
            if (std::filesystem::exists(receivedFilename, ec) && !ec) {
                if (std::filesystem::file_size(receivedFilename, ec) == size && !ec) {
                    break; // Transfer is complete
                }
            }
            // Don't poll too aggressively
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;

        // --- Calculate and Print Results ---
        double speed_mb_s = (elapsed.count() > 0) ? (static_cast<double>(size) / (1024 * 1024)) / elapsed.count() : 0;

        std::cout << "| " << std::left << std::setw(15) << FormatSize(size)
                  << "| " << std::left << std::setw(15) << std::fixed << std::setprecision(4) << elapsed.count() << " s"
                  << "| " << std::left << std::setw(18) << std::fixed << std::setprecision(2) << speed_mb_s << " |" << std::endl;

        // Cleanup files for the next run
        std::filesystem::remove(testFilename);
        std::filesystem::remove(receivedFilename);
    }

    std::cout << "+-----------------+-----------------+--------------------+" << std::endl;

    // --- Final Cleanup ---
    std::cout << "\nBenchmark finished. Disconnecting." << std::endl;
    client->Disconnect();
    server->Disconnect();
    ioContext.stop();
    asioThread.join();

    std::filesystem::remove_all(certPath);

    return 0;
}