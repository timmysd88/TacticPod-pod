#pragma once

namespace FlashDiagnostics {
// Request read-only output only when no recorder/flash test owns the bus.
void request();
// Explicit, incremental read-only identity evidence. Never runs automatically.
void requestIdentity();
}
