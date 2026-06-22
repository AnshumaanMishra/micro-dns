#pragma once
#include <unistd.h>

#include <algorithm>
#ifdef __APPLE__
#include <fcntl.h>
#endif

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <type_traits>

const size_t DB_CAPACITY = 100 * 1024 * 1024;

struct alignas(8) DbHeader {
  uint32_t magic;  // 0x444E5321 (Spells "DNS!")
  uint32_t version;

  // Now for double buffering we need 2 offsets at the same time
  // and maintain an active tree index
  std::array<uint32_t, 2> root_offsets;
  uint32_t active_idx;

  uint32_t allocator_state;  // The next free byte offset in the file
};

static_assert(std::is_trivially_copyable_v<DbHeader>, "DbHeader must be flat");

inline size_t char_to_idx(char c) {
  if (c >= 'a' && c <= 'z') {
    return c - 'a';  // Maps to 0 - 25
  } else if (c >= '0' && c <= '9') {
    return c - '0' + 26;  // Maps to 26 - 35
  } else if (c == '-') {
    return 36;
  } else if (c == '.') {
    return 37;
  }
  return 255;  // Invalid Character
}

struct alignas(8) Node {
  std::array<uint32_t, 38> children;
  uint32_t ipv4;

  // Patricia compression
  std::array<char, 64> edge_label;  // The compressed string path
  uint8_t edge_len;                 // How many characters are in the label
  bool is_terminal;                 // Is this node the end of a domain
};

inline uint32_t allocate_node(DbHeader* header, size_t max_capacity) {
  uint32_t current_offset = header->allocator_state;
  uint32_t next_offset = current_offset + sizeof(Node);

  if (next_offset > max_capacity) {
    std::cerr << "CRITICAL: Database storage engine out of memory!!" << std::endl;
    return 0;
  }

  header->allocator_state = next_offset;

  return current_offset;
}

const uint32_t NULL_OFFSET = UINT32_MAX;

inline uint32_t lookup_domain(DbHeader* header, void* db_ptr, const std::string& query) {
  std::atomic_ref<uint32_t> atomic_active(header->active_idx);
  uint32_t safe_idx = atomic_active.load(std::memory_order_acquire);

  uint32_t current_offset = header->root_offsets[safe_idx];
  size_t query_idx = 0;

  if (current_offset == NULL_OFFSET or current_offset == 0) {
    return 0;
  }

  while (1) {
    Node* current_node = reinterpret_cast<Node*>(static_cast<char*>(db_ptr) + current_offset);

    if (query_idx + current_node->edge_len > query.length()) {
      return 0;
    }

    for (int i = 0; i < current_node->edge_len; i++) {
      if (current_node->edge_label[i] != query[i + query_idx]) {
        return 0;
      }
    }
    query_idx += current_node->edge_len;

    if (query_idx == query.length()) {
      return current_node->ipv4;
    }

    char next_char = query[query_idx];
    size_t child_idx = char_to_idx(next_char);

    if (child_idx == 255) {
      return 0;
    }

    if (current_node->children[child_idx] == NULL_OFFSET) {
      return 0;
    }

    current_offset = current_node->children[child_idx];
  }
}

// Write Ahead Ledger

// Enum to make the 'op' field readable
enum class WalOp : uint8_t {
  INSERT = 1,
};

struct alignas(8) WalRecord {
  uint64_t lsn;  // The log sequence number
  uint8_t op;    // Operation Type
  uint8_t padding_[3];
  uint32_t ipv4;  // The payload
  uint16_t domain_len;
  std::array<char, 254> domain;
  uint32_t crc32;  // safety fingerprint
};

static_assert(std::is_trivially_copyable_v<WalRecord>, "WalRecord must be flat");
static_assert(sizeof(WalRecord) % 8 == 0, "WalRecord size must be aligned to 8 bytes");

extern uint32_t calculate_crc32(const void* data, size_t length);

inline bool append_wal(int wal_fd, uint64_t current_lsn, const std::string& domain, uint32_t ipv4) {
  WalRecord record{};
  record.lsn = current_lsn;
  record.op = static_cast<uint8_t>(WalOp::INSERT);
  record.ipv4 = ipv4;

  record.domain_len = domain.length();
  std::copy_n(domain.begin(), record.domain_len, record.domain.begin());

  size_t crc_length = offsetof(WalRecord, crc32);
  record.crc32 = calculate_crc32(&record, crc_length);

  if (write(wal_fd, &record, sizeof(WalRecord)) != sizeof(WalRecord)) {
    return false;
  }
#ifdef __APPLE__
  if (fcntl(wal_fd, F_FULLFSYNC) == -1) {
    return false;
  }
#else
  if (fdatasync(wal_fd) == -1) {
    return false;
  }
#endif

  return true;
}

