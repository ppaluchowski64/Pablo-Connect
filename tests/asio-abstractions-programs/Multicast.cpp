#include <Scanner.h>
#include <thread>

int main() {
    LanDeviceScanner::BeginScan();

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}