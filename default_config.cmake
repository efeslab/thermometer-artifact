# global settings
set(BLOCK_SIZE 64)
set(PAGE_SIZE 4096)
set(STAT_PRINTING_PERIOD 10000000)
set(CPU_FREQ 4000)
set(NUM_CPUS 1)

# ooo_cpu
set(IFETCH_BUFFER_SIZE 64)
set(DECODE_BUFFER_SIZE 32)
set(DISPATCH_BUFFER_SIZE 32)
set(ROB_SIZE 352)
set(LQ_SIZE 128)
set(SQ_SIZE 72)
set(FETCH_WIDTH 6)
set(DECODE_WIDTH 6)
set(DISPATCH_WIDTH 6)
set(EXEC_WIDTH 4)
set(LQ_WIDTH 2)
set(SQ_WIDTH 2)
set(RETIRE_WIDTH 5)
set(BRANCH_MISPREDICT_PENALTY 1)
set(SCHEDULER_SIZE 128)
set(DECODE_LATENCY 1)
set(DISPATCH_LATENCY 1)
set(SCHEDULING_LATENCY 0)
set(EXEC_LATENCY 0)

# DIB
set(DIB_WINDOW_SIZE 16)
set(DIB_SET 32)
set(DIB_WAY 8)

# L1I
set(L1I_SET 64)
set(L1I_WAY 8)
set(L1I_WQ_SIZE 64)
set(L1I_RQ_SIZE 64)
set(L1I_PQ_SIZE 32)
set(L1I_MSHR_SIZE 8)
set(L1I_HIT_LATENCY 3)
set(L1I_FILL_LATENCY 1)
set(L1I_MAX_READ 2)
set(L1I_MAX_WRITE 2)

# L1D
set(L1D_SET 64)
set(L1D_WAY 12)
set(L1D_WQ_SIZE 64)
set(L1D_RQ_SIZE 64)
set(L1D_PQ_SIZE 8)
set(L1D_MSHR_SIZE 16)
set(L1D_HIT_LATENCY 4)
set(L1D_FILL_LATENCY 1)
set(L1D_MAX_READ 2)
set(L1D_MAX_WRITE 2)

# L2C
set(L2C_SET 1024)
set(L2C_WAY 8)
set(L2C_WQ_SIZE 32)
set(L2C_RQ_SIZE 32)
set(L2C_PQ_SIZE 16)
set(L2C_MSHR_SIZE 32)
set(L2C_HIT_LATENCY 9)
set(L2C_FILL_LATENCY 1)
set(L2C_MAX_READ 1)
set(L2C_MAX_WRITE 1)

# ITLB
set(ITLB_SET 16)
set(ITLB_WAY 4)
set(ITLB_WQ_SIZE 16)
set(ITLB_RQ_SIZE 16)
set(ITLB_PQ_SIZE 0)
set(ITLB_MSHR_SIZE 8)
set(ITLB_HIT_LATENCY 0)
set(ITLB_FILL_LATENCY 1)
set(ITLB_MAX_READ 2)
set(ITLB_MAX_WRITE 2)

# DTLB
set(DTLB_SET 16)
set(DTLB_WAY 4)
set(DTLB_WQ_SIZE 16)
set(DTLB_RQ_SIZE 16)
set(DTLB_PQ_SIZE 0)
set(DTLB_MSHR_SIZE 8)
set(DTLB_HIT_LATENCY 0)
set(DTLB_FILL_LATENCY 1)
set(DTLB_MAX_READ 2)
set(DTLB_MAX_WRITE 2)

# STLB
set(STLB_SET 128)
set(STLB_WAY 12)
set(STLB_WQ_SIZE 32)
set(STLB_RQ_SIZE 32)
set(STLB_PQ_SIZE 0)
set(STLB_MSHR_SIZE 16)
set(STLB_HIT_LATENCY 7)
set(STLB_FILL_LATENCY 1)
set(STLB_MAX_READ 1)
set(STLB_MAX_WRITE 1)

# LLC
set(LLC_SET 2048)
set(LLC_WAY 16)
set(LLC_WQ_SIZE 32)
set(LLC_RQ_SIZE 32)
set(LLC_PQ_SIZE 32)
set(LLC_MSHR_SIZE 64)
set(LLC_HIT_LATENCY 19)
set(LLC_FILL_LATENCY 1)
set(LLC_MAX_READ 1)
set(LLC_MAX_WRITE 1)

# physical memory
set(DRAM_IO_FREQ 3200)
set(DRAM_CHANNELS 1)
set(DRAM_RANKS 1)
set(DRAM_BANKS 8)
set(DRAM_ROWS 65536)
set(DRAM_COLUMNS 128)
set(DRAM_ROW_SIZE 8)
set(DRAM_CHANNEL_WIDTH 8)
set(DRAM_WQ_SIZE 64)
set(DRAM_RQ_SIZE 64)
set(tRP_DRAM_NANOSECONDS 12.5)
set(tRCD_DRAM_NANOSECONDS 12.5)
set(tCAS_DRAM_NANOSECONDS 12.5)

# virtual memory
set(VIRTUAL_MEMORY_SIZE 8589934592)
set(VIRTUAL_MEMORY_NUM_LEVELS 5)