inline void insert_domain(DbHeader* header, void* db_ptr, uint32_t& target_root,
                          const std::string& query, uint32_t ipv4) {
  // Empty tree case:
  if (target_root == 0 or target_root == NULL_OFFSET) {
    uint32_t new_root_offset = allocate_node(header, DB_CAPACITY);
    if (new_root_offset == 0) {
      return;  // Out of memory
    }

    Node* root = reinterpret_cast<Node*>(static_cast<char*>(db_ptr) + new_root_offset);
    root->children.fill(NULL_OFFSET);
    root->ipv4 = ipv4;
    root->is_terminal = true;
    root->edge_len = query.length();
    std::copy_n(query.begin(), query.length(), root->edge_label.begin());

    target_root = new_root_offset;
    return;
  }

  uint32_t current_offset = target_root;
  size_t query_idx = 0;

  while (true) {
    Node* current_node = reinterpret_cast<Node*>(static_cast<char*>(db_ptr) + current_offset);

    uint8_t match_len = 0;

    while (match_len < current_node->edge_len and (query_idx + match_len) < query.length() and
           query[query_idx + match_len] == current_node->edge_label[match_len]) {
      match_len++;
    }

    if (match_len < current_node->edge_len) {
      uint32_t old_child_offset = allocate_node(header, DB_CAPACITY);
      if (old_child_offset == 0)
        return;
      Node* old_child = reinterpret_cast<Node*>(static_cast<char*>(db_ptr) + old_child_offset);

      old_child->ipv4 = current_node->ipv4;
      old_child->is_terminal = current_node->is_terminal;
      std::copy(current_node->children.begin(), current_node->children.end(),
                old_child->children.begin());

      old_child->edge_len = current_node->edge_len - match_len;
      std::copy(current_node->edge_label.begin() + match_len, current_node->edge_label.end(),
                old_child->edge_label.begin());

      current_node->edge_len = match_len;

      current_node->children.fill(NULL_OFFSET);
      current_node->ipv4 = 0;
      current_node->is_terminal = false;

      char old_char = old_child->edge_label[0];
      current_node->children[char_to_idx(old_char)] = old_child_offset;

      uint32_t remaining_query_len = query.length() - (query_idx + match_len);

      if (remaining_query_len == 0) {
        current_node->ipv4 = ipv4;
        current_node->is_terminal = true;
      } else {
        uint32_t new_child_offset = allocate_node(header, DB_CAPACITY);
        if (new_child_offset == 0)
          return;
        Node* new_child = reinterpret_cast<Node*>(static_cast<char*>(db_ptr) + new_child_offset);

        new_child->children.fill(NULL_OFFSET);
        new_child->ipv4 = ipv4;
        new_child->edge_len = remaining_query_len;
        new_child->is_terminal = true;

        std::copy_n(query.begin() + query_idx + match_len, remaining_query_len,
                    new_child->edge_label.begin());

        char new_char = new_child->edge_label[0];
        current_node->children[char_to_idx(new_char)] = new_child_offset;
      }

      return;
    } else if (query_idx + match_len == query.length()) {
      current_node->ipv4 = ipv4;
      current_node->is_terminal = true;
      return;
    } else {
      query_idx += match_len;
      char next_char = query[query_idx];
      size_t child_idx = char_to_idx(next_char);

      if (child_idx == 255) {
        return;
      }

      if (current_node->children[child_idx] == NULL_OFFSET) {
        uint32_t new_offset = allocate_node(header, DB_CAPACITY);
        if (new_offset == 0)
          return;  // Out of memory

        Node* new_node = reinterpret_cast<Node*>(static_cast<char*>(db_ptr) + new_offset);

        new_node->children.fill(NULL_OFFSET);

        new_node->ipv4 = ipv4;
        new_node->is_terminal = true;

        new_node->edge_len = query.length() - query_idx;
        std::copy_n(query.begin() + query_idx, new_node->edge_len, new_node->edge_label.begin());
        current_node->children[child_idx] = new_offset;
        return;
      } else {
        current_offset = current_node->children[child_idx];
      }
    }
  }
}

inline uint32_t deep_copy_tree(DbHeader* header, void* db_ptr, uint32_t source_offset) {
  if (source_offset == NULL_OFFSET or source_offset == 0) {
    return 0;
  }

  Node* source_node = reinterpret_cast<Node*>(static_cast<char*>(db_ptr) + source_offset);

  uint32_t dest_offset = allocate_node(header, DB_CAPACITY);
  if (dest_offset == 0) {
    return 0;  // Out of memory
  }

  Node* dest_node = reinterpret_cast<Node*>(static_cast<char*>(db_ptr) + dest_offset);

  dest_node->ipv4 = source_node->ipv4;
  dest_node->is_terminal = source_node->is_terminal;
  dest_node->edge_len = source_node->edge_len;
  std::copy_n(source_node->edge_label.begin(), source_node->edge_len,
              dest_node->edge_label.begin());
  dest_node->children.fill(NULL_OFFSET);

  for (int i = 0; i < 38; i++) {
    if (source_node->children[i] != NULL_OFFSET) {
      dest_node->children[i] = deep_copy_tree(header, db_ptr, source_node->children[i]);
    }
  }

  return dest_offset;
}

inline void write_domain(DbHeader* header, void* db_ptr, const std::string& query, uint32_t ipv4) {
  std::atomic_ref<uint32_t> atomic_active(header->active_idx);

  uint32_t old_idx = atomic_active.load(std::memory_order_relaxed);
  uint32_t new_idx = 1 - old_idx;

  header->root_offsets[new_idx] = deep_copy_tree(header, db_ptr, header->root_offsets[old_idx]);

  insert_domain(header, db_ptr, header->root_offsets[new_idx], query, ipv4);

  atomic_active.store(new_idx, std::memory_order_release);
}

// For Debugging
inline void print_tree(void* db_ptr, uint32_t offset, int depth = 0) {
  if (offset == NULL_OFFSET || offset == 0)
    return;

  Node* node = reinterpret_cast<Node*>(static_cast<char*>(db_ptr) + offset);

  std::string indent(depth * 2, ' ');
  std::string label(node->edge_label.begin(), node->edge_label.begin() + node->edge_len);

  std::cout << indent << "Node " << offset << " [" << label << "] (IP: " << node->ipv4 << ")"
            << std::endl;

  for (uint32_t child_offset : node->children) {
    if (child_offset != NULL_OFFSET) {
      print_tree(db_ptr, child_offset, depth + 1);
    }
  }
}
