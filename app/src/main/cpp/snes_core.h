// Retro Game SNES — núcleo do Super Nintendo em C++.
// Tradução fiel do núcleo em JavaScript (createCore) do Retro Game SNES: mesma lógica,
// mesma ordem de operações e a mesma aritmética de inteiros de 32 bits, para que os dois
// produzam exatamente a mesma imagem e o mesmo som quadro a quadro.
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>

class SnesCore {
public:
  static constexpr int W = 256, H = 224;

  SnesCore();

  // ---- API (equivalente à do núcleo em JS) ----
  bool loadRom(const uint8_t* data, size_t len);
  void reset();
  void runFrame();                       // aplica as trapaças de RAM e roda um quadro
  void setKeys(int mask) { pad1 = mask & 0xFFF0; }
  void setKeys2(int mask) { pad2 = mask & 0xFFF0; }
  int setCheats(const std::vector<std::string>& list);
  std::vector<uint8_t> getSave() const { return sram; }
  void setSave(const uint8_t* data, size_t len);
  int saveType() const { return hasBattery && !sram.empty() ? 1 : 0; }
  const std::string& chipWarning() const { return chipWarning_; }
  void setColorCorrection(bool on) { colorFix = on; lutReady = false; }
  bool takeSaveDirty() { bool d = saveDirty; saveDirty = false; return d; }

  // som: amostras de 32 kHz saem por aqui (quando ligado)
  using AudioSink = void (*)(void* user, int16_t l, int16_t r);
  void setAudio(AudioSink sink, void* user) { audioSink = sink; audioUser = user; }
  void setAudioEnabled(bool on) { audioOn = on; }

  // estado completo (formato com nomes: cada campo é "nome + bytes")
  std::vector<uint8_t> getState();
  bool setState(const uint8_t* data, size_t len);
  uint32_t stateHash();

  const uint32_t* framePtr() const { return frame; }
  uint32_t frame[W * H];

  // usado só nos testes
  int dbgPC() const { return (PB << 16) | PC; }
  int dbgSPC() const { return sPC; }
  const uint8_t* dbgWram() const { return wram; }
  const uint16_t* dbgVram() const { return vram; }
  const uint16_t* dbgCgram() const { return cgram; }
  const uint8_t* dbgAram() const { return aram; }

private:
  template <class Ar> void visit(Ar& ar);

  // ============================ Cartridge ============================
  std::vector<uint8_t> rom;
  int romSize = 0x8000, mapMode = 0;
  std::vector<uint8_t> sram;
  int sramMask = 0;
  bool hasBattery = false;
  bool saveDirty = false;
  std::string chipWarning_;
  uint8_t wram[0x20000];
  int wramAddr = 0;
  int romSpeed = 8, memsel = 0;
  int hdr = 0x7FC0;
  int headerScore(int off);
  void loadCart(const uint8_t* b, size_t len);
  int romIndex(int bank, int addr);
  inline int romAt(int i) const { return (i >= 0 && i < (int)rom.size()) ? rom[i] : -1; }

  // ============================ Timing ============================
  int cyc = 0;
  double master = 0;
  int vcount = 0;
  int frameDone = 0, inVblank = 0;

  // ============================ CPU-side I/O ============================
  int nmitimen = 0, nmiFlag = 0, timeup = 0, htime = 0x1FF, vtime = 0x1FF, wrio = 0xFF;
  int mulA = 0xFF, mulB = 0, divA = 0xFFFF, rdmpy = 0, rddiv = 0, hdmaen = 0;
  int pad1 = 0, pad2 = 0, padLatch = 0, padShift = 0, padShift2 = 0, joyAuto = 0, joyAuto2 = 0, hvbjoyBusy = 0;
  int irqLine = 0, nmiPending = 0;
  int openBus = 0;
  int readIO(int addr);
  void writeIO(int addr, int v);

  // ============================ Bus ============================
  int cost(int bank, int addr);
  int busRead(int a);
  void busWrite(int a, int v);
  std::unordered_map<int, int> cheatPatch;
  bool cheatOn = false;
  std::vector<std::pair<int, int>> ramCheats;
  void applyRamCheats();
  int rd(int a);
  void wr(int a, int v);
  int bRead(int o);
  void bWrite(int o, int v);

