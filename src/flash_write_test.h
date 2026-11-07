#pragma once

namespace FlashWriteTest {
// Services confirmation input and the nonblocking test state machine.
void update();
// Suppress other flash diagnostics while a command is in flight or timed out.
bool diagnosticsAllowed();
const char* diagnosticState();
}
