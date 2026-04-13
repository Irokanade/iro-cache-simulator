#include "cache_debugger.h"

static void l1d_take_snapshot(CPU *cpu, uint8_t core_id, uint16_t l1_set_index,
                              L1Snapshot *snapshot)
{
    L1SetMeta *meta = &cpu->cores[core_id].l1d_metas[l1_set_index];
    snapshot->plru_bits = meta->plru_bits;
    for (uint8_t i = 0; i < NUM_L1_WAYS; i++) {
        snapshot->tag[i] = meta->tag[i];
        snapshot->state[i] = meta->state[i];
    }
}

static void l1i_take_snapshot(CPU *cpu, uint8_t core_id, uint16_t l1_set_index,
                              L1Snapshot *snapshot)
{
    L1SetMeta *meta = &cpu->cores[core_id].l1i_metas[l1_set_index];
    snapshot->plru_bits = meta->plru_bits;
    for (uint8_t i = 0; i < NUM_L1_WAYS; i++) {
        snapshot->tag[i] = meta->tag[i];
        snapshot->state[i] = meta->state[i];
    }
}

static void l2_take_snapshot(CPU *cpu, uint16_t l2_set_index,
                             L2Snapshot *snapshot)
{
    L2SetMeta *meta = &cpu->l2_metas[l2_set_index];
    snapshot->plru_bits = meta->plru_bits;
    for (uint8_t i = 0; i < NUM_L2_WAYS; i++) {
        snapshot->tag[i] = meta->tag[i];
        snapshot->state[i] = meta->state[i];
        snapshot->core_valid_d[i] = meta->core_valid_d[i];
        snapshot->core_valid_i[i] = meta->core_valid_i[i];
    }
}

template <Operand OP>
static void record_trace(CacheDebugger *cd, uint8_t core_id, uint64_t address,
                         uint8_t *data, uint8_t size)
{
    TraceEntry e;
    e.op = OP;
    e.core_id = core_id;
    e.address = address;

    uint16_t l1_set = l1_to_index(address);
    uint16_t l2_set = l2_to_index(address);

    if constexpr (OP == READ) {
        cpu_read(cd->cpu, core_id, address, data, size);
    } else if constexpr (OP == WRITE) {
        cpu_write(cd->cpu, core_id, address, data, size);
    } else {
        cpu_fetch(cd->cpu, core_id, address, data, size);
    }

    if constexpr (OP == FETCH) {
        l1i_take_snapshot(cd->cpu, core_id, l1_set, &e.l1);
    } else {
        l1d_take_snapshot(cd->cpu, core_id, l1_set, &e.l1);
    }
    l2_take_snapshot(cd->cpu, l2_set, &e.l2);

    cd->trace.push_back(e);
}

void cache_debugger_init(CacheDebugger *cd, CPU *cpu)
{
    cd->cpu = cpu;
    cd->trace.clear();
}

void cache_debugger_read(CacheDebugger *cd, uint8_t core_id, uint64_t address,
                         uint8_t *data, uint8_t size)
{
    record_trace<READ>(cd, core_id, address, data, size);
}

void cache_debugger_write(CacheDebugger *cd, uint8_t core_id, uint64_t address,
                          uint8_t *data, uint8_t size)
{
    record_trace<WRITE>(cd, core_id, address, data, size);
}

void cache_debugger_fetch(CacheDebugger *cd, uint8_t core_id, uint64_t address,
                          uint8_t *data, uint8_t size)
{
    record_trace<FETCH>(cd, core_id, address, data, size);
}
