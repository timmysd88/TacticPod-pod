"""Offline analysis of saved evidence and approved source; no serial/device import."""
from pathlib import Path
import hashlib
import json
import struct

root = Path(__file__).resolve().parents[2]
folder = Path(__file__).parent
out = folder/'gps-backup-cycle1-investigation'
out.mkdir(exist_ok=True)
manifest = json.loads((folder/'recovery-gate-fix/manifest.json').read_text())
for entry in manifest['changed_source_and_tests'] + manifest['artifacts']:
    assert hashlib.sha256((root/entry['path']).read_bytes()).hexdigest() == entry['sha256'], entry['path']
run = folder/'hardware-run-approved-20261103'
cycle = json.loads((run/'cycle-1.json').read_text())
assert 'standby request sent: YES' in cycle['response']
assert 'standby not confirmed' in cycle['response'] and 'UART wake attempted: NO' in cycle['response']

def frame(flags):
    packet = bytearray(b'\xb5\x62\x02\x41\x10\x00')
    packet.extend(struct.pack('<B3xIII', 0, 0, flags, 0x08))
    a = b = 0
    for value in packet[2:]:
        a = (a + value) & 255
        b = (b + a) & 255
    packet.extend((a, b))
    assert len(packet) == 24
    return bytes(packet)

observed_files = [run/'post-upload-and-cycles-serial.txt', run/'cycle-1.json',
                  run/'cycle-1-response.txt', run/'upload-result.json',
                  root/'src/gps_power.h', root/'src/gps_receiver.cpp', root/'src/gps_uart_tx.h',
                  root/'.pio-core/packages/framework-arduinoadafruitnrf52/cores/nRF5/Uart.cpp',
                  root/'.pio-core/packages/framework-arduinoadafruitnrf52/cores/nRF5/RingBuffer.cpp']
result = dict(
    offline_only=True, device_connected=False, firmware_modified=False,
    base_commit=manifest['base_commit'], patch_sha256=manifest['patch_sha256'],
    approved_source_and_artifact_hashes_unchanged=True,
    binary_sha256='aced1874981173ebfbb3904390f6088214aca3419f72f6a5fae0deb4f4c14051',
    evidence=[dict(path=str(p.relative_to(root)), sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in observed_files],
    reconstructed_packet_from_source_hex=frame(2).hex(' ').upper(),
    reconstructed_packet_is_not_an_on_wire_capture=True,
    payload=dict(version=0, duration_ms=0, flags=2, backup=True, force=False, wakeup_sources=8),
    proposed_packet_not_applied_or_transmitted_hex=frame(6).hex(' ').upper(),
    expected_24_byte_8n1_wire_time_ms=24*10/38400*1000,
    timing=dict(tx_minimum_from_request_callback_ms=5, tx_forced_fallback_ms=105,
                dwell_target_ms=3000, listen_target_ms=1500,
                actual_rx_timestamps=None, actual_listen_duration_ms=None,
                actual_rx_bytes=None, actual_rx_count=None,
                fail_on_first_observed_non_ubx_byte_count_change=True),
    uart_buffer=dict(software_rx_ring_cleared_by_core_end=True,
        ring_is_not_explicitly_drained_at_listen_start=True,
        core_begin_does_not_explicitly_clear_endrx=True,
        stale_peripheral_event_or_restart_transient_cannot_be_excluded=True),
    tx_conclusion='24-byte MCU UART write accepted; physical full delivery/receiver acceptance not measured',
    ack_conclusion='No PMREQ ACK recorded or checked; u-blox ubxlib identifies this command as unacknowledged',
    traffic_classification='INCONCLUSIVE: only non-UBX parser-input counter change recorded; no raw data or NMEA checksum evidence for listen phase',
    leading_cause='Required MAX-M10S software-standby force bit omitted (flags 02 instead of 06)',
    alternatives=['Restart-induced UART RX wake edge', 'Peripheral stale RX event/character or other unclassified input'],
    power_wiring='Breakout schematic/module rail and V_BCKP mapping not available; no wiring defect established',
    corrective_action='Propose backup|force with documented UART wake mask, preserve guards; add bounded TX/RX/phase evidence and verify recovery prerequisites before a separately approved test',
    references=[
        dict(url='https://content.u-blox.com/sites/default/files/MAX-M10S_IntegrationManual_UBX-20053088.pdf', revision='R04', section='3.6.3.2 / page 42'),
        dict(url='https://content.u-blox.com/sites/default/files/u-blox-M10-SPG-5.10_InterfaceDescription_UBX-21035062.pdf', revision='R03 / protocol 34.10', section='3.16.6.1 / pages 116-117'),
        dict(url='https://raw.githubusercontent.com/u-blox/ubxlib/master/gnss/src/u_gnss_pwr.c', section='uGnssPwrOffBackup; primary implementation explicitly notes no acknowledgement')])
(out/'findings.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k != 'evidence'}, indent=2))
