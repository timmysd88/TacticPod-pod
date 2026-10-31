#pragma once
#include <stdint.h>
#include <string.h>

// Read-only evidence capture. No memory-array access or state-changing opcode.
// One <=16-byte transaction per step; caller must enforce idle flash ownership.
namespace FlashIdentity {
struct Port {
  void* context;
  bool (*read)(void*,uint8_t command,uint32_t address,uint8_t* out,unsigned size);
};
struct Snapshot {
  uint8_t jedec[5][3],rems[2],sr1Before,sr1After,sr2;
  uint8_t sfdp[2][256];
};
class Reader {
public:
  Snapshot data{};
  void begin(){memset(&data,0,sizeof(data));stage=0;error=nullptr;}
  void cancel(){error="cancelled: flash/diagnostic ownership changed";stage=42;}
  bool done() const {return stage>=42;}
  const char* error=nullptr;
  void step(Port port){
    if(done())return;
    uint8_t command=0;uint32_t address=0;uint8_t* out=nullptr;unsigned n=0;
    if(stage==0){command=5;out=&data.sr1Before;n=1;}
    else if(stage<=5){command=0x9f;out=data.jedec[stage-1];n=3;}
    else if(stage==6){command=0x35;out=&data.sr2;n=1;}
    else if(stage==7){command=0x90;out=data.rems;n=2;}
    else if(stage<40){command=0x5a;unsigned chunk=stage-8;address=(chunk%16)*16;out=data.sfdp[chunk/16]+address;n=16;}
    else {command=5;out=&data.sr1After;n=1;}
    if(!port.read(port.context,command,address,out,n)){
      error="read transaction failed";stage=42;return;
    }
    if((stage==0 && (data.sr1Before==255 || (data.sr1Before&3))) ||
       (stage==40 && (data.sr1After==255 || (data.sr1After&3)))){
      error="SR1 unavailable, BUSY or WEL set; no state change attempted";stage=42;return;
    }
    if(stage==40){stage=42;return;}
    ++stage;
  }
private:
  unsigned stage=42;
};
}
