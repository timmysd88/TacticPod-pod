#include "../reusable/integration.cpp"

extern "C" void useWinbond() {
  memset(SPI.sfdp, 0xff, sizeof(SPI.sfdp));
  const uint8_t header[]={0x53,0x46,0x44,0x50,0,1,0,0xff,0,0,1,9,0x80,0,0,0xff};
  const uint8_t table[]={0xe5,0x20,0xf1,0xff,0xff,0xff,0xff,7};
  memcpy(SPI.sfdp,header,sizeof(header));memcpy(SPI.sfdp+128,table,sizeof(table));
  SPI.sfdp[156]=12;SPI.sfdp[157]=0x20;
  SPI.winbond=true;
}
