#include "cpu.h"
#include <bit>
#include <cstdint>
#include <cstring>

template <typename T, uint8_t WAYS>
static void plru_update(T *plru_bits, uint8_t way)
{
    uint8_t node = (WAYS - 1) + way;
    while (node > 0) {
        uint8_t parent = (node - 1) / 2;
        T mask = 1 << parent;
        *plru_bits = (*plru_bits & ~mask) | ((node & 1) << parent);
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

static bool l1_find_way(const L1SetMeta *l1_meta, uint64_t tag, uint8_t *way)
{
    for (uint8_t i = 0; i < NUM_L1_WAYS; i++) {
        if (l1_meta->tag[i] == tag && l1_meta->state[i] != MESIState::INVALID) {
            *way = i;
            return true;
        }
    }
    return false;
}

static void l1_cache_fill(L1SetMeta *l1_meta, L1SetData *l1_data, uint64_t tag,
                          uint8_t way, uint8_t *line, MESIState state)
{
    l1_meta->tag[way] = tag;
    l1_meta->state[way] = state;
    std::memcpy(l1_data->data[way], line, LINE_SIZE);
    plru_update<uint8_t, NUM_L1_WAYS>(&l1_meta->plru_bits, way);
}

static void l2_cache_fill(L2SetMeta *l2_meta, L2SetData *l2_data,
                          uint8_t core_id, uint64_t tag, uint8_t way,
                          uint8_t *data, MESIState state,
                          uint8_t *core_valid_set, uint8_t *core_valid_clr)
{
    if (*core_valid_set != 0 || *core_valid_clr != 0) {
        abort();
    }
    l2_meta->tag[way] = tag;
    l2_meta->state[way] = state;
    *core_valid_set = static_cast<uint8_t>(1 << core_id);
    std::memcpy(l2_data->data[way], data, LINE_SIZE);
    plru_update<uint16_t, NUM_L2_WAYS>(&l2_meta->plru_bits, way);
}

static bool l2_find_way(const L2SetMeta *l2_meta, uint64_t tag, uint8_t *way)
{
    for (uint8_t i = 0; i < NUM_L2_WAYS; i++) {
        if (l2_meta->tag[i] == tag && l2_meta->state[i] != MESIState::INVALID) {
            *way = i;
            return true;
        }
    }
    return false;
}

static void l2_clear_core_valid_way(uint8_t *core_valid, uint8_t core_id)
{
    *core_valid &= ~(1 << core_id);
}

static void snoop_downgrade_peers(Core *cores, uint8_t sharers,
                                  uint16_t l1_index, uint64_t l1_tag,
                                  uint8_t *cache_line, L2SetMeta *l2set_meta,
                                  L2SetData *l2set_data, uint8_t l2_way)
{
    l2set_meta->state[l2_way] = MESIState::SHARED;

    while (sharers) {
        uint8_t c = static_cast<uint8_t>(std::countr_zero(sharers));
        sharers &= sharers - 1;

        L1SetMeta *peer_meta = &cores[c].l1d_metas[l1_index];
        L1SetData *peer_data = &cores[c].l1d_datas[l1_index];
        uint8_t w;
        if (l1_find_way(peer_meta, l1_tag, &w)) {
            if (peer_meta->state[w] == MESIState::MODIFIED) {
                std::memcpy(cache_line, peer_data->data[w], LINE_SIZE);
                std::memcpy(l2set_data->data[l2_way], peer_data->data[w],
                            LINE_SIZE);
            }
            peer_meta->state[w] = MESIState::SHARED;
        }
    }
}

static void snoop_invalidate_peers(Core *cores, uint8_t sharers,
                                   uint16_t l1_index, uint64_t l1_tag,
                                   uint8_t *cache_line, L2SetMeta *l2set_meta,
                                   uint8_t l2_way)
{
    while (sharers) {
        uint8_t c = static_cast<uint8_t>(std::countr_zero(sharers));
        sharers &= sharers - 1;

        L1SetMeta *peer_meta = &cores[c].l1d_metas[l1_index];
        L1SetData *peer_data = &cores[c].l1d_datas[l1_index];
        uint8_t w;
        if (l1_find_way(peer_meta, l1_tag, &w)) {
            if (cache_line && peer_meta->state[w] == MESIState::MODIFIED) {
                std::memcpy(cache_line, peer_data->data[w], LINE_SIZE);
            }
            peer_meta->state[w] = MESIState::INVALID;
        }
        l2set_meta->core_valid_d[l2_way] &= static_cast<uint8_t>(~(1 << c));
    }
}

static void snoop_invalidate_peers_i(Core *cores, uint8_t sharers,
                                     uint16_t l1_index, uint64_t l1_tag)
{
    while (sharers) {
        uint8_t c = static_cast<uint8_t>(std::countr_zero(sharers));
        sharers &= sharers - 1;

        L1SetMeta *peer_meta = &cores[c].l1i_metas[l1_index];
        uint8_t w;
        if (l1_find_way(peer_meta, l1_tag, &w)) {
            peer_meta->state[w] = MESIState::INVALID;
        }
    }
}

static uint8_t l1d_evict(L1SetMeta *l1_meta, L1SetData *l1_data,
                         uint16_t l1_index, L2SetMeta l2_metas[],
                         L2SetData l2_datas[], uint8_t core_id)
{
    for (uint8_t i = 0; i < NUM_L1_WAYS; i++) {
        if (l1_meta->state[i] == MESIState::INVALID) {
            return i;
        }
    }

    uint8_t victim = plru_victim<uint8_t, NUM_L1_WAYS>(l1_meta->plru_bits);

    uint64_t victim_addr = l1_to_addr(l1_meta->tag[victim], l1_index);
    L2SetMeta *l2_meta = &l2_metas[l2_to_index(victim_addr)];
    L2SetData *l2_data = &l2_datas[l2_to_index(victim_addr)];
    uint64_t l2_tag = l2_to_tag(victim_addr);

    uint8_t l2_way;
    if (!l2_find_way(l2_meta, l2_tag, &l2_way)) {
        std::abort();
    }

    if (l1_meta->state[victim] == MESIState::MODIFIED) {
        std::memcpy(l2_data->data[l2_way], l1_data->data[victim], LINE_SIZE);
        l2_meta->state[l2_way] = MESIState::MODIFIED;
        l2_meta->core_valid_d[l2_way] &= ~(1 << core_id);
    } else {
        l2_clear_core_valid_way(&l2_meta->core_valid_d[l2_way], core_id);
        if (l2_meta->core_valid_d[l2_way] == 0 &&
            l2_meta->core_valid_i[l2_way] == 0 &&
            l2_meta->state[l2_way] == MESIState::SHARED) {
            l2_meta->state[l2_way] = MESIState::EXCLUSIVE;
        }
    }

    l1_meta->state[victim] = MESIState::INVALID;
    return victim;
}

static uint8_t l1i_evict(L1SetMeta *l1_meta, L1SetData *l1_data,
                         uint16_t l1_index, L2SetMeta l2_metas[],
                         L2SetData l2_datas[], uint8_t core_id)
{
    for (uint8_t i = 0; i < NUM_L1_WAYS; i++) {
        if (l1_meta->state[i] == MESIState::INVALID) {
            return i;
        }
    }

    uint8_t victim = plru_victim<uint8_t, NUM_L1_WAYS>(l1_meta->plru_bits);
    uint64_t victim_addr = l1_to_addr(l1_meta->tag[victim], l1_index);
    L2SetMeta *l2_meta = &l2_metas[l2_to_index(victim_addr)];
    L2SetData *l2_data = &l2_datas[l2_to_index(victim_addr)];
    uint64_t l2_tag = l2_to_tag(victim_addr);

    uint8_t l2_way;
    if (!l2_find_way(l2_meta, l2_tag, &l2_way)) {
        std::abort();
    }

    l2_clear_core_valid_way(&l2_meta->core_valid_i[l2_way], core_id);
    if (l2_meta->core_valid_d[l2_way] == 0 &&
        l2_meta->core_valid_i[l2_way] == 0 &&
        l2_meta->state[l2_way] == MESIState::SHARED) {
        l2_meta->state[l2_way] = MESIState::EXCLUSIVE;
    }

    l1_meta->state[victim] = MESIState::INVALID;
    return victim;
}

static uint8_t l2_evict(Core *cores, L2SetMeta *l2_meta, L2SetData *l2_data,
                        uint16_t l2_index)
{
    for (uint8_t i = 0; i < NUM_L2_WAYS; i++) {
        if (l2_meta->state[i] == MESIState::INVALID) {
            return i;
        }
    }

    uint8_t l2_victim = plru_victim<uint16_t, NUM_L2_WAYS>(l2_meta->plru_bits);

    uint64_t l2_victim_addr = l2_to_addr(l2_meta->tag[l2_victim], l2_index);
    uint8_t valid_d = l2_meta->core_valid_d[l2_victim];
    uint8_t valid_i = l2_meta->core_valid_i[l2_victim];
    uint8_t all_valid = valid_d | valid_i;
    uint16_t victim_l1_idx = l1_to_index(l2_victim_addr);
    uint64_t victim_l1_tag = l1_to_tag(l2_victim_addr);

    while (all_valid) {
        uint8_t c = static_cast<uint8_t>(std::countr_zero(all_valid));
        all_valid &= all_valid - 1;

        if (valid_d & (1 << c)) {
            L1SetMeta *peer_meta = &cores[c].l1d_metas[victim_l1_idx];
            L1SetData *peer_data = &cores[c].l1d_datas[victim_l1_idx];
            uint8_t w;
            if (l1_find_way(peer_meta, victim_l1_tag, &w)) {
                if (peer_meta->state[w] == MESIState::MODIFIED) {
                    std::memcpy(l2_data->data[l2_victim], peer_data->data[w],
                                LINE_SIZE);
                    l2_meta->state[l2_victim] = MESIState::MODIFIED;
                }
                peer_meta->state[w] = MESIState::INVALID;
            } else {
                std::abort();
            }
        }

        if (valid_i & (1 << c)) {
            L1SetMeta *peer_meta = &cores[c].l1i_metas[victim_l1_idx];
            uint8_t w;
            if (l1_find_way(peer_meta, victim_l1_tag, &w)) {
                peer_meta->state[w] = MESIState::INVALID;
            } else {
                std::abort();
            }
        }
    }

    l2_meta->core_valid_d[l2_victim] = 0;
    l2_meta->core_valid_i[l2_victim] = 0;
    l2_meta->state[l2_victim] = MESIState::INVALID;

    return l2_victim;
}

void cpu_read(CPU *cpu, uint8_t core_id, uint64_t address, uint8_t *data,
              uint8_t data_size)
{
    Core *cores = cpu->cores;
    Core *core = &cpu->cores[core_id];

    uint16_t l1_index = l1_to_index(address);
    uint64_t l1_tag = l1_to_tag(address);
    uint8_t offset = l1_to_offset(address);
    L1SetMeta *l1_meta = &core->l1d_metas[l1_index];
    L1SetData *l1_data = &core->l1d_datas[l1_index];

    uint16_t l2_index = l2_to_index(address);
    uint64_t l2_tag = l2_to_tag(address);
    L2SetMeta *l2_metas = cpu->l2_metas;
    L2SetData *l2_datas = cpu->l2_datas;
    L2SetMeta *l2_meta = &cpu->l2_metas[l2_index];
    L2SetData *l2_data = &cpu->l2_datas[l2_index];

    // L1 hit
    uint8_t l1_hit_way;
    if (l1_find_way(l1_meta, l1_tag, &l1_hit_way)) {
        std::memcpy(data, l1_data->data[l1_hit_way] + offset, data_size);
        plru_update<uint8_t, NUM_L1_WAYS>(&l1_meta->plru_bits, l1_hit_way);
        return;
    }

    core->perf_counters.l1d_misses++;

    // L2 hit
    uint8_t cache_line[LINE_SIZE];
    uint8_t l2_hit_way;
    if (l2_find_way(l2_meta, l2_tag, &l2_hit_way)) {
        uint8_t l1_victim =
            l1d_evict(l1_meta, l1_data, l1_index, l2_metas, l2_datas, core_id);

        std::memcpy(cache_line, l2_data->data[l2_hit_way], LINE_SIZE);
        plru_update<uint16_t, NUM_L2_WAYS>(&l2_meta->plru_bits, l2_hit_way);
        l2_meta->core_valid_d[l2_hit_way] |= static_cast<uint8_t>(1 << core_id);

        uint8_t d_sharers = l2_meta->core_valid_d[l2_hit_way] & ~(1 << core_id);
        uint8_t i_sharers = l2_meta->core_valid_i[l2_hit_way] & ~(1 << core_id);
        uint8_t other_sharers = d_sharers | i_sharers;
        MESIState fill_state;

        if (other_sharers) {
            fill_state = MESIState::SHARED;
            snoop_downgrade_peers(cores, d_sharers, l1_index, l1_tag,
                                  cache_line, l2_meta, l2_data, l2_hit_way);
        } else {
            fill_state = MESIState::EXCLUSIVE;
        }

        l1_cache_fill(l1_meta, l1_data, l1_tag, l1_victim, cache_line,
                      fill_state);
        std::memcpy(data, cache_line + offset, data_size);
        return;
    }

    core->perf_counters.l2_misses++;

    uint8_t l2_victim = l2_evict(cores, l2_meta, l2_data, l2_index);
    uint8_t l1_victim =
        l1d_evict(l1_meta, l1_data, l1_index, l2_metas, l2_datas, core_id);

    std::memset(cache_line, 0, LINE_SIZE);
    l2_cache_fill(l2_meta, l2_data, core_id, l2_tag, l2_victim, cache_line,
                  MESIState::EXCLUSIVE, &l2_meta->core_valid_d[l2_victim],
                  &l2_meta->core_valid_i[l2_victim]);
    l1_cache_fill(l1_meta, l1_data, l1_tag, l1_victim, cache_line,
                  MESIState::EXCLUSIVE);
    std::memcpy(data, cache_line + offset, data_size);
}

void cpu_write(CPU *cpu, uint8_t core_id, uint64_t address, uint8_t *data,
               uint8_t data_size)
{
    Core *cores = cpu->cores;
    Core *core = &cpu->cores[core_id];

    uint16_t l1_index = l1_to_index(address);
    uint64_t l1_tag = l1_to_tag(address);
    uint8_t offset = l1_to_offset(address);
    L1SetMeta *l1_meta = &core->l1d_metas[l1_index];
    L1SetData *l1_data = &core->l1d_datas[l1_index];

    uint16_t l2_index = l2_to_index(address);
    uint64_t l2_tag = l2_to_tag(address);
    L2SetMeta *l2_metas = cpu->l2_metas;
    L2SetData *l2_datas = cpu->l2_datas;
    L2SetMeta *l2_meta = &cpu->l2_metas[l2_index];
    L2SetData *l2_data = &cpu->l2_datas[l2_index];

    // L1 hit
    uint8_t l1_hit_way;
    if (l1_find_way(l1_meta, l1_tag, &l1_hit_way)) {
        uint8_t l2_way;
        if (!l2_find_way(l2_meta, l2_tag, &l2_way)) {
            std::abort();
        }

        if (l1_meta->state[l1_hit_way] == MESIState::SHARED) {
            uint8_t other_sharers =
                l2_meta->core_valid_d[l2_way] & ~(1 << core_id);
            snoop_invalidate_peers(cores, other_sharers, l1_index, l1_tag,
                                   nullptr, l2_meta, l2_way);
            plru_update<uint16_t, NUM_L2_WAYS>(&l2_meta->plru_bits, l2_way);
        }

        snoop_invalidate_peers_i(cores, l2_meta->core_valid_i[l2_way], l1_index,
                                 l1_tag);
        l2_meta->core_valid_i[l2_way] = 0;
        l2_meta->core_valid_d[l2_way] = static_cast<uint8_t>(1 << core_id);
        l2_meta->state[l2_way] = MESIState::MODIFIED;

        std::memcpy(l1_data->data[l1_hit_way] + offset, data, data_size);
        l1_meta->state[l1_hit_way] = MESIState::MODIFIED;
        plru_update<uint8_t, NUM_L1_WAYS>(&l1_meta->plru_bits, l1_hit_way);
        return;
    }

    core->perf_counters.l1d_misses++;

    uint8_t cache_line[LINE_SIZE];
    uint8_t l2_hit_way;
    if (l2_find_way(l2_meta, l2_tag, &l2_hit_way)) {
        std::memcpy(cache_line, l2_data->data[l2_hit_way], LINE_SIZE);

        uint8_t other_sharers =
            l2_meta->core_valid_d[l2_hit_way] & ~(1 << core_id);
        snoop_invalidate_peers(cores, other_sharers, l1_index, l1_tag,
                               cache_line, l2_meta, l2_hit_way);

        std::memcpy(cache_line + offset, data, data_size);

        uint8_t l1_victim =
            l1d_evict(l1_meta, l1_data, l1_index, l2_metas, l2_datas, core_id);
        l1_cache_fill(l1_meta, l1_data, l1_tag, l1_victim, cache_line,
                      MESIState::MODIFIED);

        snoop_invalidate_peers_i(cores, l2_meta->core_valid_i[l2_hit_way],
                                 l1_index, l1_tag);
        l2_meta->core_valid_i[l2_hit_way] = 0;
        l2_meta->core_valid_d[l2_hit_way] = static_cast<uint8_t>(1 << core_id);
        l2_meta->state[l2_hit_way] = MESIState::MODIFIED;
        plru_update<uint16_t, NUM_L2_WAYS>(&l2_meta->plru_bits, l2_hit_way);
        return;
    }

    core->perf_counters.l2_misses++;

    std::memset(cache_line, 0, LINE_SIZE);
    std::memcpy(cache_line + offset, data, data_size);

    uint8_t l2_victim = l2_evict(cores, l2_meta, l2_data, l2_index);
    uint8_t l1_victim =
        l1d_evict(l1_meta, l1_data, l1_index, l2_metas, l2_datas, core_id);

    l2_cache_fill(l2_meta, l2_data, core_id, l2_tag, l2_victim, cache_line,
                  MESIState::MODIFIED, &l2_meta->core_valid_d[l2_victim],
                  &l2_meta->core_valid_i[l2_victim]);
    l1_cache_fill(l1_meta, l1_data, l1_tag, l1_victim, cache_line,
                  MESIState::MODIFIED);
}

void cpu_fetch(CPU *cpu, uint8_t core_id, uint64_t address, uint8_t *data,
               uint8_t data_size)
{
    Core *cores = cpu->cores;
    Core *core = &cpu->cores[core_id];

    uint16_t l1_index = l1_to_index(address);
    uint64_t l1_tag = l1_to_tag(address);
    uint8_t offset = l1_to_offset(address);
    L1SetMeta *l1_meta = &core->l1i_metas[l1_index];
    L1SetData *l1_data = &core->l1i_datas[l1_index];

    uint16_t l2_index = l2_to_index(address);
    uint64_t l2_tag = l2_to_tag(address);
    L2SetMeta *l2_metas = cpu->l2_metas;
    L2SetData *l2_datas = cpu->l2_datas;
    L2SetMeta *l2_meta = &cpu->l2_metas[l2_index];
    L2SetData *l2_data = &cpu->l2_datas[l2_index];

    uint8_t l1_hit_way;
    if (l1_find_way(l1_meta, l1_tag, &l1_hit_way)) {
        std::memcpy(data, l1_data->data[l1_hit_way] + offset, data_size);
        plru_update<uint8_t, NUM_L1_WAYS>(&l1_meta->plru_bits, l1_hit_way);
        return;
    }

    core->perf_counters.l1i_misses++;

    uint8_t cache_line[LINE_SIZE];
    uint8_t l2_hit_way;
    if (l2_find_way(l2_meta, l2_tag, &l2_hit_way)) {
        if (l2_meta->state[l2_hit_way] == MESIState::MODIFIED) {
            std::abort();
        }

        uint8_t l1_victim =
            l1i_evict(l1_meta, l1_data, l1_index, l2_metas, l2_datas, core_id);

        std::memcpy(cache_line, l2_data->data[l2_hit_way], LINE_SIZE);
        plru_update<uint16_t, NUM_L2_WAYS>(&l2_meta->plru_bits, l2_hit_way);
        l2_meta->core_valid_i[l2_hit_way] |= static_cast<uint8_t>(1 << core_id);

        uint8_t d_sharers = l2_meta->core_valid_d[l2_hit_way] & ~(1 << core_id);
        uint8_t i_sharers = l2_meta->core_valid_i[l2_hit_way] & ~(1 << core_id);
        if (d_sharers | i_sharers) {
            snoop_downgrade_peers(cores, d_sharers, l1_index, l1_tag, cache_line,
                                  l2_meta, l2_data, l2_hit_way);
        }

        l1_cache_fill(l1_meta, l1_data, l1_tag, l1_victim, cache_line,
                      MESIState::SHARED);
        std::memcpy(data, cache_line + offset, data_size);
        return;
    }

    core->perf_counters.l2_misses++;

    std::memset(cache_line, 0, LINE_SIZE);

    uint8_t l2_victim = l2_evict(cores, l2_meta, l2_data, l2_index);
    uint8_t l1_victim =
        l1i_evict(l1_meta, l1_data, l1_index, l2_metas, l2_datas, core_id);

    l2_cache_fill(l2_meta, l2_data, core_id, l2_tag, l2_victim, cache_line,
                  MESIState::EXCLUSIVE, &l2_meta->core_valid_i[l2_victim],
                  &l2_meta->core_valid_d[l2_victim]);
    l1_cache_fill(l1_meta, l1_data, l1_tag, l1_victim, cache_line,
                  MESIState::SHARED);
    std::memcpy(data, cache_line + offset, data_size);
}
