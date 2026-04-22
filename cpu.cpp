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
        uint8_t parent = static_cast<uint8_t>((node - 1) / 2);
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
        node = static_cast<uint8_t>(2 * node + 1 + bit);
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

static void flush(uint8_t *dest, uint8_t *src, MESIState *dest_state)
{
    std::memcpy(dest, src, LINE_SIZE);
    *dest_state = MESIState::MODIFIED;
}

static void l1d_fill(Core *core, uint16_t l1_index, uint64_t l1_tag,
                     uint8_t way, uint8_t *line, MESIState state)
{
    L1SetMeta *meta = &core->l1d_metas[l1_index];
    L1SetData *data = &core->l1d_datas[l1_index];

    meta->tag[way] = l1_tag;
    meta->state[way] = state;
    std::memcpy(data->data[way], line, LINE_SIZE);
    plru_update<uint8_t, NUM_L1_WAYS>(&meta->plru_bits, way);
}

static void l1i_fill(Core *core, uint16_t l1_index, uint64_t l1_tag,
                     uint8_t way, uint8_t *line, MESIState state)
{
    L1SetMeta *meta = &core->l1i_metas[l1_index];
    L1SetData *data = &core->l1i_datas[l1_index];

    meta->tag[way] = l1_tag;
    meta->state[way] = state;
    std::memcpy(data->data[way], line, LINE_SIZE);
    plru_update<uint8_t, NUM_L1_WAYS>(&meta->plru_bits, way);
}

static void l2_fill(Core *core, uint16_t l2_index, uint64_t l2_tag, uint8_t way,
                    uint8_t *line, MESIState state)
{
    L2SetMeta *meta = &core->l2_metas[l2_index];
    L2SetData *data = &core->l2_datas[l2_index];

    meta->tag[way] = l2_tag;
    meta->state[way] = state;
    std::memcpy(data->data[way], line, LINE_SIZE);
    plru_update<uint8_t, NUM_L2_WAYS>(&meta->plru_bits, way);
}

enum class CacheType : uint8_t { Data, Instruction };

template <CacheType TYPE>
static void l3_fill(CPU *cpu, uint16_t l3_index, uint64_t l3_tag, uint8_t way,
                    uint8_t *line, MESIState state, uint8_t core_id)
{
    L3SetMeta *meta = &cpu->l3_metas[l3_index];
    L3SetData *data = &cpu->l3_datas[l3_index];

    meta->tag[way] = l3_tag;
    meta->state[way] = state;
    uint8_t core_bit = static_cast<uint8_t>(1 << core_id);
    if constexpr (TYPE == CacheType::Instruction) {
        meta->core_valid_d[way] = 0;
        meta->core_valid_i[way] = core_bit;
    } else {
        meta->core_valid_d[way] = core_bit;
        meta->core_valid_i[way] = 0;
    }
    std::memcpy(data->data[way], line, LINE_SIZE);
    plru_update<uint16_t, NUM_L3_WAYS>(&meta->plru_bits, way);
}

static void l1d_back_invalidate(Core *core, uint16_t l1_index, uint64_t l1_tag,
                                uint8_t *dirty_dest,
                                MESIState *dirty_dest_state)
{
    L1SetMeta *meta = &core->l1d_metas[l1_index];
    L1SetData *data = &core->l1d_datas[l1_index];

    uint8_t way;
    if (l1_find_way(meta, l1_tag, &way)) {
        if (meta->state[way] == MESIState::MODIFIED) {
            flush(dirty_dest, data->data[way], dirty_dest_state);
        }
        meta->state[way] = MESIState::INVALID;
    }
}

static void l1i_back_invalidate(Core *core, uint16_t l1_index, uint64_t l1_tag)
{
    L1SetMeta *meta = &core->l1i_metas[l1_index];

    uint8_t way;
    if (l1_find_way(meta, l1_tag, &way)) {
        meta->state[way] = MESIState::INVALID;
    }
}

