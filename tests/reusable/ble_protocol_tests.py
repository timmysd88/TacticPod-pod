"""Portable framing and CRC tests for the GATT transfer protocol."""
from host_checks import run_cpp

run_cpp('tests/reusable/ble_protocol.cpp','ble-protocol-tests')
print('PASS: BLE protocol CRC and MTU-23/247 fragmentation')