  // ============================ DMA / HDMA ============================
  uint8_t dmaRegs[0x80];
  struct Hdma { int addr = 0, ind = 0, lines = 0, doTransfer = 0, done = 1; } hdmaState[8];
  int dmaRead(int a);
  void dmaWrite(int a, int v) { dmaRegs[a & 0x7F] = (uint8_t)v; }
  bool aBusOk(int a);
  void runGDMA(int mask);
  void hdmaInit();
  void hdmaReload(int ch);
  void hdmaRun();

  // ============================ CPU (65816) ============================
  int A = 0, X = 0, Y = 0, S = 0x1FF, D = 0, DB = 0, PB = 0, PC = 0, E = 1;
  int fN = 0, fV = 0, fM = 1, fX = 1, fD = 0, fI = 1, fZ = 0, fC = 0;
  int waiting = 0, stopped = 0;
  int w0 = 0;
  inline void io() { cyc += 6; }
  int getP() const { return (fN << 7) | (fV << 6) | (fM << 5) | (fX << 4) | (fD << 3) | (fI << 2) | (fZ << 1) | fC; }
  void setP(int v);
  int fetch();
  int fetch16();
  int fetch24();
  void push8(int v);
  int pull8();
  void push16(int v);
  int pull16();
  void pushN(int v);
  int pullN();
  void fixS() { if (E) S = 0x100 | (S & 0xFF); }
  int rd16b0(int a);
  inline void setNZ8(int v) { fN = (v >> 7) & 1; fZ = (v & 0xFF) == 0 ? 1 : 0; }
  inline void setNZ16(int v) { fN = (v >> 15) & 1; fZ = (v & 0xFFFF) == 0 ? 1 : 0; }
  void dpBase() { if (D & 0xFF) io(); }
  int aDp(); int aDpX(); int aDpY(); int aAbs(); int aAbsX(bool w); int aAbsY(bool w);
  int aLong(); int aLongX(); int ptrDp(int d); int aInd(); int aIndX(); int aIndY(bool w);
  int aIndL(); int aIndLY(); int aSr(); int aSrY();
  inline int nxt(int a) const { return w0 ? (a + 1) & 0xFFFF : (a + 1) & 0xFFFFFF; }
  int rdM(int a); int rdX(int a); void wrM(int a, int v); void wrX(int a, int v);
  int immM() { return fM ? fetch() : fetch16(); }
  int immX() { return fX ? fetch() : fetch16(); }
  void bcd(int a, int b, int c, int nib, bool sub, int& r, int& cOut, int& pre);
  void ADC(int v); void SBC(int v); void CMPw(int reg, int v, int eight);
  void setA(int v); void setXr(int v); void setYr(int v);
  void BIT(int v, bool imm);
  typedef int (SnesCore::*AluFn)(int);
  void rmw(int a, AluFn f);
  int ASLf(int v); int LSRf(int v); int ROLf(int v); int RORf(int v); int INCf(int v); int DECf(int v);
  void accOp(AluFn f);
  void TSB(int a); void TRB(int a);
  void branch(bool cond);
  void interrupt(int nativeVec, int emuVec, bool isBrk);
  void xce();
  void blockMove(int step);
  void step();
  void aluOp(int k, int a);
  void step2(int op);