static uint8_t l1d_evict(Core *core, uint16_t l1_index)
{
    L1SetMeta *l1_set_meta = &core->l1d_metas[l1_index];
    L1SetData *l1_set_data = &core->l1d_datas[l1_index];

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
            flush(l2_data->data[l2_way], l1_set_data->data[victim],
                  &l2_meta->state[l2_way]);
        } else {
            // writeback policy ensures that l1 cacheline is always in l2
            std::unreachable();
        }
    }

    l1_set_meta->state[victim] = MESIState::INVALID;
    return victim;
}

static uint8_t l1i_evict(Core *core, uint16_t l1_index)
{
    L1SetMeta *meta = &core->l1i_metas[l1_index];

    for (uint8_t i = 0; i < NUM_L1_WAYS; i++) {
        if (meta->state[i] == MESIState::INVALID) {
            return i;
        }
    }

    uint8_t victim = plru_victim<uint8_t, NUM_L1_WAYS>(meta->plru_bits);
    meta->state[victim] = MESIState::INVALID;
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

    l1d_back_invalidate(core, l1_index, l1_tag, l2_set_data->data[victim],
                        &l2_set_meta->state[victim]);
    l1i_back_invalidate(core, l1_index, l1_tag);

    if (l2_set_meta->state[victim] == MESIState::MODIFIED) {
        uint16_t l3_index = l3_to_index(victim_addr);
        uint64_t l3_tag = l3_to_tag(victim_addr);

        L3SetMeta *l3_meta = &cpu->l3_metas[l3_index];
        L3SetData *l3_data = &cpu->l3_datas[l3_index];

        uint8_t l3_way;
        if (l3_find_way(l3_meta, l3_tag, &l3_way)) {
            flush(l3_data->data[l3_way], l2_set_data->data[victim],
                  &l3_meta->state[l3_way]);
        } else {
            std::unreachable();
        }
    }

    l2_set_meta->state[victim] = MESIState::INVALID;
    return victim;
}

static uint8_t l3_evict(CPU *cpu, uint16_t l3_index, uint8_t *memory)
{
    L3SetMeta *l3_set_meta = &cpu->l3_metas[l3_index];
    L3SetData *l3_set_data = &cpu->l3_datas[l3_index];

    for (uint8_t i = 0; i < NUM_L3_WAYS; i++) {
        if (l3_set_meta->state[i] == MESIState::INVALID) {
            return i;
        }
    }

    uint8_t victim = plru_victim<uint16_t, NUM_L3_WAYS>(l3_set_meta->plru_bits);

    uint64_t victim_addr = l3_to_addr(l3_set_meta->tag[victim], l3_index);
    uint16_t l2_set_index = l2_to_index(victim_addr);
    uint64_t l2_set_tag = l2_to_tag(victim_addr);
    uint16_t l1_set_index = l1_to_index(victim_addr);
    uint64_t l1_set_tag = l1_to_tag(victim_addr);

    for (uint8_t i = 0; i < NUM_CORES; i++) {
        Core *core = &cpu->cores[i];

        if (l3_set_meta->core_valid_d[victim] & (1 << i)) {
            L2SetMeta *l2_meta = &core->l2_metas[l2_set_index];
            L2SetData *l2_data = &core->l2_datas[l2_set_index];

            uint8_t l2_way;
            if (l2_find_way(l2_meta, l2_set_tag, &l2_way)) {
                l1d_back_invalidate(core, l1_set_index, l1_set_tag,
                                    l2_data->data[l2_way],
                                    &l2_meta->state[l2_way]);

                if (l2_meta->state[l2_way] == MESIState::MODIFIED) {
                    flush(l3_set_data->data[victim], l2_data->data[l2_way],
                          &l3_set_meta->state[victim]);
                }
                l2_meta->state[l2_way] = MESIState::INVALID;
            }
        }

        if (l3_set_meta->core_valid_i[victim] & (1 << i)) {
            l1i_back_invalidate(core, l1_set_index, l1_set_tag);
        }
    }

    if (l3_set_meta->state[victim] == MESIState::MODIFIED) {
        uint64_t victim_addr = l3_to_addr(l3_set_meta->tag[victim], l3_index);
        std::memcpy(&memory[to_line_base(victim_addr)],
                    l3_set_data->data[victim], LINE_SIZE);
    }

    l3_set_meta->core_valid_d[victim] = 0;
    l3_set_meta->core_valid_i[victim] = 0;
    l3_set_meta->state[victim] = MESIState::INVALID;
    return victim;
}

