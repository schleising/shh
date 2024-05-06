#include "oui.h"
#include "packet.h"
#include "types.h"
#include <cassert>
#include <format>
#include <iostream>
#include <mutex>
#include <ranges>
#include <string>
#include <syncstream>
#include <thread>
#include <vector>

int main() {
  using namespace std::chrono_literals;

  // Shared data structure for stored packets
  auto packet_mutex = std::mutex{};
  auto packets = std::vector<ethernet_packet_t>{};

  // Thread pool
  auto threads = std::vector<std::jthread>{};

  // Create progress thread
  threads.emplace_back([&](std::stop_token token) {
    while (not token.stop_requested()) {
      std::osyncstream{std::cout}
          << std::format("Packets processed: {}\n", std::size(packets));
      std::this_thread::sleep_for(1s);
    }
  });

  // Get all network interfaces
  auto interfaces = cap::interfaces();

  // If there's an "any" interface, just use that
  constexpr auto catch_all = "any";
  if (interfaces.contains(catch_all))
    interfaces = {catch_all};

  assert(not std::empty(interfaces));

  // Start a thread to capture on each interface
  for (auto interface : interfaces) {
    threads.emplace_back(
        [&](std::stop_token token, std::string interface) {
          // Create capture object
          auto capture = cap::packet_t{interface};

          // Capture until stop is requested
          while (not token.stop_requested()) {

            // Read a packet
            auto packet = capture.read();

            // And store it if valid
            std::scoped_lock lock{packet_mutex};
            if (not std::empty(packet.source_.mac_))
              packets.push_back(packet);
          }
        },
        interface);
  }

  // Capture packets for a while
  std::this_thread::sleep_for(5s);

  // Stop all the threads
  for (auto &thread : threads)
    thread.request_stop();

  // Print the packets
  for (auto &packet : packets) {
    // Resolve the vendors or just print the MAC address
    auto source_vendor = oui::lookup(packet.source_.mac_);
    auto dest_vendor = oui::lookup(packet.destination_.mac_);

    std::osyncstream{std::cout} << std::format(
        "{:6} {:04x} {} > {} | {} > {}\n", packet.interface_, packet.type_,
        std::empty(source_vendor) ? packet.source_.mac_ : source_vendor,
        std::empty(dest_vendor) ? packet.destination_.mac_ : dest_vendor,
        packet.source_.ip_, packet.destination_.ip_);
  }
}