  // ============================ PPU ============================
  uint16_t vram[0x8000];
  uint16_t cgram[256];
  uint8_t oam[544];
  int inidisp = 0x80, obsel = 0, oamAddr = 0, oamReload = 0, oamLatch = 0, oamPrio = 0, bgmode = 0, mosaic = 0;
  uint8_t bgsc[4] = {0, 0, 0, 0}, bgnba[2] = {0, 0};
  uint16_t hofs[4] = {0, 0, 0, 0}, vofs[4] = {0, 0, 0, 0};
  int scrollPrev = 0, scrollPrev2 = 0, m7prev = 0;
  int vmain = 0, vmadd = 0, vramLatch = 0, m7sel = 0, m7a = 0, m7b = 0, m7c = 0, m7d = 0, m7x = 0, m7y = 0, m7hofs = 0, m7vofs = 0;
  int cgadd = 0, cgLatch = -1, w12sel = 0, w34sel = 0, wobjsel = 0, wh0 = 0, wh1 = 0, wh2 = 0, wh3 = 0, wbglog = 0, wobjlog = 0;
  int tm = 0, ts = 0, tmw = 0, tsw = 0, cgwsel = 0, cgadsub = 0, fixedColor = 0, setini = 0;
  int mpy = 0, latchH = 0, latchV = 0, hToggle = 0, vToggle = 0, cgReadToggle = 0, latched = 0;
  int vramAddr();
  bool vramOpen() const { return (inidisp & 0x80) || vcount < 1 || vcount > 224; }
  void ppuWrite(int o, int v);
  int ppuRead(int o);
  int32_t bgCol[4][W];
  int32_t objCol[W];
  uint8_t objPrio[W], objPal[W];
  uint8_t winBuf[6][W];
  int32_t optH[66], optV[66];
  int bg3Entry(int hx, int vy);
  void buildOpt(int i, int mode);
  void renderBG(int i, int y, int32_t* out, int bpp);
  void renderMode7(int y, int32_t* out, int32_t* out2);
  void renderObj(int y);
  void windowFor(int layer, uint8_t* out);
  std::vector<uint32_t> lutB;  // 16 * 0x8000
  bool lutReady = false, colorFix = false;
  void buildLuts();
  void renderLine(int y);

  // ============================ APU ============================
  uint8_t aram[0x10000];
  uint8_t apuIn[4] = {0, 0, 0, 0}, apuOut[4] = {0, 0, 0, 0};
  int sA = 0, sX = 0, sY = 0, sSP = 0xEF, sPC = 0, sPSW = 0x02;
  double spcCycles = 0, spcTarget = 0;
  int spcHalt = 0;
  uint8_t timerDiv[3] = {0, 0, 0}, timerOut[3] = {0, 0, 0}, timerCnt[3] = {0, 0, 0};
  uint16_t timerStage[3] = {0, 0, 0};
  int spcCtl = 0x80, dspAddr = 0;
  struct Ipl { int on = 1, state = 0, expect = 0, echo = 0, addr = 0, pending = 0; double at = 0; } ipl;
  void iplReset();
  void iplPortWrite(int port);
  void iplProcess();
  void apuCatchUp();
  int sRead(int a);
  void sWrite(int a, int v);
  int dspCycle = 0;
  void spcTick(int c);
  int sf();
  int sf16();
  int dpg() const;
  int dpA(int o) const { return dpg() | (o & 0xFF); }
  int r16dp(int o);
  void spush(int v) { aram[0x100 | sSP] = (uint8_t)v; sSP = (sSP - 1) & 0xFF; }
  int spop() { sSP = (sSP + 1) & 0xFF; return aram[0x100 | sSP]; }
  int nz(int v);
  int sadc(int a, int b);
  int ssbc(int a, int b) { return sadc(a, (~b) & 0xFF); }
  void scmp(int a, int b);
  int sop(int k, int a, int b);
  int sbranch(bool c);
  int sasl(int v); int slsr(int v); int srol(int v); int sror(int v);
  void bitAddr(int& a, int& b);
  int spcStep();

  // ----- S-DSP -----
  uint8_t dsp[128];
  struct Voice {
    int on = 0, addr = 0, loopAddr = 0;
    int16_t buf[16] = {0};
    int bufPos = 16, hist1 = 0, hist2 = 0, prev = 0, cur = 0, old = 0, older = 0, counter = 0, env = 0, mode = 0, rateCnt = 0, end = 0, out = 0, konDelay = 0;
  } voices[8];
  int noise = 0x4000, noiseCnt = 0, endx = 0, echoPos = 0;
  int16_t firL[8] = {0}, firR[8] = {0};
  int firPos = 0;
  AudioSink audioSink = nullptr;
  void* audioUser = nullptr;
  bool audioOn = true;
  int dspRead(int a);
  void dspWrite(int a, int v);
  void keyOn(int i);
  void decodeBlock(Voice& vc, int i);
  void envStep(Voice& vc, int i);
  void dspSample();

  // ============================ Run loop ============================
  int irqPoint();
  void runLine();
  void runFrameInner();
};
