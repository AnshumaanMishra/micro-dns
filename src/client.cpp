#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "../include/microdns/ring_buffer.hpp"
#include "../include/microdns/wire_protocol.hpp"

struct ShmLayout {
  LockFreeRingBuffer<DnsQuery, 256> query_ring;
  LockFreeRingBuffer<uint32_t, 256> response_ring;
};

const int NUM_QUERIES = 1'000'000;

int main() {
  int fd = shm_open("/micro_dns_shm", O_RDWR, 0666);
  if (fd == -1)
    return 1;
  void* raw_ptr = mmap(nullptr, sizeof(ShmLayout), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  ShmLayout* shm = reinterpret_cast<ShmLayout*>(raw_ptr);

  std::string target_domain = "api.google.com";
  DnsQuery query{};
  query.domain_len = target_domain.length();
  std::copy_n(target_domain.begin(), query.domain_len, query.domain.begin());

  std::vector<long long> latencies;
  latencies.reserve(NUM_QUERIES);

  std::cout << "Starting 1 Million Query Benchmark..." << std::endl;

  for (int i = 0; i < NUM_QUERIES; i++) {
    auto start = std::chrono::high_resolution_clock::now();

    while (!shm->query_ring.push(query)) {
      // Occupied until it can push
    }

    uint32_t response_ip;
    while (!shm->response_ring.pop(response_ip)) {
      // Keep it occupied
    }

    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    latencies.push_back(duration);
  }

  std::sort(latencies.begin(), latencies.end());
  std::cout << "p50: " << latencies[NUM_QUERIES / 2] << std::endl;
  std::cout << "p99: " << latencies[(NUM_QUERIES * 99) / 100] << std::endl;

  return 0;
}