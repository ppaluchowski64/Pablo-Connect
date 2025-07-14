#include <P2P/Client.h>

void process(std::unique_ptr<Package<P2P::MessageType>> package) {

}

int main() {
    P2P::Client client;
    client.AddHandler(P2P::MessageType::message, process);
}