static void bus_read_data(CPU *cpu, uint8_t core_id, uint64_t address,
                          uint8_t *data, uint8_t data_size, uint8_t *memory)
{
    uint16_t l3_set_index = l3_to_index(address);
    uint64_t l3_set_tag = l3_to_tag(address);

    L3SetMeta *l3_set_meta = &cpu->l3_metas[l3_set_index];
    L3SetData *l3_set_data = &cpu->l3_datas[l3_set_index];

    uint8_t l3_set_way;
    if (l3_find_way(l3_set_meta, l3_set_tag, &l3_set_way)) {
        bool shared = false;

        for (uint8_t i = 0; i < NUM_CORES; i++) {
            if (i == core_id) {
                continue;
            }

            Core *core = &cpu->cores[i];
            uint16_t l1_set_index = l1_to_index(address);
            uint64_t l1_set_tag = l1_to_tag(address);
            L1SetMeta *l1_set_meta = &core->l1d_metas[l1_set_index];
            L1SetData *l1_set_data = &core->l1d_datas[l1_set_index];

            uint8_t l1_way;
            if (l1_find_way(l1_set_meta, l1_set_tag, &l1_way)) {
                shared = true;

                switch (l1_set_meta->state[l1_way]) {
                case MESIState::MODIFIED:
                    flush(l3_set_data->data[l3_set_way],
                          l1_set_data->data[l1_way],
                          &l3_set_meta->state[l3_set_way]);
                    l1_set_meta->state[l1_way] = MESIState::SHARED;
                    break;
                case MESIState::EXCLUSIVE:
                    l1_set_meta->state[l1_way] = MESIState::SHARED;
                    break;
                case MESIState::SHARED:
                    break;
                case MESIState::INVALID:
                    std::unreachable();
                }
            }
        }

        MESIState fill_state =
            shared ? MESIState::SHARED : MESIState::EXCLUSIVE;

        Core *req_core = &cpu->cores[core_id];
        uint16_t l2_set_index = l2_to_index(address);
        uint64_t l2_set_tag = l2_to_tag(address);
        uint16_t l1_set_index = l1_to_index(address);
        uint64_t l1_set_tag = l1_to_tag(address);

        uint8_t l2_way = l2_evict(cpu, core_id, l2_set_index);
        l2_fill(req_core, l2_set_index, l2_set_tag, l2_way,
                l3_set_data->data[l3_set_way], fill_state);

        uint8_t l1_way = l1d_evict(req_core, l1_set_index);
        l1d_fill(req_core, l1_set_index, l1_set_tag, l1_way,
                 l3_set_data->data[l3_set_way], fill_state);

        plru_update<uint16_t, NUM_L3_WAYS>(&l3_set_meta->plru_bits, l3_set_way);
        l3_set_meta->core_valid_d[l3_set_way] |=
            static_cast<uint8_t>(1 << core_id);

        std::memcpy(data, l3_set_data->data[l3_set_way] + l1_to_offset(address),
                    data_size);
        return;
    }

    // l3 miss
    uint8_t cache_line[LINE_SIZE];
    std::memcpy(cache_line, &memory[to_line_base(address)], LINE_SIZE);

    uint8_t l3_way = l3_evict(cpu, l3_set_index, memory);
    l3_fill<CacheType::Data>(cpu, l3_set_index, l3_set_tag, l3_way, cache_line,
                             MESIState::EXCLUSIVE, core_id);

    Core *req_core = &cpu->cores[core_id];
    uint16_t l2_set_index = l2_to_index(address);
    uint64_t l2_set_tag = l2_to_tag(address);
    uint16_t l1_set_index = l1_to_index(address);
    uint64_t l1_set_tag = l1_to_tag(address);

    uint8_t l2_way = l2_evict(cpu, core_id, l2_set_index);
    l2_fill(req_core, l2_set_index, l2_set_tag, l2_way, cache_line,
            MESIState::EXCLUSIVE);

    uint8_t l1_way = l1d_evict(req_core, l1_set_index);
    l1d_fill(req_core, l1_set_index, l1_set_tag, l1_way, cache_line,
             MESIState::EXCLUSIVE);

    std::memcpy(data, cache_line + l1_to_offset(address), data_size);
}

