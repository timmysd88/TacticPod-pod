#include "../reusable/ble_transport.cpp"
extern "C" unsigned identityBle(unsigned device){
  resetMocks();mockFicr.DEVICEID[0]=0x289430bd;mockFicr.DEVICEID[1]=0x0e8a89b7+device;
  memset(mockIdentityValue,0xa5,sizeof(mockIdentityValue));
  started=false;Adapter target;CHECK(target.begin());
  CHECK(!strcmp(mockBleName,"TacticPod-289430bd"));
  CHECK(strlen(mockBleName)+2<=31 && !strncmp(mockBleName,"TacticPod",9));
  CHECK(!strcmp(mockBleSerial,device?"0e8a89b8289430bd":"0e8a89b7289430bd"));
  CHECK(!strcmp(hardwareIdentity.uuid,"b973000c-7d2a-4f91-a380-8b52c029dc61"));
  CHECK(!strcmp(hardwareIdentity.serviceUuid,"b9730001-7d2a-4f91-a380-8b52c029dc61"));
  CHECK(hardwareIdentity.properties==CHR_PROPS_READ);
  CHECK(hardwareIdentity.readPermission==SECMODE_OPEN && hardwareIdentity.writePermission==SECMODE_NO_ACCESS);
  CHECK(hardwareIdentity.fixedLength==16 && mockIdentityLength==16);
  CHECK(!memcmp(mockIdentityValue,mockBleSerial,16) && mockIdentityValue[16]==0xa5);
  CHECK(mockNotifyCalls==0);
  CHECK(mockProtocolLength==2 && mockProtocolValue[0]==2 && mockProtocolValue[1]==0);
  return 0;
}
extern "C" unsigned identityBleFailure(unsigned scenario){
  resetMocks();started=false;
  mockIdentityBeginFailure=scenario==0;mockIdentityWriteFailure=scenario==1;
  Adapter target;CHECK(!target.begin());
  CHECK(!Bluefruit.Advertising.running);
  return 0;
}
