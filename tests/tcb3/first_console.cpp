#ifndef TACTICPOD_TCB3_TEST_BUILD
#define TACTICPOD_TCB3_TEST_BUILD 1
#endif
#define TACTICPOD_TCB3_FLASH_ENABLED 1
#define TCB_LARGE_TEST_NOR 1
#include "../flash_write/harness.cpp"
#include "../../src/tcb3_store.cpp"
#include "../../src/tcb3_session.cpp"
bool refusalContains(const char* expected){
  Serial.used=0;Serial.output[0]=0;TcbDetail::lineSize=0;TcbDetail::lineAt=0;
  if(!Tcb3Session::handleCommand("TCB READ"))return false;
  while(TcbDetail::lineSize)TcbDetail::flushLine();
  return Serial.used && Serial.output[Serial.used-1]=='\n' &&
    strstr(Serial.output,"TCB REFUSED: busy/fault") && strstr(Serial.output,expected);
}
bool refusalOmits(const char* unexpected){
  Serial.used=0;Serial.output[0]=0;TcbDetail::lineSize=0;TcbDetail::lineAt=0;
  if(!Tcb3Session::handleCommand("TCB READ"))return false;
  while(TcbDetail::lineSize)TcbDetail::flushLine();
  return Serial.used && Serial.output[Serial.used-1]=='\n' &&
    strstr(Serial.output,"TCB REFUSED: busy/fault") && !strstr(Serial.output,unexpected);
}
extern "C" unsigned firstConsole(){
  memset(SPI.memory,255,sizeof(SPI.memory));memset(SPI.finalSector,255,sizeof(SPI.finalSector));
  Serial.connected=false;setup();
  for(unsigned n=0;n<20000;++n){loop();++mockTime;}
  if(SPI.programs || SPI.erases || SPI.enables || Tcb3Session::exclusive() || CompactRecorder::capturing())return 1;
  Serial.connected=true;FlashWriteTest::update();
  Serial.rx="ERASE FFF000\nRECORD START\nRECORD ERASE\nRECORD ERASE CONFIRM\n";
  for(unsigned n=0;n<100;++n){loop();++mockTime;}
  startTest(); // Internal diagnostic entry point also refuses this build.
  if(SPI.programs || SPI.erases || SPI.enables || state!=State::Idle || SPI.violations)return 2;
  Serial.rx="";
  if(!Tcb3Session::handleCommand("TCB STATUS"))return 3;
  while(TcbDetail::lineSize)TcbDetail::flushLine();
  if(strstr(Serial.output,"tcb_pending="))return 4;
#if TACTICPOD_REUSABLE
  TcbDetail::pending=true;
  if(!refusalContains("tcb_pending=1"))return 5;
  TcbDetail::pending=false;TcbDetail::owned=true;
  if(!refusalContains("tcb_owned=1"))return 6;
  TcbDetail::owned=false;TcbDetail::dumping=true;
  if(!refusalContains("tcb_dumping=1"))return 7;
  TcbDetail::dumping=false;TcbDetail::locked=true;
  if(!refusalContains("tcb_locked=1"))return 8;
  TcbDetail::locked=false;TcbDetail::resetting=true;
  if(!refusalContains("tcb_resetting=1"))return 9;
  TcbDetail::resetting=false;
#else
  TcbDetail::pending=true;
  if(!refusalOmits("tcb_resetting="))return 5;
  TcbDetail::pending=false;
#endif
  Recorder::mockBusy=true;Recorder::mockDiagnosticStage="Record";
  if(!refusalContains("recorder_busy=1 recorder_stage=Record"))return 10;
  Recorder::mockBusy=false;Recorder::mockDiagnosticStage="Idle";
  state=State::Ready;
  if(!refusalContains("flash_diagnostic_busy=1 flash_diagnostic_state=Ready"))return 11;
  state=State::Idle;
  GpsReceiver::mockScanActive=true;
  if(!refusalContains("gps_uart_scan_active=1"))return 12;
  GpsReceiver::mockScanActive=false;GpsReceiver::mockConfigurationActive=true;
  if(!refusalContains("gps_configuration_active=1"))return 13;
  GpsReceiver::mockConfigurationActive=false;GpsReceiver::mockProfileAuditActive=true;
  if(!refusalContains("gps_profile_audit_active=1"))return 14;
  return 0;
}