static void bus_read_instruction(CPU *cpu, uint8_t core_id, uint64_t address,
                                 uint8_t *data, uint8_t data_size,
                                 uint8_t *memory)
{
    uint16_t l3_set_index = l3_to_index(address);
    uint64_t l3_set_tag = l3_to_tag(address);

    L3SetMeta *l3_set_meta = &cpu->l3_metas[l3_set_index];
    L3SetData *l3_set_data = &cpu->l3_datas[l3_set_index];

    uint8_t l3_set_way;
    if (l3_find_way(l3_set_meta, l3_set_tag, &l3_set_way)) {
        bool shared = false;

        for (uint8_t i = 0; i < NUM_CORES; i++) {
            if (i == core_id) {
                continue;
            }

            Core *core = &cpu->cores[i];
            uint16_t l1_set_index = l1_to_index(address);
            uint64_t l1_set_tag = l1_to_tag(address);
            L1SetMeta *l1_set_meta = &core->l1d_metas[l1_set_index];
            L1SetData *l1_set_data = &core->l1d_datas[l1_set_index];

            uint8_t l1_way;
            if (l1_find_way(l1_set_meta, l1_set_tag, &l1_way)) {
                shared = true;

                switch (l1_set_meta->state[l1_way]) {
                case MESIState::MODIFIED:
                    flush(l3_set_data->data[l3_set_way],
                          l1_set_data->data[l1_way],
                          &l3_set_meta->state[l3_set_way]);
                    l1_set_meta->state[l1_way] = MESIState::SHARED;
                    break;
                case MESIState::EXCLUSIVE:
                    l1_set_meta->state[l1_way] = MESIState::SHARED;
                    break;
                case MESIState::SHARED:
                    break;
                case MESIState::INVALID:
                    std::unreachable();
                }
            }
        }

        Core *req_core = &cpu->cores[core_id];
        uint16_t l2_set_index = l2_to_index(address);
        uint64_t l2_set_tag = l2_to_tag(address);
        uint16_t l1_set_index = l1_to_index(address);
        uint64_t l1_set_tag = l1_to_tag(address);

        uint8_t l2_way = l2_evict(cpu, core_id, l2_set_index);
        l2_fill(req_core, l2_set_index, l2_set_tag, l2_way,
                l3_set_data->data[l3_set_way], MESIState::SHARED);

        uint8_t l1_way = l1i_evict(req_core, l1_set_index);
        l1i_fill(req_core, l1_set_index, l1_set_tag, l1_way,
                 l3_set_data->data[l3_set_way], MESIState::SHARED);

        plru_update<uint16_t, NUM_L3_WAYS>(&l3_set_meta->plru_bits, l3_set_way);
        l3_set_meta->core_valid_i[l3_set_way] |=
            static_cast<uint8_t>(1 << core_id);

        std::memcpy(data, l3_set_data->data[l3_set_way] + l1_to_offset(address),
                    data_size);
        return;
    }

    // l3 miss
    uint8_t cache_line[LINE_SIZE];
    std::memcpy(cache_line, &memory[to_line_base(address)], LINE_SIZE);

    uint8_t l3_way = l3_evict(cpu, l3_set_index, memory);
    l3_fill<CacheType::Instruction>(cpu, l3_set_index, l3_set_tag, l3_way,
                                    cache_line, MESIState::EXCLUSIVE, core_id);

    Core *req_core = &cpu->cores[core_id];
    uint16_t l2_set_index = l2_to_index(address);
    uint64_t l2_set_tag = l2_to_tag(address);
    uint16_t l1_set_index = l1_to_index(address);
    uint64_t l1_set_tag = l1_to_tag(address);

    uint8_t l2_way = l2_evict(cpu, core_id, l2_set_index);
    l2_fill(req_core, l2_set_index, l2_set_tag, l2_way, cache_line,
            MESIState::SHARED);

    uint8_t l1_way = l1i_evict(req_core, l1_set_index);
    l1i_fill(req_core, l1_set_index, l1_set_tag, l1_way, cache_line,
             MESIState::SHARED);

    std::memcpy(data, cache_line + l1_to_offset(address), data_size);
}

