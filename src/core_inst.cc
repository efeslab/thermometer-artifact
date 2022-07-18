#include "cache.h"
#include "champsim.h"
#include "dram_controller.h"
#include "ooo_cpu.h"
#include "vmem.h"
#include "champsim_constants.h"
#include <vector>

CACHE LLC("LLC", LLC_SET, LLC_WAY, LLC_WQ_SIZE, LLC_RQ_SIZE, LLC_PQ_SIZE, LLC_MSHR_SIZE, LLC_HIT_LATENCY, LLC_FILL_LATENCY, LLC_MAX_READ, LLC_MAX_WRITE);
std::vector<O3_CPU> ooo_cpu {  };
MEMORY_CONTROLLER DRAM("DRAM");
VirtualMemory vmem(NUM_CPUS, VIRTUAL_MEMORY_SIZE, PAGE_SIZE, VIRTUAL_MEMORY_NUM_LEVELS, 1);
