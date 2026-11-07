"""Host-only BLE groundwork: idle double-tap classifier and Bluetooth Mode controller."""
from host_checks import run_cpp
run_cpp('tests/reusable/ble_groundwork.cpp', 'ble-groundwork-tests')

print("PASS: BLE double-tap classifier and Bluetooth Mode controller (portable logic tests)")
