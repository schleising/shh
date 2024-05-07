#include "packet.h"
#include "types.h"
#include <format>

namespace cap {

// Only constructor allowed
packet_t::packet_t(std::string_view interface) {

  interface_ = interface;

  char errbuf[256];
  pcap_ =
      pcap_open_live(std::string{interface}.c_str(), 65535, 1, 1000, errbuf);
}

// RAII destructor
packet_t::~packet_t() {
  if (pcap_ != nullptr) {
    pcap_close(pcap_);
    pcap_ = nullptr;
  }
}

// Read a single packet from the interface
ethernet_packet_t packet_t::read() {
  pcap_pkthdr header;
  const u_char *data = pcap_next(pcap_, &header);

  if (data == nullptr)
    return {};

  // Structure of the first part of the packet
  struct ethernet_header_t {
    uint8_t destination_mac_[6];
    uint8_t source_mac_[6];
    uint16_t packet_type_;
  };

  static_assert(sizeof(ethernet_header_t) == 14);

  // Map these data into the header structure
  auto eth = reinterpret_cast<const ethernet_header_t *>(data);

  // Extract the MAC addresses
  auto source_mac = std::format("{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
                                eth->source_mac_[0], eth->source_mac_[1],
                                eth->source_mac_[2], eth->source_mac_[3],
                                eth->source_mac_[4], eth->source_mac_[5]);

  auto destination_mac =
      std::format("{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
                  eth->destination_mac_[0], eth->destination_mac_[1],
                  eth->destination_mac_[2], eth->destination_mac_[3],
                  eth->destination_mac_[4], eth->destination_mac_[5]);

  auto source_ip = std::string{};
  auto destination_ip = std::string{};

  // Get the IPs if it's an IPv4 packet
  if (eth->packet_type_ == 0x0008) {

    // Map the IPv4 structure onto these data
    auto ip =
        reinterpret_cast<const ip_header_t *>(data + sizeof(ethernet_header_t));

    // Extract from IP
    source_ip = std::format("{}.{}.{}.{}", ip->source_ip_[0], ip->source_ip_[1],
                            ip->source_ip_[2], ip->source_ip_[3]);

    // Extract to IP
    destination_ip =
        std::format("{}.{}.{}.{}", ip->dest_ip_[0], ip->dest_ip_[1],
                    ip->dest_ip_[2], ip->dest_ip_[3]);
  }

  // If it's an RTP packet, extract the payload type
  auto info = std::string{};
  if (eth->packet_type_ == 0x0089) {
    auto rtp = reinterpret_cast<const uint8_t *>(
        data + sizeof(ethernet_header_t) + 12);
    auto payload_type = *rtp & 0x7f;
    info = std::format("RTP payload type: {}", payload_type);
  }

  return {
      .interface_ = interface_,
      .info = info,
      .source_ = {.mac_ = source_mac, .ip_ = source_ip},
      .destination_ = {.mac_ = destination_mac, .ip_ = destination_ip},
      .type_ = eth->packet_type_,
  };
}

// Make assertions about the class
static_assert(not std::is_default_constructible_v<packet_t>);
static_assert(not std::is_copy_constructible_v<packet_t>);
static_assert(not std::is_copy_assignable_v<packet_t>);
static_assert(not std::is_move_constructible_v<packet_t>);
static_assert(not std::is_move_assignable_v<packet_t>);
static_assert(std::is_destructible_v<packet_t>);
static_assert(std::is_constructible_v<packet_t, std::string_view>);
static_assert(not std::has_virtual_destructor_v<packet_t>);

// List all network interfaces
std::set<std::string> interfaces() {

  std::set<std::string> network_interfaces{};

  // Find all network interfaces
  pcap_if_t *alldevs;
  char errbuf[256];
  if (pcap_findalldevs(&alldevs, errbuf) >= 0)
    for (pcap_if_t *d = alldevs; d != nullptr; d = d->next)
      network_interfaces.emplace(d->name);

  return network_interfaces;
}

} // namespace cap
