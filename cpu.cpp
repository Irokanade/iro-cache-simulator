#include "cpu.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <utility>

static bool l1_find_way(const L1SetMeta *meta, uint64_t tag, uint8_t *way)
{
    for (uint8_t i = 0; i < NUM_L1_WAYS; i++) {
        if (meta->tag[i] == tag && meta->state[i] != INVALID) {
            *way = i;
            return true;
        }
    }

    return false;
}

static bool l2_find_way(const L2SetMeta *meta, uint64_t tag, uint8_t *way)
{
    for (uint8_t i = 0; i < NUM_L2_WAYS; i++) {
        if (meta->tag[i] == tag && meta->state[i] != INVALID) {
            *way = i;
            return true;
        }
    }
    return false;
}

static void flush() {}

static void bus_read(CPU *cpu, uint8_t core_id, uint64_t address)
{
    for (uint8_t i = 0; i < NUM_CORES; i++) {
        if (i == core_id) {
            continue;
        }

        Core *core = &cpu->cores[i];
    }
}

static void bus_read_exclusive(CPU *cpu, uint8_t core_id, uint64_t address) {}
static void bus_upgrade(CPU *cpu, uint8_t core_id, uint64_t address) {}

static void processor_read(CPU *cpu, uint8_t core_id, uint64_t address,
                           uint8_t *data, uint8_t data_size)
{
    uint16_t l1_set_index = l1_to_index(address);
    uint64_t l1_set_tag = l1_to_tag(address);

    Core *core = &cpu->cores[core_id];
    L1SetMeta *l1_set_meta = &core->l1d_metas[l1_set_index];
    L1SetData *l1_set_data = &core->l1d_datas[l1_set_index];

    uint8_t l1_set_way;
    if (l1_find_way(l1_set_meta, l1_set_tag, &l1_set_way)) {
        std::memcpy(data, l1_set_data->data[l1_set_way] + l1_to_offset(address),
                    data_size);
        return;
    }

    uint16_t l2_set_index = l2_to_index(address);
    uint64_t l2_set_tag = l2_to_tag(address);

    L2SetMeta *l2_set_meta = &core->l2_metas[l2_set_index];
    L2SetData *l2_set_data = &core->l2_datas[l2_set_index];

    uint8_t l2_set_way;
    if (l2_find_way(l2_set_meta, l2_set_tag, &l2_set_way)) {
        // evict l1 then fill l1 from l2
        return;
    }

    bus_read(cpu, core_id, address);
}

static void processor_write(CPU *cpu, uint8_t core_id, uint64_t address,
                            uint8_t *data, uint8_t data_size)
{
    uint16_t l1_set_index = l1_to_index(address);
    uint64_t l1_set_tag = l1_to_tag(address);

    Core *core = &cpu->cores[core_id];
    L1SetMeta *l1_set_meta = &core->l1d_metas[l1_set_index];
    L1SetData *l1_set_data = &core->l1d_datas[l1_set_index];

    uint8_t l1_set_way;
    if (l1_find_way(l1_set_meta, l1_set_tag, &l1_set_way)) {
        switch (l1_set_meta->state[l1_set_way]) {
        case MESIState::MODIFIED:
            break;
        case MESIState::EXCLUSIVE:
            l1_set_meta->state[l1_set_way] = MESIState::MODIFIED;
            break;
        case MESIState::SHARED:
            bus_upgrade(cpu, core_id, address);
            l1_set_meta->state[l1_set_way] = MESIState::MODIFIED;
            break;
        case MESIState::INVALID:
            std::unreachable();
        }

        std::memcpy(l1_set_data->data[l1_set_way] + l1_to_offset(address), data,
                    data_size);
        return;
    }

    uint16_t l2_set_index = l2_to_index(address);
    uint64_t l2_set_tag = l2_to_tag(address);

    L2SetMeta *l2_set_meta = &core->l2_metas[l2_set_index];
    L2SetData *l2_set_data = &core->l2_datas[l2_set_index];

    uint8_t l2_set_way;
    if (l2_find_way(l2_set_meta, l2_set_tag, &l2_set_way)) {
        switch (l2_set_meta->state[l2_set_way]) {
        case MESIState::MODIFIED:
            break;
        case MESIState::EXCLUSIVE:
            l2_set_meta->state[l2_set_way] = MESIState::MODIFIED;
            break;
        case MESIState::SHARED:
            bus_upgrade(cpu, core_id, address);
            l2_set_meta->state[l2_set_way] = MESIState::MODIFIED;
            break;
        case MESIState::INVALID:
            std::unreachable();
        }

        // evict l1 then fill l1 from l2
        return;
    }

    bus_read_exclusive(cpu, core_id, address);
}