#include "cpu.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <utility>

template <typename T, uint8_t WAYS>
static void plru_update(T *plru_bits, uint8_t way)
{
    uint8_t node = (WAYS - 1) + way;
    while (node > 0) {
        uint8_t parent = (node - 1) / 2;
        T mask = static_cast<T>(1 << parent);
        *plru_bits =
            static_cast<T>((*plru_bits & ~mask) | ((node & 1) << parent));
        node = parent;
    }
}

template <typename T, uint8_t WAYS>
static uint8_t plru_victim(T plru_bits)
{
    uint8_t node = 0;
    while (node < WAYS - 1) {
        uint8_t bit = (plru_bits >> node) & 1;
        node = 2 * node + 1 + bit;
    }
    return node - (WAYS - 1);
}

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

static bool l3_find_way(const L3SetMeta *meta, uint64_t tag, uint8_t *way)
{
    for (uint8_t i = 0; i < NUM_L3_WAYS; i++) {
        if (meta->tag[i] == tag && meta->state[i] != INVALID) {
            *way = i;
            return true;
        }
    }

    return false;
}

static uint8_t l1d_evict(Core *core, uint16_t l1_index)
{
    L1SetMeta *l1_set_meta = &core->l1d_metas[l1_index];
    L1SetData *data = &core->l1d_datas[l1_index];

    // find an invalid way
    for (uint8_t i = 0; i < NUM_L1_WAYS; i++) {
        if (l1_set_meta->state[i] == MESIState::INVALID) {
            return i;
        }
    }

    // no invalid ways find victim
    uint8_t victim = plru_victim<uint8_t, NUM_L1_WAYS>(l1_set_meta->plru_bits);
    if (l1_set_meta->state[victim] == MESIState::MODIFIED) {
        uint64_t victim_addr = l1_to_addr(l1_set_meta->tag[victim], l1_index);
        uint16_t l2_index = l2_to_index(victim_addr);
        uint64_t l2_tag = l2_to_tag(victim_addr);

        L2SetMeta *l2_meta = &core->l2_metas[l2_index];
        L2SetData *l2_data = &core->l2_datas[l2_index];

        uint8_t l2_way;
        if (l2_find_way(l2_meta, l2_tag, &l2_way)) {
            std::memcpy(l2_data->data[l2_way], data->data[victim], LINE_SIZE);
            l2_meta->state[l2_way] = MESIState::MODIFIED;
        } else {
            // writeback policy ensures that l1 cacheline is always in l2
            std::unreachable();
        }
    }

    l1_set_meta->state[victim] = MESIState::INVALID;
    return victim;
}

static uint8_t l2_evict(CPU *cpu, uint8_t core_id, uint16_t l2_index)
{
    Core *core = &cpu->cores[core_id];
    L2SetMeta *l2_set_meta = &core->l2_metas[l2_index];
    L2SetData *l2_set_data = &core->l2_datas[l2_index];

    // find an invalid way
    for (uint8_t i = 0; i < NUM_L2_WAYS; i++) {
        if (l2_set_meta->state[i] == MESIState::INVALID) {
            return i;
        }
    }

    // no invalid ways find victim
    uint8_t victim = plru_victim<uint8_t, NUM_L2_WAYS>(l2_set_meta->plru_bits);

    // back-invalidate L1
    uint64_t victim_addr = l2_to_addr(l2_set_meta->tag[victim], l2_index);
    uint16_t l1_index = l1_to_index(victim_addr);
    uint64_t l1_tag = l1_to_tag(victim_addr);
    L1SetMeta *l1_meta = &core->l1d_metas[l1_index];
    L1SetData *l1_data = &core->l1d_datas[l1_index];

    // back-invalidate L1D
    uint8_t l1_way;
    if (l1_find_way(l1_meta, l1_tag, &l1_way)) {
        if (l1_meta->state[l1_way] == MESIState::MODIFIED) {
            std::memcpy(l2_set_data->data[victim], l1_data->data[l1_way],
                        LINE_SIZE);
            l2_set_meta->state[victim] = MESIState::MODIFIED;
        }
        l1_meta->state[l1_way] = MESIState::INVALID;
    }

    // back-invalidate L1I
    L1SetMeta *l1i_meta = &core->l1i_metas[l1_index];
    uint8_t l1i_way;
    if (l1_find_way(l1i_meta, l1_tag, &l1i_way)) {
        l1i_meta->state[l1i_way] = MESIState::INVALID;
    }

    // writeback to L3 if dirty
    if (l2_set_meta->state[victim] == MESIState::MODIFIED) {
        uint16_t l3_index = l3_to_index(victim_addr);
        uint64_t l3_tag = l3_to_tag(victim_addr);

        L3SetMeta *l3_meta = &cpu->l3_metas[l3_index];
        L3SetData *l3_data = &cpu->l3_datas[l3_index];

        uint8_t l3_way;
        if (l3_find_way(l3_meta, l3_tag, &l3_way)) {
            std::memcpy(l3_data->data[l3_way], l2_set_data->data[victim],
                        LINE_SIZE);
            l3_meta->state[l3_way] = MESIState::MODIFIED;
        } else {
            std::unreachable();
        }
    }

    l2_set_meta->state[victim] = MESIState::INVALID;
    return victim;
}

static uint8_t l3_evict(CPU *cpu, uint16_t l3_index);

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
        plru_update<uint8_t, NUM_L1_WAYS>(&l1_set_meta->plru_bits, l1_set_way);
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
        uint8_t way = l1d_evict(core, l1_set_index);

        l1_set_meta->tag[way] = l1_set_tag;
        l1_set_meta->state[way] = l2_set_meta->state[l2_set_way];
        std::memcpy(l1_set_data->data[way], l2_set_data->data[l2_set_way],
                    LINE_SIZE);
        plru_update<uint8_t, NUM_L1_WAYS>(&l1_set_meta->plru_bits, way);
        plru_update<uint8_t, NUM_L2_WAYS>(&l2_set_meta->plru_bits, l2_set_way);

        std::memcpy(data, l1_set_data->data[way] + l1_to_offset(address),
                    data_size);
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

        plru_update<uint8_t, NUM_L1_WAYS>(&l1_set_meta->plru_bits, l1_set_way);
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

        uint8_t way = l1d_evict(core, l1_set_index);

        l1_set_meta->tag[way] = l1_set_tag;
        l1_set_meta->state[way] = MESIState::MODIFIED;
        std::memcpy(l1_set_data->data[way], l2_set_data->data[l2_set_way],
                    LINE_SIZE);
        std::memcpy(l1_set_data->data[way] + l1_to_offset(address), data,
                    data_size);
        plru_update<uint8_t, NUM_L1_WAYS>(&l1_set_meta->plru_bits, way);
        plru_update<uint8_t, NUM_L2_WAYS>(&l2_set_meta->plru_bits, l2_set_way);
        return;
    }

    bus_read_exclusive(cpu, core_id, address);
}