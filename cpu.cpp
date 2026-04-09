#include "cpu.h"
#include <bit>
#include <cstdint>
#include <cstring>

template <typename T, uint8_t WAYS> void plru_update(T *plru_bits, uint8_t way)
{
    uint8_t node = (WAYS - 1) + way;
    while (node > 0) {
        uint8_t parent = (node - 1) / 2;
        T mask = 1 << parent;
        *plru_bits = (*plru_bits & ~mask) | ((node & 1) << parent);
        node = parent;
    }
}

template <typename T, uint8_t WAYS> uint8_t plru_victim(T plru_bits)
{
    uint8_t node = 0;
    while (node < WAYS - 1) {
        uint8_t bit = (plru_bits >> node) & 1;
        node = 2 * node + 1 + bit;
    }
    return node - (WAYS - 1);
}

static bool find_l1_way(const L1SetMeta *l1set_meta, uint64_t tag, uint8_t *way)
{
    for (uint8_t i = 0; i < NUM_L1_WAYS; i++) {
        if (l1set_meta->tag[i] == tag &&
            l1set_meta->state[i] != MESIState::INVALID) {
            *way = i;
            return true;
        }
    }

    return false;
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
        if (find_l1_way(peer_meta, l1_tag, &w)) {
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
        if (find_l1_way(peer_meta, l1_tag, &w)) {
            if (cache_line && peer_meta->state[w] == MESIState::MODIFIED) {
                std::memcpy(cache_line, peer_data->data[w], LINE_SIZE);
            }
            peer_meta->state[w] = MESIState::INVALID;
            l2set_meta->core_valid_d[l2_way] &= static_cast<uint8_t>(~(1 << c));
        }
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
        if (find_l1_way(peer_meta, l1_tag, &w)) {
            peer_meta->state[w] = MESIState::INVALID;
        }
    }
}

void cpu_read(CPU *cpu, uint8_t core_id, uint64_t address, uint8_t *data,
              uint8_t data_size)
{
    Core *core = &cpu->cores[core_id];

    uint16_t l1_index = l1_to_index(address);
    uint64_t l1_tag = l1_to_tag(address);
    uint8_t offset = l1_to_offset(address);
    L1SetMeta *l1_meta = &core->l1d_metas[l1_index];
    L1SetData *l1_data = &core->l1d_datas[l1_index];

    uint16_t l2_index = l2_to_index(address);
    uint64_t l2_tag = l2_to_tag(address);
    L2SetMeta *l2_set_meta = &cpu->l2_metas[l2_index];
    L2SetData *l2_set_data = &cpu->l2_datas[l2_index];
}

void cpu_write(CPU *cpu, uint8_t core_id, uint64_t address, uint8_t *data,
               uint8_t data_size)
{
}

void cpu_fetch(CPU *cpu, uint8_t core_id, uint64_t address, uint8_t *data,
               uint8_t data_size)
{
}