static void bus_read_exclusive(CPU *cpu, uint8_t core_id, uint64_t address,
                               uint8_t *data, uint8_t data_size,
                               uint8_t *memory)
{
    uint16_t l3_set_index = l3_to_index(address);
    uint64_t l3_set_tag = l3_to_tag(address);

    L3SetMeta *l3_set_meta = &cpu->l3_metas[l3_set_index];
    L3SetData *l3_set_data = &cpu->l3_datas[l3_set_index];

    uint8_t l3_set_way;
    if (l3_find_way(l3_set_meta, l3_set_tag, &l3_set_way)) {
        for (uint8_t i = 0; i < NUM_CORES; i++) {
            if (i == core_id) {
                continue;
            }

            Core *core = &cpu->cores[i];
            uint16_t l1_set_index = l1_to_index(address);
            uint64_t l1_set_tag = l1_to_tag(address);
            L1SetMeta *l1_set_meta = &core->l1d_metas[l1_set_index];
            L1SetData *l1_set_data = &core->l1d_datas[l1_set_index];

            uint8_t l1_way;
            if (l1_find_way(l1_set_meta, l1_set_tag, &l1_way)) {
                switch (l1_set_meta->state[l1_way]) {
                case MESIState::MODIFIED:
                    flush(l3_set_data->data[l3_set_way],
                          l1_set_data->data[l1_way],
                          &l3_set_meta->state[l3_set_way]);
                    l1_set_meta->state[l1_way] = MESIState::INVALID;
                    break;
                case MESIState::EXCLUSIVE:
                case MESIState::SHARED:
                    l1_set_meta->state[l1_way] = MESIState::INVALID;
                    break;
                case MESIState::INVALID:
                    std::unreachable();
                }
            }
        }

        Core *req_core = &cpu->cores[core_id];
        uint16_t l2_set_index = l2_to_index(address);
        uint64_t l2_set_tag = l2_to_tag(address);
        uint16_t l1_set_index = l1_to_index(address);
        uint64_t l1_set_tag = l1_to_tag(address);

        uint8_t l2_way = l2_evict(cpu, core_id, l2_set_index);
        l2_fill(req_core, l2_set_index, l2_set_tag, l2_way,
                l3_set_data->data[l3_set_way], MESIState::MODIFIED);

        uint8_t l1_way = l1d_evict(req_core, l1_set_index);
        l1d_fill(req_core, l1_set_index, l1_set_tag, l1_way,
                 l3_set_data->data[l3_set_way], MESIState::MODIFIED);

        plru_update<uint16_t, NUM_L3_WAYS>(&l3_set_meta->plru_bits, l3_set_way);
        l3_set_meta->core_valid_d[l3_set_way] =
            static_cast<uint8_t>(1 << core_id);

        std::memcpy(req_core->l1d_datas[l1_set_index].data[l1_way] +
                        l1_to_offset(address),
                    data, data_size);
        return;
    }

    // l3 miss
    uint8_t cache_line[LINE_SIZE];
    std::memcpy(cache_line, &memory[to_line_base(address)], LINE_SIZE);

    uint8_t l3_way = l3_evict(cpu, l3_set_index, memory);
    l3_fill<CacheType::Data>(cpu, l3_set_index, l3_set_tag, l3_way, cache_line,
                             MESIState::MODIFIED, core_id);

    Core *req_core = &cpu->cores[core_id];
    uint16_t l2_set_index = l2_to_index(address);
    uint64_t l2_set_tag = l2_to_tag(address);
    uint16_t l1_set_index = l1_to_index(address);
    uint64_t l1_set_tag = l1_to_tag(address);

    uint8_t l2_way = l2_evict(cpu, core_id, l2_set_index);
    l2_fill(req_core, l2_set_index, l2_set_tag, l2_way, cache_line,
            MESIState::MODIFIED);

    uint8_t l1_way = l1d_evict(req_core, l1_set_index);
    l1d_fill(req_core, l1_set_index, l1_set_tag, l1_way, cache_line,
             MESIState::MODIFIED);

    std::memcpy(req_core->l1d_datas[l1_set_index].data[l1_way] +
                    l1_to_offset(address),
                data, data_size);
}

