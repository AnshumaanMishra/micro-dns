#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <string>

#include "../include/microdns/ring_buffer.hpp"
#include "../include/microdns/wire_protocol.hpp"

DnsQuery prepare_query(std::string& input_domain) {
  DnsQuery query{};

  std::ranges::transform(input_domain, input_domain.begin(),
                         [](unsigned char c) { return std::tolower(c); });

  // -1 to ensure at least one \0 stays
  size_t safe_len = std::min(input_domain.length(), query.domain.size() - 1);

  std::copy_n(input_domain.begin(), safe_len, query.domain.begin());

  query.domain_len = safe_len;
  query.request_id = 1;

  return query;
}

struct ShmLayout {
  LockFreeRingBuffer<DnsQuery, 256> query_ring;
};

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cout << "Error: Too few arguments passed" << std::endl;
    return 1;
  }

  std::string input_domain = argv[1];

  // Only readwrite since it should already have been created
  int fd = shm_open("/micro_dns_shm", O_RDWR, 0666);
  if (fd == -1) {
    std::cout << "Error: File Not Created" << std::endl;
    return 2;
  }

  int shm_size = sizeof(ShmLayout);

  void* raw_ptr = mmap(nullptr, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

  close(fd);

  ShmLayout* shm = static_cast<ShmLayout*>(raw_ptr);

  DnsQuery query = prepare_query(input_domain);

  while (!shm->query_ring.push(query)) {
    // This empty loop is to trap the thread until the push is complete
  }

  munmap(shm, shm_size);
}
