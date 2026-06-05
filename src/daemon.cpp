#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <iostream>

#include "../include/microdns/ring_buffer.hpp"
#include "../include/microdns/storage.hpp"
#include "../include/microdns/wire_protocol.hpp"

struct ShmLayout {
  LockFreeRingBuffer<DnsQuery, 256> query_ring;
  LockFreeRingBuffer<uint32_t, 256> response_ring;
};

int main() {
  int fd = shm_open("/micro_dns_shm", O_CREAT | O_RDWR, 0666);
  int shm_size = sizeof(ShmLayout);
  ftruncate(fd, shm_size);
  void* raw_ptr = mmap(nullptr, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  ShmLayout* shm = new (raw_ptr) ShmLayout();

  int db_fd = open("micro_dns.db", O_CREAT | O_RDWR, 0666);
  if (db_fd == -1) {
    std::cout << "Error Opening File" << std::endl;
    return 1;
  }

  struct stat file_stat;
  fstat(db_fd, &file_stat);

  void* db_ptr = nullptr;
  DbHeader* header = nullptr;

  if (file_stat.st_size == 0) {
    std::cout << "Creating new database file..." << std::endl;
    ftruncate(db_fd, DB_CAPACITY);
    db_ptr = mmap(nullptr, DB_CAPACITY, PROT_READ | PROT_WRITE, MAP_SHARED, db_fd, 0);

    header = new (db_ptr) DbHeader();
    header->magic = 0x444E5321;
    header->root_offsets.fill(NULL_OFFSET);
    header->active_idx = 0;
    header->version = 1;
    header->allocator_state = sizeof(DbHeader);
  } else {
    std::cout << "Found Existing Database! Recovering..." << std::endl;
    db_ptr = mmap(nullptr, file_stat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, db_fd, 0);
    header = static_cast<DbHeader*>(db_ptr);

    if (header->magic != 0x444E5321) {
      std::cout << "Corrupted Database!" << std::endl;
      return 2;
    }
  }

  std::cout << "Micro DNS Server running, waiting for queries..." << std::endl;
  DnsQuery query;

  // Only For Testing
  std::string test_domain = "api.google.com";
  write_domain(header, db_ptr, test_domain, 123456789);
  // Testing code ends here

  while (1) {
    if (shm->query_ring.pop(query)) {
      std::string domain_str(query.domain.data(), query.domain_len);
      uint32_t resolved_ip = lookup_domain(header, db_ptr, domain_str);

      while (!shm->response_ring.push(resolved_ip)) {
#if defined(__aarch64__) || defined(__APPLE__)
        asm volatile("yield" ::: "memory");
#else
        asm volatile("pause" ::: "memory");
#endif
      }
    } else {
#if defined(__aarch64__) || defined(__APPLE__)
      asm volatile("yield" ::: "memory");
#else
      asm volatile("pause" ::: "memory");
#endif
    }
  }
}