static void bus_upgrade(CPU *cpu, uint8_t core_id, uint64_t address)
{
    for (uint8_t i = 0; i < NUM_CORES; i++) {
        if (i == core_id) {
            continue;
        }

        Core *core = &cpu->cores[i];
        uint16_t l1_set_index = l1_to_index(address);
        uint64_t l1_set_tag = l1_to_tag(address);
        L1SetMeta *l1_set_meta = &core->l1d_metas[l1_set_index];

        uint8_t l1_way;
        if (l1_find_way(l1_set_meta, l1_set_tag, &l1_way)) {
            l1_set_meta->state[l1_way] = MESIState::INVALID;
        }
    }
}

static void processor_read(CPU *cpu, uint8_t core_id, uint64_t address,
                           uint8_t *data, uint8_t data_size, uint8_t *memory)
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
        l1d_fill(core, l1_set_index, l1_set_tag, way,
                 l2_set_data->data[l2_set_way], l2_set_meta->state[l2_set_way]);
        plru_update<uint8_t, NUM_L2_WAYS>(&l2_set_meta->plru_bits, l2_set_way);

        std::memcpy(data,
                    core->l1d_datas[l1_set_index].data[way] +
                        l1_to_offset(address),
                    data_size);
        return;
    }

    bus_read_data(cpu, core_id, address, data, data_size, memory);
}

static void processor_write(CPU *cpu, uint8_t core_id, uint64_t address,
                            uint8_t *data, uint8_t data_size, uint8_t *memory)
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
        l1d_fill(core, l1_set_index, l1_set_tag, way,
                 l2_set_data->data[l2_set_way], MESIState::MODIFIED);
        std::memcpy(l1_set_data->data[way] + l1_to_offset(address), data,
                    data_size);
        plru_update<uint8_t, NUM_L2_WAYS>(&l2_set_meta->plru_bits, l2_set_way);
        return;
    }

    bus_read_exclusive(cpu, core_id, address, data, data_size, memory);
}

static void processor_fetch(CPU *cpu, uint8_t core_id, uint64_t address,
                            uint8_t *data, uint8_t data_size, uint8_t *memory)
{
    uint16_t l1_set_index = l1_to_index(address);
    uint64_t l1_set_tag = l1_to_tag(address);

    Core *core = &cpu->cores[core_id];
    L1SetMeta *l1_set_meta = &core->l1i_metas[l1_set_index];
    L1SetData *l1_set_data = &core->l1i_datas[l1_set_index];

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
        uint8_t way = l1i_evict(core, l1_set_index);
        l1i_fill(core, l1_set_index, l1_set_tag, way,
                 l2_set_data->data[l2_set_way], MESIState::SHARED);
        plru_update<uint8_t, NUM_L2_WAYS>(&l2_set_meta->plru_bits, l2_set_way);

        std::memcpy(data,
                    core->l1i_datas[l1_set_index].data[way] +
                        l1_to_offset(address),
                    data_size);
        return;
    }

    bus_read_instruction(cpu, core_id, address, data, data_size, memory);
}
