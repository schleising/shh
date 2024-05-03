#include "oui.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <ranges>

// Anonymous
namespace {

// Remove any non-hex characters from a MAC address or vendor
auto strip(std::string_view mac) {
  auto key = std::string{mac};
  key.erase(std::remove_if(key.begin(), key.end(),
                           [](char c) { return !std::isxdigit(c); }),
            key.end());
  std::transform(key.begin(), key.end(), key.begin(), ::tolower);
  return key;
}

// Remove any non-printable characters from a string
auto sanitise(std::string_view str) {
  auto key = std::string{str};
  key.erase(std::remove_if(key.begin(), key.end(),
                           [](char c) { return !std::isprint(c); }),
            key.end());
  return key;
}

// Tidy up a MAC address into just the vendor part
auto mac_to_vendor(const std::string_view mac) {
  auto key = strip(mac);
  return key.substr(0, 6);
}

// Create the OUI database from a text file
std::map<std::string, std::string> get_oui() {

  auto in = std::ifstream{"oui.txt"};

  // Read whole file into a string
  auto str = std::string{std::istreambuf_iterator<char>{in},
                         std::istreambuf_iterator<char>{}};

  // Parse each line
  auto oui = std::map<std::string, std::string>{
      {"f2:ed:07", "Nothing Technology Limited"},
  };

  for (auto line : str | std::views::split('\n')) {

    // Look for all the lines with the (hex) string
    auto s = std::string{line.begin(), line.end()};
    if (not s.contains("(hex)") or s.empty())
      continue;

    auto pos = s.find("(hex)");

    if (pos != std::string::npos) {

      auto key = s.substr(0, 8);
      auto value = s.substr(pos + 6);

      // Remove leading and trailing spaces
      auto vendor = strip(key);

      oui[vendor] = value;
    }
  }

  return oui;
}

// Initialise the database on startup
const auto database = get_oui();
} // namespace

// Lookup a MAC address in the database
namespace oui {
std::string lookup(const std::string_view mac) {

  // Create key from MAC address
  auto vendor = mac_to_vendor(mac);

  // Search for the vendor in the database
  auto it = database.find(vendor);
  auto details = it != database.end() ? it->second : "";

  // Return the cleaned up vendor details if found, or nothing. NOTHING
  return sanitise(details);
}

// Pretty print the MAC address
std::string prettify(const std::string_view dirty) {

  // Tidy up the incoming MAC address
  auto clean = strip(sanitise(dirty));

  // Create a pretty version of the MAC address
  auto pretty = std::string{};

  // Insert colons every two characters
  for (auto x : clean | std::views::split(2)) {
    pretty += std::string{x.begin(), x.end()};
    pretty += ":";
  }

  // auto blah = clean | std::views::split(2);
  // auto blah2 = std::ranges::join_view{blah, ":"};

  // Remove the trailing colon
  pretty.pop_back();

  return pretty;
}
} // namespace oui