"""Host checks of the local protocol and firmware (requires g++)."""
from pathlib import Path
import shutil
import subprocess
from host_stubs import write_uno_stubs

project = Path(__file__).resolve().parents[1]
out = project / ".pio/bus-checks"
out.mkdir(parents=True, exist_ok=True)
write_uno_stubs(out)
common = r'''
#include <cassert>
#include <cstdio>
#include <vector>
#include "puzzle_i2c.h"
void deliver(uint8_t type,uint64_t round,uint16_t seed,uint8_t difficulty,uint8_t op=ModuleBus::GENERATE) {
  ModuleBus::Frame f;f.type=type;f.operation=op;f.round=round;f.seed=seed;f.difficulty=difficulty;
  uint8_t bytes[ModuleBus::FRAME_SIZE];ModuleBus::encode(f,bytes);Wire.inject(bytes,sizeof(bytes));
}
ModuleBus::Frame response() {
  auto bytes=Wire.request();ModuleBus::Frame f;
  assert(ModuleBus::decode(bytes.data(),bytes.size(),f));return f;
}
'''
protocol = r'''
int main() {
  PuzzleI2c endpoint;endpoint.begin(ModuleBus::SIMON,ModuleBus::SIMON_ADDRESS);
  assert(response().state==ModuleBus::WAITING);
  deliver(ModuleBus::SIMON,4294967297ULL,42,2);
  assert(endpoint.status.round==0); // No work inside onReceive ISR.
  assert(endpoint.update()==PuzzleI2c::NEW_GAME);
  assert(response().round==4294967297ULL && response().seed==42 && response().difficulty==2);
  endpoint.smallError();endpoint.smallError();endpoint.solved();
  assert(response().errors==2 && response().state==ModuleBus::SOLVED);
  deliver(ModuleBus::SIMON,4294967297ULL,42,2);
  assert(endpoint.update()==PuzzleI2c::NONE && response().errors==2);
  deliver(ModuleBus::LABYRINTH,10,1234,0);
  assert(endpoint.update()==PuzzleI2c::NONE);
  ModuleBus::Frame f;f.type=ModuleBus::SIMON;f.operation=ModuleBus::GENERATE;f.round=99;
  uint8_t data[ModuleBus::FRAME_SIZE];ModuleBus::encode(f,data);
  for(unsigned i=0;i<sizeof(data);++i) {
    data[i]^=1;Wire.inject(data,sizeof(data));assert(endpoint.update()==PuzzleI2c::NONE);data[i]^=1;
  }
  Wire.inject(data,sizeof(data)-1);assert(endpoint.update()==PuzzleI2c::NONE);
  deliver(ModuleBus::SIMON,10,0,0);assert(endpoint.update()==PuzzleI2c::NEW_GAME);
  assert(response().errors==0 && response().seed==0);
  deliver(ModuleBus::SIMON,9,0,0,ModuleBus::STOP);
  assert(endpoint.update()==PuzzleI2c::NONE);
  deliver(ModuleBus::SIMON,10,0,0,ModuleBus::STOP);
  assert(endpoint.update()==PuzzleI2c::HALT && response().state==ModuleBus::STOPPED);
  endpoint.smallError();assert(response().errors==0);
  deliver(ModuleBus::SIMON,10,0,0,ModuleBus::RESET);
  assert(endpoint.update()==PuzzleI2c::HALT && response().round==0);
  uint8_t module;char event[8];uint64_t round;uint16_t count;
  assert(ModuleBus::parseEvent("G:3:ERROR:18446744073709551615:65535",module,event,round,count));
  assert(module==3 && round==UINT64_MAX && count==65535);
  assert(ModuleBus::parseEvent("E:GACK:2:SOLVED:42:0",module,event,round,count,"E:GACK:"));
  for(auto bad:{"G:1:ERROR:1:1","G:2:ERROR:1:65536","G:2:BOGUS:1:1","G:2:ERROR:-1:1","G:2:ERROR:1:1:1"})
    assert(!ModuleBus::parseEvent(bad,module,event,round,count));
  puts("PASS: CRC, deferred ISR, duplicate rounds, reset/stop, counters and UART envelopes");
}
'''
gateway = r'''
#include "src/main.cpp"
void nodeReply(uint8_t type,uint8_t address,uint64_t round,uint16_t errors,uint8_t state=ModuleBus::PLAYING) {
  ModuleBus::Frame f;f.type=type;f.round=round;f.seed=42;f.difficulty=1;f.errors=errors;f.state=state;
  uint8_t packet[ModuleBus::FRAME_SIZE];ModuleBus::encode(f,packet);
  Wire.replies[address]=std::vector<uint8_t>(packet,packet+sizeof(packet));
}
int main() {
  setup();assert(lastGenerationId==0);
  nodeReply(2,0x11,0,0);nodeReply(3,0x12,0,0);
  updateGateway(25);updateGateway(50);
  assert(nodes[0].online && nodes[1].online);
  assert(espLink.tx.find("G:2:ONLINE:0:0\n")!=std::string::npos);
  espLink.tx.clear();handleEspLine("E:GENERATE:4294967297:42:1");
  for(int address:{0x11,0x12}) {
    ModuleBus::Frame f;auto p=Wire.writes[address];assert(ModuleBus::decode(p.data(),p.size(),f));
    assert(f.round==4294967297ULL && f.seed==42 && f.difficulty==1 && f.operation==ModuleBus::GENERATE);
  }
  nodeReply(2,0x11,4294967297ULL,0);nodeReply(3,0x12,4294967297ULL,0);
  updateGateway(75);updateGateway(100);assert(nodes[0].ready && nodes[1].ready);
  espLink.tx.clear();nodeReply(2,0x11,4294967297ULL,2);
  updateGateway(125);assert(espLink.tx=="G:2:ERROR:4294967297:2\n");
  handleEspLine("E:GACK:2:ERROR:4294967296:2");assert(nodes[0].ackedErrors==0);
  handleEspLine("E:GACK:2:ERROR:4294967297:2");assert(nodes[0].ackedErrors==2);
  espLink.tx.clear();updateGateway(150);updateGateway(625);assert(espLink.tx.empty());
  nodeReply(2,0x11,4294967297ULL,3,ModuleBus::SOLVED);
  updateGateway(650);updateGateway(675);
  assert(espLink.tx.find("G:2:ERROR:4294967297:3\n")!=std::string::npos);
  assert(espLink.tx.find("G:2:SOLVED:4294967297:0\n")!=std::string::npos);
  handleEspLine("E:GACK:2:ERROR:4294967297:3");handleEspLine("E:GACK:2:SOLVED:4294967297:0");
  espLink.tx.clear();updateGateway(700);updateGateway(1200);assert(espLink.tx.empty());
  // Duplicate ESP delivery must not reset child counters or acknowledgements.
  handleEspLine("E:GENERATE:4294967297:42:1");assert(nodes[0].ackedErrors==3 && nodes[0].resultAcked);
  // A reset child is reconfigured with the active round.
  nodeReply(3,0x12,0,0);updateGateway(1225);updateGateway(1250);
  auto packet=Wire.writes[0x12];ModuleBus::Frame f;assert(ModuleBus::decode(packet.data(),packet.size(),f));
  assert(f.round==4294967297ULL && f.operation==ModuleBus::GENERATE);
  Wire.replies[0x12].clear();
  for(uint32_t t=1275;t<=1450;t+=25)updateGateway(t);
  assert(!nodes[1].online);
  puts("PASS: gateway discovery, generation relay, cumulative errors/ACK, solved retry, reconnect and timeout");
}
'''

compiler = shutil.which("g++")
if not compiler:
    raise SystemExit("g++ required")
for name, checks in (("protocol", protocol), ("gateway", gateway)):
    cpp = out / f"{name}.cpp"
    cpp.write_text(common + checks, encoding="utf-8")
    exe = out / f"{name}.exe"
    subprocess.run([compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-I", str(out), "-I", str(project), "-I", str(project / "include"), str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)