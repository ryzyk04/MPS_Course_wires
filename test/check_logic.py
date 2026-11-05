"""Host checks of the actual firmware rules and input state machine (requires g++)."""
from pathlib import Path
import shutil
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "shared/test"))
from host_stubs import write_wire_stub

project = Path(__file__).resolve().parents[1]
out = project / ".pio" / "host-checks"
out.mkdir(parents=True, exist_ok=True)
write_wire_stub(out)
(out / "Arduino.h").write_text(r'''
#pragma once
#include <cstdint>
#include <cstdlib>
constexpr int HIGH=1, LOW=0, OUTPUT=1, INPUT_PULLUP=2, A0=14;
#define F(x) x
inline int pins[20] = {};
inline uint32_t millis() { return 0; }
inline uint32_t micros() { return 0; }
inline int analogRead(int) { return 0; }
inline int digitalRead(int p) { return pins[p]; }
inline void digitalWrite(int p, int v) { pins[p]=v; }
inline void pinMode(int,int) {}
inline void randomSeed(uint32_t n) { srand(n); }
inline long random(long n) { return rand()%n; }
inline long random(long a,long b) { return a+random(b-a); }
struct SerialStub {
  void begin(int) {}
  template<class T> void print(T) {}
  template<class T> void println(T) {}
  void println() {}
};
inline SerialStub Serial;
''', encoding="utf-8")
(out / "Adafruit_NeoPixel.h").write_text(r'''
#pragma once
#include <cstdint>
constexpr int NEO_GRB=0, NEO_KHZ800=0;
struct Adafruit_NeoPixel {
  uint32_t pixels[8]={};
  Adafruit_NeoPixel(int,int,int) {}
  void begin() {}
  void setBrightness(int) {}
  void setPixelColor(int n,uint32_t color) {pixels[n]=color;}
  void clear() {for(auto &pixel:pixels)pixel=0;}
  void show() {}
  uint32_t Color(uint8_t r,uint8_t g,uint8_t b) { return (uint32_t(r)<<16)|(uint32_t(g)<<8)|b; }
};
''', encoding="utf-8")
(out / "SoftwareSerial.h").write_text(r'''
#pragma once
#include <string>
#include <cstdint>
struct SoftwareSerial {
  std::string rx,tx;
  SoftwareSerial(int,int) {}
  void begin(uint32_t) {}
  int available() {return int(rx.size());}
  int read() {char c=rx[0];rx.erase(0,1);return static_cast<unsigned char>(c);}
  void print(const char *s) {tx+=s;}
  void print(char c) {tx+=c;}
  void print(uint32_t n) {tx+=std::to_string(n);}
  void write(char c) {tx+=c;}
  void println() {tx+='\n';}
  template<class T> void println(T value) {print(value);tx+='\n';}
};
''', encoding="utf-8")
source = (project / "src" / "main.cpp").read_text(encoding="utf-8")
(out / "firmware.cpp").write_text(source, encoding="utf-8")
(out / "check.cpp").write_text(r'''

#include "firmware.cpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
void fixture(Difficulty level, int seed, uint8_t mask, std::initializer_list<uint8_t> cs) {
  gameDifficulty=level; activeMask=mask; count=0;
  for (int i=3;i>=0;--i) { digits[i]=seed%10; seed/=10; }
  for (int i=0;i<5;++i) if (mask & (1<<i)) phys[count++]=i;
  int i=0; for (auto c:cs) colors[i++]=c;
  assert(i==count);
}
void resetInputs() {
  state=WAITING_FOR_WIRES; allClosed=false; cutMask=0;
  activeMask=7; targetMask=3;
  for (int i=0;i<5;++i) {
    pins[WIRE_PINS[i]]=LOW; rawOpen[i]=stableOpen[i]=false; changedAt[i]=0;
  }
  scanWires(0); scanWires(150); assert(state==PLAYING);
}
void cut(int i,uint32_t t) { pins[WIRE_PINS[i]]=HIGH; scanWires(t); scanWires(t+40); }
int main() {
  setup();assert(state==WAITING_FOR_COMMAND && count==0 && targetMask==0 && activeMask==0);
  for(auto pixel:strip.pixels)assert(pixel==0);
  scanWires(0);scanWires(1000);assert(state==WAITING_FOR_COMMAND);
  espLink.rx="E:PING\n";updateEspLink(0);
  assert(state==WAITING_FOR_COMMAND && count==0 && espLink.tx=="W:PONG\n");
  espLink.tx.clear();
  // One event per debounced press, retry until ACK; no repeats while held.
  buttonEventId=100;buttonRaw=buttonStable=false;pendingButtonId=0;
  pins[PIN_BUTTON]=LOW;scanButton(1000);
  pins[PIN_BUTTON]=HIGH;scanButton(1010);
  pins[PIN_BUTTON]=LOW;scanButton(1020);scanButton(1059);
  assert(espLink.tx.empty());scanButton(1060);
  assert(espLink.tx=="W:BUTTON:101\n" && pendingButtonId==101);
  scanButton(1500);assert(buttonEventId==101);
  updateEspLink(1559);assert(espLink.tx=="W:BUTTON:101\n");
  updateEspLink(1560);assert(espLink.tx=="W:BUTTON:101\nW:BUTTON:101\n");
  espLink.rx="E:ACK:100\n";updateEspLink(1561);assert(pendingButtonId==101);
  espLink.rx="E:ACK:101\r\n";updateEspLink(1562);assert(pendingButtonId==0);
  espLink.tx.clear();updateEspLink(2500);assert(espLink.tx.empty());
  pins[PIN_BUTTON]=HIGH;scanButton(2500);scanButton(2540);
  pins[PIN_BUTTON]=LOW;scanButton(2600);scanButton(2640);
  assert(pendingButtonId==102 && espLink.tx=="W:BUTTON:102\n");
  espLink.rx="E:ACK:102\nE:PING\n";updateEspLink(2641);
  assert(pendingButtonId==0 && espLink.tx=="W:BUTTON:102\nW:PONG\n");
  espLink.tx.clear();espLink.rx=std::string(60,'x')+"E:PING\n";
  updateEspLink(2642);updateEspLink(2643);assert(espLink.tx.empty());
  espLink.rx="E:PING\n";updateEspLink(2644);assert(espLink.tx=="W:PONG\n");
  // Debouncing and retransmission tolerate millis() wrapping.
  pendingButtonId=0;buttonRaw=buttonStable=false;espLink.tx.clear();
  pins[PIN_BUTTON]=LOW;scanButton(UINT32_MAX-19);scanButton(19);
  assert(pendingButtonId==0);scanButton(20);assert(pendingButtonId==103);
  espLink.tx.clear();updateEspLink(519);assert(espLink.tx.empty());
  updateEspLink(520);assert(espLink.tx=="W:BUTTON:103\n");
  pendingButtonId=0;
  // A 20 ms release used to be missed by the symmetric 40 ms debounce.
  pins[PIN_BUTTON]=HIGH;scanButton(1000);scanButton(1010);
  pins[PIN_BUTTON]=LOW;scanButton(1020);scanButton(1060);
  assert(buttonEventId==104 && pendingButtonId==104);
  // Every new press has its own ID even after solving/failing and without ACKs.
  for(int mode=WAITING_FOR_WIRES;mode<=FAILED;++mode) {
    state=State(mode);
    for(int press=0;press<5;++press) {
      uint32_t t=2000+mode*1000+press*150;
      pins[PIN_BUTTON]=HIGH;scanButton(t);scanButton(t+10);
      pins[PIN_BUTTON]=LOW;scanButton(t+20);
      uint32_t previous=buttonEventId;
      espLink.tx.clear();scanButton(t+60);
      assert(buttonEventId==previous+1 && pendingButtonId==buttonEventId);
      assert(espLink.tx=="W:BUTTON:"+std::to_string(buttonEventId)+"\n");
      scanButton(t+100);assert(buttonEventId==previous+1);
      assert(state==State(mode));
    }
  }
  pendingButtonId=0;
  fixture(EASY,4173,21,{COLOR_GREEN,COLOR_BLUE,COLOR_WHITE}); assert(chooseTargets()==4); // first rule beats white
  fixture(EASY,4172,31,{COLOR_WHITE,COLOR_RED,COLOR_WHITE,COLOR_GREEN,COLOR_WHITE}); assert(chooseTargets()==17);
  fixture(MEDIUM,1234,27,{COLOR_PINK,COLOR_RED,COLOR_BLUE,COLOR_GREEN}); assert(chooseTargets()==10);
  fixture(MEDIUM,1234,31,{COLOR_PINK,COLOR_RED,COLOR_ORANGE,COLOR_YELLOW,COLOR_WHITE}); assert(chooseTargets()==16);
  fixture(HARD,1234,14,{COLOR_RED,COLOR_RED,COLOR_RED}); assert(chooseTargets()==4); // physical extremes inactive
  fixture(HARD,1235,21,{COLOR_BLUE,COLOR_GREEN,COLOR_BLUE}); assert(chooseTargets()==4); // unique colour
  fixture(HARD,1235,27,{COLOR_BLUE,COLOR_RED,COLOR_GREEN,COLOR_YELLOW}); assert(chooseTargets()==10);
  fixture(HARD,1234,31,{COLOR_RED,COLOR_BLUE,COLOR_RED,COLOR_BLUE,COLOR_RED}); assert(chooseTargets()==10);
  fixture(HARD,1234,31,{COLOR_RED,COLOR_GREEN,COLOR_WHITE,COLOR_BLUE,COLOR_YELLOW}); assert(chooseTargets()==4);
  // Every colour layout and active mask for representative seed conditions:
  // answers must always select one or two active physical wires.
  const int seeds[]={0,1234,9876,2357,4173,6098,1111};
  unsigned cases=0;
  for (int level=0;level<3;++level) for (int seed:seeds) for (int mask=1;mask<32;++mask) {
    int n=__builtin_popcount(unsigned(mask)); if(n<3) continue;

    gameDifficulty=Difficulty(level); activeMask=mask; count=n;
    int s=seed; for(int i=3;i>=0;--i) {digits[i]=s%10;s/=10;}
    int pos=0;for(int i=0;i<5;++i)if(mask&(1<<i))phys[pos++]=i;
    int layouts=1;for(int i=0;i<n;++i)layouts*=COLOR_COUNT;
    for(int layout=0;layout<layouts;++layout) {
      int v=layout;for(int i=0;i<n;++i){colors[i]=v%COLOR_COUNT;v/=COLOR_COUNT;}
      uint8_t result=chooseTargets();
      assert((result & ~mask)==0);
      int selected=__builtin_popcount(unsigned(result)); assert(selected>=1 && selected<=2);
      ++cases;
    }

  }

  resetInputs(); cut(0,200); assert(state==PLAYING); cut(1,300); assert(state==SOLVED);
  resetInputs(); cut(1,200); cut(0,300); assert(state==SOLVED);
  resetInputs(); cut(2,200); assert(state==FAILED);
  pins[WIRE_PINS[2]]=LOW;scanWires(300);scanWires(340);assert(state==FAILED);
  resetInputs(); cut(4,200); assert(state==PLAYING && cutMask==0);
  resetInputs(); pins[WIRE_PINS[0]]=HIGH;scanWires(200);
  pins[WIRE_PINS[0]]=LOW;scanWires(220);scanWires(270);assert(cutMask==0);
  resetInputs();for(int i=0;i<3;++i)pins[WIRE_PINS[i]]=HIGH;
  scanWires(200);scanWires(240);assert(state==FAILED);
  resetInputs();cut(0,200);pins[WIRE_PINS[1]]=HIGH;scanWires(300);
  pins[WIRE_PINS[2]]=HIGH;scanWires(330);scanWires(340);assert(state==PLAYING);
  scanWires(370);assert(state==FAILED);
  resetInputs();state=WAITING_FOR_WIRES;allClosed=false;
  pins[WIRE_PINS[0]]=HIGH;scanWires(200);scanWires(400);assert(state==WAITING_FOR_WIRES);
  pins[WIRE_PINS[0]]=LOW;scanWires(500);scanWires(649);assert(state==WAITING_FOR_WIRES);
  scanWires(650);assert(state==PLAYING);
  // A new command generates and resets; duplicate delivery only repeats ACK.
  state=SOLVED;cutMask=31;allClosed=true;
  for(auto pin:WIRE_PINS)pins[pin]=HIGH;
  espLink.tx.clear();handleEspLine("E:GENERATE:4294967297:42:2");
  assert(lastGenerationId==4294967297ULL && state==WAITING_FOR_WIRES && cutMask==0 && !allClosed);
  assert(count>=3 && count<=5 && targetMask!=0);
  assert(gameSeed==42 && gameDifficulty==HARD && digits[0]==0 && digits[1]==0 && digits[2]==4 && digits[3]==2);
  assert(espLink.tx=="W:GENERATED:4294967297\n");
  scanWires(1000);assert(state==WAITING_FOR_WIRES);
  state=FAILED;cutMask=7;uint16_t savedSeed=gameSeed;uint8_t savedTargets=targetMask;
  espLink.tx.clear();handleEspLine("E:GENERATE:4294967297:42:2");
  assert(state==FAILED && cutMask==7 && gameSeed==savedSeed && targetMask==savedTargets);
  assert(espLink.tx=="W:GENERATED:4294967297\n");
  handleEspLine("E:GENERATE:4294967298:9999:1");
  assert(lastGenerationId==4294967298ULL && state==WAITING_FOR_WIRES && cutMask==0);
  assert(gameSeed==9999 && gameDifficulty==MEDIUM);
  for(auto pin:WIRE_PINS)pins[pin]=LOW;
  scanWires(1100);scanWires(1249);assert(state==WAITING_FOR_WIRES);
  scanWires(1250);assert(state==PLAYING);
  for(const char *bad : {"E:GENERATE:0:42:0","E:GENERATE:-1:42:0","E:GENERATE:18446744073709551616:42:0","E:GENERATE:12x:42:0", "E:GENERATE:5:10000:0", "E:GENERATE:5:-1:0", "E:GENERATE:5:42:3", "E:GENERATE:5:42", "E:GENERATE:5", "E:GENERATE:5:42:0:1"}) {
    espLink.tx.clear();handleEspLine(bad);
    assert(state==PLAYING && lastGenerationId==4294967298ULL && espLink.tx.empty());
  }
  handleEspLine("E:GENERATE:4294967299:0:0");assert(gameSeed==0 && gameDifficulty==EASY);
  uint8_t zeroColors[5],zeroMask=activeMask,zeroTargets=targetMask,zeroCount=count;
  for(int i=0;i<5;++i)zeroColors[i]=colors[i];
  handleEspLine("E:GENERATE:4294967300:0:0");
  assert(gameSeed==0 && gameDifficulty==EASY && activeMask==zeroMask && targetMask==zeroTargets && count==zeroCount);
  for(int i=0;i<count;++i)assert(colors[i]==zeroColors[i]);
  // Report the result once on transition, retry until ACK, and clear it on new game.
  resetInputs();lastGenerationId=501;pendingResultId=0;espLink.tx.clear();
  cut(0,200);assert(state==PLAYING && espLink.tx.empty());
  cut(1,300);assert(state==SOLVED && pendingResultId==501 && pendingVictory);
  assert(espLink.tx=="W:SOLVED:501\n");
  scanWires(600);assert(espLink.tx=="W:SOLVED:501\n");
  updateEspLink(839);assert(espLink.tx=="W:SOLVED:501\n");
  updateEspLink(840);assert(espLink.tx=="W:SOLVED:501\nW:SOLVED:501\n");
  handleEspLine("E:RESULTACK:500");assert(pendingResultId==501);
  handleEspLine("E:RESULTACK:501");assert(pendingResultId==0);
  espLink.tx.clear();updateEspLink(2000);assert(espLink.tx.empty());
  resetInputs();lastGenerationId=502;espLink.tx.clear();cut(2,200);
  assert(state==FAILED && pendingResultId==502 && !pendingVictory && espLink.tx=="W:FAILED:502\n");
  scanWires(300);assert(espLink.tx=="W:FAILED:502\n");
  handleEspLine("E:GENERATE:503:42:1");assert(pendingResultId==0 && state==WAITING_FOR_WIRES);
  printf("PASS: %u rule invariant cases, rule fixtures and input-state checks\n",cases);
  return 0;
}
''', encoding="utf-8")
compiler = shutil.which("g++")
if not compiler:
    raise SystemExit("g++ is required for host checks")
exe = out / "check.exe"
subprocess.run([compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-I", str(out), "-I", str(project / "src"), str(out / "check.cpp"), "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)
