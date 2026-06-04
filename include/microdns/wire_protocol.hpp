#pragma once
#include <array>
#include <cstdint>
#include <type_traits>

// Force 8 byte alignment
struct alignas(8) DnsQuery {
  uint32_t request_id;
  uint16_t domain_len;
  // Global rules state max length for a domain is 253 characters + null character
  std::array<char, 254> domain{'\0'};
  // Flat Array
  // Because string allocates a pointer on the heap
  // The pointer will point to something else in the virtual
  // memory of the client leading to garbage values
};

// static_assert is executed at compile time, if false, it gives a compile time error
static_assert(std::is_trivially_copyable_v<DnsQuery>, "DNS Query must be trivially copyable");
static_assert(sizeof(DnsQuery) <= 264, "DNS Query exceeds size limit");

struct alignas(8) DnsResponse {
  uint32_t request_id;
  uint8_t resolved;  // 1 if found,0 if not
  uint8_t _pad[3];   // Explicit padding to maintain 8 byte alignment
  uint32_t ipv4;
};

static_assert(std::is_trivially_copyable_v<DnsResponse>, "DNS Response must be trivially copyable");
