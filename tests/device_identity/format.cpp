#include "ficr.h"
#include "../../src/device_identity.h"
#include "../../src/tcb3_store.cpp"
char identityJson[192]{},identityHex[17]{},identityDecimal[21]{},identityName[19]{};
uint8_t identityMetadata[Tcb3::MetadataBytes]{};
extern "C" unsigned identityFormat(unsigned which){
  const DeviceIdentity::Id ids[]={{0,0},{1,0},{0xffffffff,0xffffffff},{1,0x200000},{0x289430bd,0x0e8a89b7},{0x289430bd,0x0e8a89b8}};
  const auto id=ids[which];identityTestFicr.DEVICEID[0]=id.low;identityTestFicr.DEVICEID[1]=id.high;
  DeviceIdentity::Id read{};if(!DeviceIdentity::read(read) || read.value()!=id.value())return 1;
  DeviceIdentity::hex(read,identityHex);DeviceIdentity::decimal(read,identityDecimal);DeviceIdentity::bleName(read,identityName);
  if(!DeviceIdentity::info(read,true,identityJson,sizeof(identityJson)))return 2;
  char tiny[8];memset(tiny,0x5a,sizeof(tiny));
  if(DeviceIdentity::info(read,true,tiny,7) || tiny[0] || tiny[7]!=0x5a)return 3;
  if(DeviceIdentity::info(read,false,nullptr,0))return 4;
  Tcb3::metadata(identityMetadata,123456);
  Tcb3::put(identityMetadata+16,read.value(),8);Tcb3::put(identityMetadata+68,1);
  return 0;
}
