#pragma once

namespace GpsUartScan {
bool start();
void update();
bool active();
bool ownsReceiver();
bool faulted();
}
