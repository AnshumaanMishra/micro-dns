#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>  // Required for fstat
#include <unistd.h>

#include <iostream>

#include "../include/microdns/ring_buffer.hpp"
#include "../include/microdns/storage.hpp"
#include "../include/microdns/wire_protocol.hpp"

struct ShmLayout {
  LockFreeRingBuffer<DnsQuery, 256> query_ring;
};

const size_t DB_CAPACITY = 100 * 1024 * 1024;

int main() {
  // Since this daemon willcreate the shared memory as well, we need to use
  // O_CREAT
  int fd = shm_open("/micro_dns_shm", O_CREAT | O_RDWR, 0666);
  int shm_size = sizeof(ShmLayout);

  ftruncate(fd, shm_size);

  void* raw_ptr = mmap(nullptr, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);

  ShmLayout* shm = new (raw_ptr) ShmLayout();

  std::cout << "Micro DNS Server running, waiting for queries..." << std::endl;

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
    header->root_offset = 0;
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

  close(db_fd);

  std::cout << "Micro DNS Server running, waiting for queries..." << std::endl;

  DnsQuery query;

  while (1) {
    if (shm->query_ring.pop(query)) {
      std::cout << "Received Query: " << query.domain.data() << " (Len:" << query.domain_len << ")"
                << std::endl;
    }
  }
}
