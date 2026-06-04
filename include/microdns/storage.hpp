#pragma once
#include <array>
#include <cstdint>
#include <type_traits>

struct alignas(8) DbHeader {
  uint32_t magic;            // E.g., 0x444E5321 (Spells "DNS!")
  uint32_t version;          // E.g., 1
  uint32_t root_offset;      // Where the first tree node lives (0 if tree is empty)
  uint32_t allocator_state;  // The next free byte offset in the file
};

static_assert(std::is_trivially_copyable_v<DbHeader>, "DbHeader must be flat");

struct alignas(8) Node {
  std::array<uint32_t, 256> children;  // To allow direct lookups of the type children['a']
  uint32_t ipv4;
};
