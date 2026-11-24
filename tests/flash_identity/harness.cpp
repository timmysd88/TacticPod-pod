#define FLASH_ID_TEST 1
#include "../flash_write/harness.cpp"
extern "C" unsigned identityCase(unsigned scenario){
  memset(SPI.memory,0xa5,sizeof(SPI.memory));
  for(unsigned i=0;i<256;++i)SPI.sfdp[i]=uint8_t(i);
  memcpy(SPI.sfdp,"SFDP\0\1\1\xff",8);
  flashReportPrinted=true;Serial.connected=true;
  if(scenario==1)SPI.stuckBusy=true;
  if(scenario==2)SPI.wel=true;
  if(scenario==3)SPI.unavailableStatus=true;
  if(scenario==4)Serial.freeSpace=0;
  if(scenario==6)SerialConsole::handleCommand("OBS START");
  if(scenario==8)SPI.unstableSfdp=true;
  if(scenario==9)memset(SPI.sfdp,255,256);
  if(scenario==10){
    uint8_t b[16];const uint8_t forbidden[]={1,2,4,6,0x15,0x20,0x50,0x66,0x99,0xab,0x4b,0xc7};
    for(auto c:forbidden)if(identityRead(nullptr,c,0,b,1))return 1;
    if(identityRead(nullptr,0x5a,256,b,1) || identityRead(nullptr,0x5a,250,b,16) || identityRead(nullptr,0x9f,1,b,3))return 2;
    return SPI.traceCount==0?0:3;
  }
  if(!SerialConsole::handleCommand("DIAG ID"))return 4;
  for(unsigned i=0;i<200;++i){
    if(i==10 && scenario==5)SerialConsole::handleCommand("OBS START");
    if(i==10 && scenario==7)Serial.connected=false;
    loop();++mockTime;
  }
  if(SPI.enableCount || SPI.programCount || SPI.eraseCount || SPI.violations || Serial.overfilledWrites)return 5;
  for(auto b:SPI.memory)if(b!=0xa5)return 6;
  if(scenario==6)return SPI.traceCount==0 && strstr(Serial.output,"DIAG ID refused")?0:7;
  if(scenario==5 || scenario==7)return SPI.traceCount==10 && !identityRunning && !strstr(Serial.output,"BEGIN_FLASH_ID")?0:8;
  if(scenario>=1 && scenario<=3)return SPI.traceCount==1 && strstr(Serial.output,"FLASH ID ERROR") && (scenario!=2 || SPI.wel)?0:9;
  if(SPI.traceCount!=41)return 10;
  if(scenario==4){
    if(strstr(Serial.output,"BEGIN_FLASH_ID"))return 11;
    Serial.freeSpace=256;for(unsigned i=0;i<100;++i){loop();++mockTime;}
  }
  if(identityRunning || !strstr(Serial.output,"BEGIN_FLASH_ID") || !strstr(Serial.output,"qualification=UNRESOLVED"))return 12;
  if(!strstr(Serial.output,scenario==8?"SFDP_repeat=FAIL":"SFDP_repeat=PASS"))return 13;
  for(unsigned i=0;i<41;++i){
    auto p=SPI.trace[i];unsigned n=SPI.traceSize[i];
    if(p[0]==0x5a){
      unsigned k=i-8;if(n!=21 || p[1] || p[2] || p[3]!=(k%16)*16 || p[4])return 14;
      for(unsigned j=5;j<n;++j)if(p[j])return 15;
    }else if(p[0]==0x90){if(n!=6 || p[1] || p[2] || p[3])return 16;}
    else if(p[0]==0x9f){if(n!=4)return 17;}
    else if(p[0]==5 || p[0]==0x35){if(n!=2)return 18;}
    else return 19;
  }
  if(scenario!=9 && memcmp(identityReader.data.sfdp[0],SPI.sfdp,256))return 20;
  return 0;
}
extern "C" unsigned factoryCase(unsigned scenario) {
  memset(SPI.memory,0xa5,sizeof(SPI.memory));
  memset(SPI.sfdp,0,256);
  memcpy(SPI.sfdp,"SFDP\0\1\0\xff",8);
  SPI.sfdp[8]=0;SPI.sfdp[9]=0;SPI.sfdp[10]=1;SPI.sfdp[11]=9;
  SPI.sfdp[12]=16;SPI.sfdp[15]=0xff;
  SPI.sfdp[20]=0xff;SPI.sfdp[21]=0xff;SPI.sfdp[22]=0xff;SPI.sfdp[23]=7;
  Serial.connected=true;flashReportPrinted=true;imuReady=true;imuStatus=0;
  Recorder::mockBusy=true;Recorder::mockFault=true;Recorder::mockDiagnosticStage="Fault";
  if(scenario==1)SPI.wrongId=true;
  if(scenario==2)SPI.stuckBusy=true;
  if(scenario==3)SPI.unavailableStatus=true;
  if(scenario==4)SPI.wel=true;
  if(scenario==5)state=State::Ready;
  if(scenario==6)Serial.freeSpace=0;
  if(!SerialConsole::handleCommand("FACTORY DIAG"))return 1;
  if(!factoryOutputActive)return 2;
  for(unsigned i=0;i<30;++i)updateFactory();
  if(scenario==6){
    if(!factoryOutputActive || Serial.used)return 3;
    Serial.freeSpace=256;for(unsigned i=0;i<30;++i)updateFactory();
  }
  if(factoryOutputActive || !strstr(Serial.output,"END_FACTORY_DIAG") ||
     !strstr(Serial.output,"FACTORY IMU PASS") ||
     !strstr(Serial.output,"FACTORY RECORDER FAIL stage=Fault") ||
     !strstr(Serial.output,"startup_fault=\"mock recorder fault\""))return 4;
  if(SPI.enableCount || SPI.programCount || SPI.eraseCount || SPI.violations || Serial.overfilledWrites)return 5;
  for(auto b:SPI.memory)if(b!=0xa5)return 6;
  if(scenario==2 || scenario==3 || scenario==4) {
    const char* status=scenario==2?"sr1=01":scenario==3?"sr1=FF":"sr1=02";
    return SPI.traceCount==1 && strstr(Serial.output,status) &&
      strstr(Serial.output,"FACTORY JEDEC UNVERIFIED raw=not_read")?0:7;
  }
  if(scenario==5)return !SPI.traceCount && strstr(Serial.output,"reason=ownership_busy")?0:8;
  if(SPI.traceCount!=41 || !strstr(Serial.output,scenario==1?"FACTORY JEDEC FAIL raw=EF4018":"FACTORY JEDEC PASS raw=0B4018") ||
     !strstr(Serial.output,"FACTORY SFDP PASS signature=53464450 capacity_bytes=16777216"))return 9;
  return 0;
}
