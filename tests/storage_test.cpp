#include "../include/microdns/storage.hpp"

#include <gtest/gtest.h>

TEST(StorageTest, BasicInsertionAndLookup) {
  void* db_mem = malloc(1024 * 1024);
  DbHeader* header = new (db_mem) DbHeader();
  header->root_offsets.fill(NULL_OFFSET);
  header->active_idx = 0;
  header->allocator_state = sizeof(DbHeader);

  std::string domain = "google.com";
  uint32_t ip = 123;

  insert_domain(header, db_mem, header->root_offsets[header->active_idx], domain, ip);

  uint32_t result = lookup_domain(header, db_mem, domain);
  EXPECT_EQ(result, ip);

  EXPECT_EQ(lookup_domain(header, db_mem, "unknown.com"), 0);

  free(db_mem);
}

TEST(StorageTest, DoubleBufferAtomicFlip) {
  void* db_mem = malloc(1024 * 1024);
  DbHeader* header = new (db_mem) DbHeader();
  header->root_offsets.fill(NULL_OFFSET);
  header->active_idx = 0;
  header->allocator_state = sizeof(DbHeader);
  write_domain(header, db_mem, "site.com", 100);
  write_domain(header, db_mem, "other.com", 200);

  // // Optional Tree Printing test
  // std::cout << "Tree: " << std::endl;
  // print_tree(db_mem, header->root_offsets[header->active_idx]);

  EXPECT_EQ(lookup_domain(header, db_mem, "site.com"), 100);
  EXPECT_EQ(lookup_domain(header, db_mem, "other.com"), 200);

  free(db_mem);
}