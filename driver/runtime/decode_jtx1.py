#!/usr/bin/env python3
"""Decode the bounded explicit JTX1 schema. Does not read or configure hardware."""
import json
import struct
import sys
from pathlib import Path

def decode(data):
    def u32(offset): return struct.unpack_from('<I', data, offset)[0]
    def u64(offset): return struct.unpack_from('<Q', data, offset)[0]
    assert len(data) >= 128 and data[:4] == b'JTX1', 'Not a JTX1 export'
    assert (u32(4), u32(8), u32(12), u32(16)) == (1, 128, 320, 64), 'Unsupported schema'
    count = u32(20)
    assert count <= 64 and len(data) == 128 + count * 320, 'Truncated/oversized trace'
    names = ['jumbo_attempts', 'suppressed_after_capacity', 'service_calls', 'doorbell_attempts',
             'doorbell_failures', 'tx_error_irq_services', 'stops', 'unsafe_stops', 'revision', 'export_ns']
    result = {'schema': 1, 'timestamp_unit': 'monotonic_nanoseconds', 'capacity': 64,
              'count': count, 'capacity_reached': bool(u32(24)),
              'counters': {name: u64(32 + i * 8) for i, name in enumerate(names)},
              'delivery_on_wire_proven': False, 'records': []}
    for index in range(count):
        base = 128 + index * 320
        r = {}
        for i, name in enumerate(['id', 'begin_ns', 'map_done_ns', 'publish_ns', 'complete_ns']):
            r[name] = u64(base + i * 8)
        assert r['id'] == index + 1, 'Invalid record identity'
        for i, name in enumerate(['length', 'mtu', 'generation', 'slot', 'slot_stride', 'first_descriptor', 'last_descriptor', 'segments']):
            r[name] = u32(base + 40 + i * 4)
        for i, name in enumerate(['iova', 'segment_length', 'iova_last_byte', 'arena_offset']):
            r[name] = u64(base + 72 + i * 8)
        for i, name in enumerate(['map_status', 'submit_status', 'copy_flags', 'publish_opts1', 'publish_opts2',
                                  'completion_opts1', 'completion_opts2', 'completion_irq_causes', 'lifecycle_flags']):
            r[name] = u32(base + 104 + i * 4)
        r['ether_type'], r['inner_type'] = struct.unpack_from('<HH', data, base + 140)
        r['destination_mac'] = ':'.join(f'{v:02x}' for v in data[base + 144:base + 150])
        r['ip_protocol'], r['icmp_type'] = data[base + 150:base + 152]
        for i, name in enumerate(['ipv4_id', 'ipv4_length', 'icmp_id', 'icmp_sequence', 'udp_source', 'udp_destination']):
            r[name] = struct.unpack_from('<H', data, base + 152 + i * 2)[0]
        for i, name in enumerate(['services_at_publish', 'doorbells_at_publish', 'services_at_completion', 'doorbells_at_completion']):
            r[name] = u64(base + 168 + i * 8)
        for i, name in enumerate(['phy_at_map_return', 'phy_at_publication', 'phy_at_completion']):
            off = base + 200 + i * 32
            phy = {'observed_ns': u64(off), 'valid_mask': u32(off + 8)}
            for j, key in enumerate(['bmcr', 'bmsr_first', 'bmsr_second', 'anar', 'anlpar']):
                phy[key] = struct.unpack_from('<H', data, off + 12 + j * 2)[0]
            phy['raw_status'], phy['an_pending'], phy['mtu_state'] = data[off + 22:off + 25]
            phy['source'] = 'cached_existing_observation; not a new per-packet PHY read'
            r[name] = phy
        r['pending_at_publication'] = u32(base + 296)
        r['cancel_ns'] = u64(base + 304)
        r['terminal'] = {0: 'pending', 1: 'descriptor_returned', 2: 'cancelled_after_BME_off',
                         3: 'retry', 4: 'rejected'}.get(u32(base + 312), 'unknown')
        r['completion_raw_valid'] = bool(r['lifecycle_flags'] & 2)
        r['publication_bits'] = {name: bool(r['publish_opts1'] & (1 << bit))
                                 for name, bit in [('OWN', 31), ('RingEnd', 30), ('FS', 29), ('LS', 28)]}
        r['copy'] = {name: bool(r['copy_flags'] & (1 << bit))
                     for name, bit in [('prefix_valid', 0), ('attempted', 1), ('succeeded', 2), ('prefix_matches_snapshot', 3)]}
        r['sync_and_kick'] = {name: bool(r['lifecycle_flags'] & (1 << bit)) for name, bit in
                             [('payload_sync_attempted', 7), ('payload_sync_succeeded', 8),
                              ('ring_sync_attempted', 9), ('ring_sync_succeeded', 10),
                              ('doorbell_attempted', 11), ('doorbell_succeeded', 12)]}
        # Raw status is deliberately not decoded with old-generation error masks.
        result['records'].append(r)
    return result

if __name__ == '__main__':
    print(json.dumps(decode(Path(sys.argv[1]).read_bytes()), indent=2))
