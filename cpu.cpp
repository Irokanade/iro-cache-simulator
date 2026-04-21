#include "cpu.h"
#include <cstdint>
#include <cstdlib>
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

static void flush() {}

static void bus_read(CPU *cpu, uint8_t core_id, uint64_t address) {}
static void bus_read_exclusive(CPU *cpu, uint8_t core_id, uint64_t address) {}
static void bus_upgrade(CPU *cpu, uint8_t core_id, uint64_t address) {}

static void processor_read(CPU *cpu, uint8_t core_id, uint64_t address)
{
    uint8_t l1_set_index = static_cast<uint8_t>(l1_to_index(address));
    uint64_t l1_set_tag = l1_to_tag(address);

    Core *core = &cpu->cores[core_id];
    L1SetMeta *l1_set_meta = &core->l1d_metas[l1_set_index];
    L1SetData *l1_set_data = &core->l1d_datas[l1_set_index];

    uint8_t l1_set_way;
    if (l1_find_way(l1_set_meta, l1_set_tag, &l1_set_way)) {
        switch (l1_set_meta->state[l1_set_way]) {
        case MESIState::MODIFIED:
        case MESIState::EXCLUSIVE:
        case MESIState::SHARED:
            return;
        case MESIState::INVALID:
            std::unreachable();
        }
    }

    bus_read(cpu, core_id, address);
}

static void processor_write(CPU *cpu, uint8_t core_id, uint64_t address)
{
    uint8_t l1_set_index = static_cast<uint8_t>(l1_to_index(address));
    uint64_t l1_set_tag = l1_to_tag(address);

    Core *core = &cpu->cores[core_id];
    L1SetMeta *l1_set_meta = &core->l1d_metas[l1_set_index];
    L1SetData *l1_set_data = &core->l1d_datas[l1_set_index];

    uint8_t l1_set_way;
    if (l1_find_way(l1_set_meta, l1_set_tag, &l1_set_way)) {
        switch (l1_set_meta->state[l1_set_way]) {
        case MESIState::MODIFIED:
            return;
        case MESIState::EXCLUSIVE:
            l1_set_meta->state[l1_set_way] = MESIState::MODIFIED;
            return;
        case MESIState::SHARED:
            bus_upgrade(cpu, core_id, address);
            return;
        case MESIState::INVALID:
            std::unreachable();
        }
    }

    bus_read_exclusive(cpu, core_id, address);
}