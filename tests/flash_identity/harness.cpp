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
