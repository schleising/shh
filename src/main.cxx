#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <map>
#include <mutex>
#include <pcap.h>
#include <print>
#include <ranges>
#include <string.h>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Forward declarations
namespace oui {
std::string lookup(const std::string_view);
}

// Information about devices
struct device_t {
  size_t packets{};
  size_t packet_length{};
  uint16_t packet_type{};
  std::string_view network{};
  std::string ip{};
  std::string vendor{};
};

// Capture packets from the given network interface
auto capture(std::string_view network_interface) {

  auto devices = std::multimap<std::string, device_t>{};

  // Open the network interface in promiscuous mode
  char errbuf[256];
  pcap_t *pcap = pcap_open_live(std::string{network_interface}.c_str(), 65535, 1,
                                1000, errbuf);

  if (pcap == nullptr) {
    std::println("Error opening network interface: '{}'",
                 std::string_view(errbuf));
    return devices;
  }

  // Process number of packets
  for (auto _ : std::views::iota(0, 20)) {

    // Read packets
    pcap_pkthdr header;
    const u_char *data = pcap_next(pcap, &header);

    if (data == nullptr)
      continue;

    // Create a new device to describe this packet
    auto device_source = device_t{.network = network_interface};
    auto device_dest = device_t{.network = network_interface};

    // Extract MAC addresses
    auto mac_source = std::string{};
    for (auto i = size_t{6}; i < 12; ++i)
      mac_source += std::format("{:02x}", data[i]) + "-";

    auto mac_dest = std::string{};
    for (auto i = size_t{0}; i < 6; ++i)
      mac_dest += std::format("{:02x}", data[i]) + "-";

    auto packet_type = data[12] << 8 | data[13];

    // Copy these data into the outgoing device
    device_source.network = device_dest.network = network_interface;
    device_source.packet_type = device_dest.packet_type = packet_type;
    device_source.packet_length = device_dest.packet_length = header.len;
    device_source.packets = device_dest.packets = 1;

    // Only set IP address if it's a suitable packet type
    if (packet_type == 0x0800) {
      device_source.ip = std::format("{}.{}.{}.{}", data[26], data[27], data[28], data[29]);
      device_dest.ip = std::format("{}.{}.{}.{}", data[30], data[31], data[32], data[33]);
    }

    // Add device to the list
    devices.emplace(mac_source, device_source);
    devices.emplace(mac_dest, device_dest);
  }

  return devices;
}

int main() {

  using namespace std::chrono_literals;

  // List network interfaces
  std::println("Network interfaces:");

  pcap_if_t *alldevs;
  char errbuf[256];

  if (pcap_findalldevs(&alldevs, errbuf) == -1) {
    std::println("Error in pcap_findalldevs: {}", errbuf);
    return 1;
  }

  std::vector<std::string> network_interfaces{};

  for (pcap_if_t *d = alldevs; d != nullptr; d = d->next)
    network_interfaces.push_back(d->name);

  assert(not std::empty(network_interfaces));

  for (auto d : network_interfaces)
    std::println("\t{}", d);

  std::println("READY");
  std::this_thread::sleep_for(2s);

  // Control the threads
  std::atomic_bool run{true};

  // Shared MAC data
  std::mutex mac_mutex;
  std::map<std::string, device_t> devices;

  // Create container for all threads
  std::vector<std::thread> threads;

  // Search for MAC addresses
  threads.emplace_back([&]() {
    while (run) {

      // Use only the first network interface
      auto dev = network_interfaces.front();

      // Capture packets from the chosen network interface
      auto dx = capture(dev);

      {
        std::scoped_lock lock{mac_mutex};

        // Add devices to the list
        for (auto [mac, device] : dx) {
          devices[mac].packets += device.packets;
          devices[mac].ip = device.ip;
          devices[mac].packet_type = device.packet_type;
        }
      }
    }

    std::println("Sniffer stopped");
  });

  // Report devices seen and packet count
  threads.emplace_back([&]() {
    while (run) {

      // Clear terminal
      std::print("\033[2J\033[1;1H");

      // Print current time
      auto now = std::chrono::system_clock::now();
      auto now_c = std::chrono::system_clock::to_time_t(now);
      std::println("{}", std::ctime(&now_c));

      // Print markdown table header
      std::println("| MAC | IP | Type | Packets | Vendor |");
      std::println("|-|-|-|-|-|");

      // Grab devices and print summary
      {
        std::scoped_lock lock{mac_mutex};
        for (auto [mac, device] : devices) {
          auto vendor = oui::lookup(mac);
          std::println("| {} | {:15} | {:04x} | {:6} | {:30} |", mac.substr(0, 8),
                       device.ip, device.packet_type, device.packets, vendor);
        }
      }

      std::this_thread::sleep_for(1s);
    }

    std::println("Reporter stopped");
  });

  // Wait for a while
  std::this_thread::sleep_for(60s);

  // Request all threads stop
  run = false;

  // Wait for the threads to finish
  std::ranges::all_of(threads, [](auto &t) {
    if (t.joinable())
      t.join();

    return true;
  });
}