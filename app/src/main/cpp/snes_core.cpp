// Retro Game SNES — núcleo em C++ (tradução fiel do núcleo em JavaScript).
// Regras seguidas na tradução:
//  * toda leitura de memória com efeito colateral acontece na mesma ordem do JS
//    (em C++ "rd(a) | (rd(b) << 8)" não tem ordem garantida, então cada leitura vira uma variável);
//  * contas de bits usam int de 32 bits com complemento de 2 (C++20), igual aos operadores do JS;
//  * contas fracionárias (tempo do processador de som) usam double, igual ao Number do JS.
#include "snes_core.h"
#include <cstring>
#include <cmath>
#include <algorithm>

#include "smooth_table.inc"

static const double SPC_RATIO = 1024000.0 / 21477272.0;
static inline int s8(int v) { return (int32_t)((uint32_t)v << 24) >> 24; }
static inline int s13(int v) { return (int32_t)((uint32_t)v << 19) >> 19; }
static inline int s16(int v) { return (int32_t)((uint32_t)v << 16) >> 16; }
static inline int clamp16(int v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }

SnesCore::SnesCore() {
  std::memset(frame, 0, sizeof frame);
  std::memset(wram, 0, sizeof wram);
  std::memset(dmaRegs, 0xFF, sizeof dmaRegs);
  std::memset(vram, 0, sizeof vram);
  std::memset(cgram, 0, sizeof cgram);
  std::memset(oam, 0, sizeof oam);
  std::memset(bgCol, 0, sizeof bgCol);
  std::memset(objCol, 0, sizeof objCol);
  std::memset(objPrio, 0, sizeof objPrio);
  std::memset(objPal, 0, sizeof objPal);
  std::memset(winBuf, 0, sizeof winBuf);
  std::memset(optH, 0, sizeof optH);
  std::memset(optV, 0, sizeof optV);
  std::memset(aram, 0, sizeof aram);
  std::memset(dsp, 0, sizeof dsp);
  rom.assign(0x8000, 0);
  lutB.assign(16 * 0x8000, 0);
}

/* ============================ Cartridge ============================ */
int SnesCore::headerScore(int off) {
  if (off + 0x40 > (int)rom.size()) return -100;
  int s = 0;
  const int mode = rom[off + 0x15], type = rom[off + 0x16], csum = rom[off + 0x1E] | (rom[off + 0x1F] << 8), comp = rom[off + 0x1C] | (rom[off + 0x1D] << 8);
  const int reset = rom[off + 0x3C] | (rom[off + 0x3D] << 8);
  if ((csum ^ comp) == 0xFFFF) s += 4;
  if (reset >= 0x8000) s += 2; else s -= 4;
  if (off == 0x7FC0 && (mode & 0x0F) == 0) s += 2;
  if (off == 0xFFC0 && (mode & 0x0F) == 1) s += 2;
  if (off == 0x40FFC0 && (mode & 0x0F) == 5) s += 3;
  if (type < 0x08 || (type >= 0x10 && type < 0x40)) s += 1;
  int printable = 0;
  for (int i = 0; i < 21; i++) { const int c = rom[off + i]; if (c >= 0x20 && c < 0x7F) printable++; }
  if (printable > 15) s += 2;
  const int op = romAt((off & ~0x7FFF) + (reset & 0x7FFF));  // fora do arquivo = "undefined" no JS
  static const int OPS[] = {0x78, 0x18, 0x38, 0x9C, 0x4C, 0x5C, 0xC2, 0xE2, 0xAD, 0xAE, 0xAC, 0xAF, 0xA9, 0xA2, 0xA0, 0x20, 0x22};
  for (int o : OPS) if (op == o) { s += 2; break; }
  return s;
}

void SnesCore::loadCart(const uint8_t* b, size_t len) {
  if ((len & 0x3FF) == 0x200) { b += 0x200; len -= 0x200; }  // copier header
  romSize = std::max<int>(0x8000, (int)len);
  rom.assign(romSize, 0);
  if (len) std::memcpy(rom.data(), b, len);
  const int lo = headerScore(0x7FC0), hi = headerScore(0xFFC0), ex = (int)rom.size() > 0x410000 ? headerScore(0x40FFC0) : -100;
  if (ex > hi && ex > lo) { mapMode = 2; hdr = 0x40FFC0; }
  else if (hi > lo) { mapMode = 1; hdr = 0xFFC0; }
  else { mapMode = 0; hdr = 0x7FC0; }
  const int sramK = rom[hdr + 0x18], type = rom[hdr + 0x16];
  const int size = (sramK > 0 && sramK <= 8) ? (0x400 << sramK) : 0;
  static const int BAT[] = {0x02, 0x05, 0x06, 0x09, 0x0A, 0x15, 0x1A, 0x25, 0x32, 0x35, 0x36, 0x45, 0x55, 0xE5, 0xF5, 0xF6, 0xF9};
  hasBattery = size > 0;
  for (int t : BAT) if (type == t) hasBattery = true;
  sram.assign(size, 0xFF);
  sramMask = size ? size - 1 : 0;
  const int chip = type >> 4;
  const bool coproc = type >= 0x03 && (type & 0x0F) >= 0x03;
  chipWarning_.clear();
  if (coproc) {
    if (chip == 1) chipWarning_ = "Super FX";
    else if (chip == 3) chipWarning_ = "SA-1";
    else if (chip == 0) chipWarning_ = "DSP";
    else if (chip == 4) chipWarning_ = "S-DD1";
    else if (chip == 5) chipWarning_ = "S-RTC";
    else chipWarning_ = "chip especial";
  }
}

int SnesCore::romIndex(int bank, int addr) {
  int o;
  if (mapMode == 0) o = ((bank & 0x7F) << 15) | (addr & 0x7FFF);
  else if (mapMode == 1) o = ((bank & 0x3F) << 16) | addr;
  else o = ((bank & 0x80) ? 0 : 0x400000) | ((bank & 0x3F) << 16) | addr;
  if (o >= romSize) {  // espelha como o hardware faz em tamanhos quebrados
    int p = 1;
    while (p < romSize) p <<= 1;
    o &= p - 1;
    if (o >= romSize) o -= (p >> 1);
    if (o < 0 || o >= romSize) o %= romSize;
  }
  return o;
}

/* ============================ CPU-side I/O ============================ */
int SnesCore::readIO(int addr) {
  switch (addr) {
    case 0x4016: { const int v = (padShift & 0x8000) ? 1 : 0; padShift = ((padShift << 1) | 1) & 0xFFFF; return (openBus & 0xFC) | v; }
    case 0x4017: { const int v = (padShift2 & 0x8000) ? 1 : 0; padShift2 = ((padShift2 << 1) | 1) & 0xFFFF; return (openBus & 0xE0) | 0x1C | v; }
    case 0x4210: { const int v = nmiFlag | 0x02 | (openBus & 0x70); nmiFlag = 0; return v; }
    case 0x4211: { const int v = timeup | (openBus & 0x7F); timeup = 0; irqLine = 0; return v; }
    case 0x4212: { const int h = cyc; return (inVblank ? 0x80 : 0) | ((h < 4 || h >= 1096) ? 0x40 : 0) | (hvbjoyBusy ? 1 : 0) | (openBus & 0x3E); }
    case 0x4213: return wrio;
    case 0x4214: return rddiv & 0xFF;
    case 0x4215: return rddiv >> 8;
    case 0x4216: return rdmpy & 0xFF;
    case 0x4217: return rdmpy >> 8;
    case 0x4218: return joyAuto & 0xFF;
    case 0x4219: return joyAuto >> 8;
    case 0x421A: return joyAuto2 & 0xFF;
    case 0x421B: return joyAuto2 >> 8;
    case 0x421C: case 0x421D: case 0x421E: case 0x421F: return 0;
  }
  if (addr >= 0x4300 && addr < 0x4380) return dmaRead(addr);
  return openBus;
}

void SnesCore::writeIO(int addr, int v) {
  switch (addr) {
    case 0x4016: {
      const int nl = v & 1;
      if (padLatch && !nl) { padShift = pad1; padShift2 = pad2; }
      if (nl) { padShift = pad1; padShift2 = pad2; }
      padLatch = nl;
      return;
    }
    case 0x4200: {
      const int old = nmitimen;
      nmitimen = v;
      if (!(v & 0x30)) { irqLine = 0; timeup = 0; }
      if (!(old & 0x80) && (v & 0x80) && nmiFlag) nmiPending = 1;
      return;
    }
    case 0x4201: wrio = v; return;
    case 0x4202: mulA = v; return;
    case 0x4203: mulB = v; rdmpy = mulA * mulB; rddiv = mulB; return;
    case 0x4204: divA = (divA & 0xFF00) | v; return;
    case 0x4205: divA = (divA & 0x00FF) | (v << 8); return;
    case 0x4206: if (v == 0) { rddiv = 0xFFFF; rdmpy = divA; } else { rddiv = divA / v; rdmpy = divA % v; } return;
    case 0x4207: htime = (htime & 0x100) | v; return;
    case 0x4208: htime = (htime & 0xFF) | ((v & 1) << 8); return;
    case 0x4209: vtime = (vtime & 0x100) | v; return;
    case 0x420A: vtime = (vtime & 0xFF) | ((v & 1) << 8); return;
    case 0x420B: runGDMA(v); return;
    case 0x420C: hdmaen = v; return;
    case 0x420D: memsel = v & 1; romSpeed = memsel ? 6 : 8; return;
  }
  if (addr >= 0x4300 && addr < 0x4380) dmaWrite(addr, v);
}

/* ============================ Bus ============================ */
int SnesCore::cost(int bank, int addr) {
  if (bank & 0x40) {
    if (bank == 0x7E || bank == 0x7F) return 8;
    return (bank & 0x80) ? romSpeed : 8;
  }
  if (addr < 0x2000) return 8;
  if (addr < 0x4000) return 6;
  if (addr < 0x4200) return 12;
  if (addr < 0x6000) return 6;
  if (addr < 0x8000) return 8;
  return (bank & 0x80) ? romSpeed : 8;
}

int SnesCore::busRead(int a) {
  const int bank = (int)((uint32_t)a >> 16), addr = a & 0xFFFF;
  if (bank == 0x7E || bank == 0x7F) return wram[a & 0x1FFFF];
  if (!(bank & 0x40)) {  // banks 00-3F, 80-BF
    if (addr < 0x2000) return wram[addr];
    if (addr < 0x8000) {
      if (addr >= 0x2100 && addr < 0x2200) return bRead(addr & 0xFF);
      if (addr >= 0x4000 && addr < 0x4400) return readIO(addr);
      if (addr >= 0x6000 && !sram.empty() && (mapMode != 0)) {
        if ((bank & 0x7F) >= 0x20) return sram[(((bank & 0x1F) << 13) | (addr - 0x6000)) & sramMask];
      }
      return openBus;
    }
    return rom[romIndex(bank, addr)];
  }
  // banks 40-7D, C0-FF
  if (mapMode == 0) {
    if ((bank & 0x7F) >= 0x70 && addr < 0x8000) {
      if (!sram.empty()) return sram[(((bank & 0x0F) << 15) | addr) & sramMask];
      return openBus;
    }
    return rom[romIndex(bank, addr)];
  }
  return rom[romIndex(bank, addr)];
}

void SnesCore::busWrite(int a, int v) {
  const int bank = (int)((uint32_t)a >> 16), addr = a & 0xFFFF;
  if (bank == 0x7E || bank == 0x7F) { wram[a & 0x1FFFF] = (uint8_t)v; return; }
  if (!(bank & 0x40)) {
    if (addr < 0x2000) { wram[addr] = (uint8_t)v; return; }
    if (addr < 0x8000) {
      if (addr >= 0x2100 && addr < 0x2200) { bWrite(addr & 0xFF, v); return; }
      if (addr >= 0x4000 && addr < 0x4400) { writeIO(addr, v); return; }
      if (addr >= 0x6000 && !sram.empty() && mapMode != 0 && (bank & 0x7F) >= 0x20) {
        sram[(((bank & 0x1F) << 13) | (addr - 0x6000)) & sramMask] = (uint8_t)v;
        saveDirty = true;
      }
    }
    return;
  }
  if (mapMode == 0 && (bank & 0x7F) >= 0x70 && addr < 0x8000 && !sram.empty()) {
    sram[(((bank & 0x0F) << 15) | addr) & sramMask] = (uint8_t)v;
    saveDirty = true;
  }
}

int SnesCore::rd(int a) {
  a &= 0xFFFFFF;
  cyc += cost(a >> 16, a & 0xFFFF);
  int v = busRead(a);
  if (cheatOn) { auto it = cheatPatch.find(a); if (it != cheatPatch.end()) v = it->second; }
  openBus = v;
  return v;
}

void SnesCore::wr(int a, int v) {
  a &= 0xFFFFFF;
  cyc += cost(a >> 16, a & 0xFFFF);
  openBus = v & 0xFF;  // o barramento tem 8 bits (o JS antigo guardava 16 bits aqui)
  busWrite(a, v & 0xFF);
}

/* ----- B bus ($2100-$21FF) ----- */
int SnesCore::bRead(int o) {
  if (o >= 0x40 && o < 0x80) { apuCatchUp(); return apuOut[o & 3]; }
  if (o == 0x80) { const int v = wram[wramAddr]; wramAddr = (wramAddr + 1) & 0x1FFFF; return v; }
  if (o >= 0x34 && o <= 0x3F) return ppuRead(o);
  return openBus;
}

void SnesCore::bWrite(int o, int v) {
  if (o >= 0x40 && o < 0x80) { apuCatchUp(); apuIn[o & 3] = (uint8_t)v; iplPortWrite(o & 3); return; }
  if (o == 0x80) { wram[wramAddr] = (uint8_t)v; wramAddr = (wramAddr + 1) & 0x1FFFF; return; }
  if (o == 0x81) { wramAddr = (wramAddr & 0x1FF00) | v; return; }
  if (o == 0x82) { wramAddr = (wramAddr & 0x100FF) | (v << 8); return; }
  if (o == 0x83) { wramAddr = (wramAddr & 0x0FFFF) | ((v & 1) << 16); return; }
  if (o < 0x34) ppuWrite(o, v);
}

/* ============================ DMA / HDMA ============================ */
static const int UNIT_LEN[8] = {1, 2, 2, 4, 4, 4, 2, 4};
static const int UNIT[8][4] = {{0}, {0, 1}, {0, 0}, {0, 0, 1, 1}, {0, 1, 2, 3}, {0, 1, 0, 1}, {0, 0}, {0, 0, 1, 1}};

int SnesCore::dmaRead(int a) {
  const int o = a & 0x7F;
  return (o & 0x0F) < 0x0C ? dmaRegs[o] : (o & 0x0F) == 0x0F ? dmaRegs[o] : openBus;
}

bool SnesCore::aBusOk(int a) {
  const int bank = (int)((uint32_t)a >> 16), addr = a & 0xFFFF;
  return !(!(bank & 0x40) && ((addr >= 0x2100 && addr < 0x2200) || (addr >= 0x4300 && addr < 0x4380)));
}

void SnesCore::runGDMA(int mask) {
  for (int ch = 0; ch < 8; ch++) {
    if (!(mask & (1 << ch))) continue;
    const int b = ch << 4, ctl = dmaRegs[b], bAddr = dmaRegs[b + 1];
    int aAddr = dmaRegs[b + 2] | (dmaRegs[b + 3] << 8);
    const int aBank = dmaRegs[b + 4];
    int count = dmaRegs[b + 5] | (dmaRegs[b + 6] << 8);
    if (!count) count = 0x10000;
    const int* unit = UNIT[ctl & 7];
    const int ulen = UNIT_LEN[ctl & 7];
    const int stepv = (ctl & 0x08) ? 0 : (ctl & 0x10) ? -1 : 1, toA = ctl & 0x80;
    cyc += 8;
    int i = 0;
    while (count > 0) {
      const int bReg = (bAddr + unit[i & (ulen - 1)]) & 0xFF, full = (aBank << 16) | aAddr;
      if (toA) { const int v = bRead(bReg); if (aBusOk(full)) busWrite(full, v); }
      else { const int v = aBusOk(full) ? busRead(full) : openBus; bWrite(bReg, v); }
      cyc += 8;
      aAddr = (aAddr + stepv) & 0xFFFF;
      count--;
      i++;
    }
    dmaRegs[b + 2] = aAddr & 0xFF; dmaRegs[b + 3] = aAddr >> 8; dmaRegs[b + 5] = 0; dmaRegs[b + 6] = 0;
  }
}

void SnesCore::hdmaInit() {
  for (int ch = 0; ch < 8; ch++) {
    Hdma& h = hdmaState[ch];
    const int b = ch << 4;
    h.done = 1;
    if (!(hdmaen & (1 << ch))) continue;
    h.addr = dmaRegs[b + 2] | (dmaRegs[b + 3] << 8);
    h.done = 0; h.lines = 0; h.doTransfer = 0;
    hdmaReload(ch);
  }
}

void SnesCore::hdmaReload(int ch) {
  Hdma& h = hdmaState[ch];
  const int b = ch << 4, bank = dmaRegs[b + 4];
  const int lc = busRead((bank << 16) | h.addr);
  h.addr = (h.addr + 1) & 0xFFFF;
  h.lines = lc;
  if (dmaRegs[b] & 0x40) {
    const int lo = busRead((bank << 16) | h.addr);
    const int hi = busRead((bank << 16) | ((h.addr + 1) & 0xFFFF));
    h.addr = (h.addr + 2) & 0xFFFF;
    h.ind = lo | (hi << 8);
    cyc += 16;
  }
  cyc += 8;
  if (lc == 0) h.done = 1;
  h.doTransfer = 1;
  dmaRegs[b + 8] = h.addr & 0xFF; dmaRegs[b + 9] = h.addr >> 8; dmaRegs[b + 10] = (uint8_t)lc;
}

void SnesCore::hdmaRun() {
  for (int ch = 0; ch < 8; ch++) {
    Hdma& h = hdmaState[ch];
    if (h.done || !(hdmaen & (1 << ch))) continue;
    const int b = ch << 4, ctl = dmaRegs[b], bAddr = dmaRegs[b + 1];
    const int* unit = UNIT[ctl & 7];
    const int ulen = UNIT_LEN[ctl & 7];
    cyc += 8;
    if (h.doTransfer) {
      for (int i = 0; i < ulen; i++) {
        int src;
        if (ctl & 0x40) { src = (dmaRegs[b + 7] << 16) | h.ind; h.ind = (h.ind + 1) & 0xFFFF; }
        else { src = (dmaRegs[b + 4] << 16) | h.addr; h.addr = (h.addr + 1) & 0xFFFF; }
        if (ctl & 0x80) busWrite(src, bRead((bAddr + unit[i]) & 0xFF));
        else bWrite((bAddr + unit[i]) & 0xFF, busRead(src));
        cyc += 8;
      }
    }
    h.lines = (h.lines - 1) & 0xFF;
    h.doTransfer = (h.lines & 0x80) ? 1 : 0;
    if ((h.lines & 0x7F) == 0) hdmaReload(ch);
    dmaRegs[b + 10] = (uint8_t)h.lines;
  }
}

/* ============================ CPU (65816) ============================ */
void SnesCore::setP(int v) {
  fN = (v >> 7) & 1; fV = (v >> 6) & 1; fD = (v >> 3) & 1; fI = (v >> 2) & 1; fZ = (v >> 1) & 1; fC = v & 1;
  if (E) { fM = 1; fX = 1; } else { fM = (v >> 5) & 1; fX = (v >> 4) & 1; }
  if (fX) { X &= 0xFF; Y &= 0xFF; }
}
int SnesCore::fetch() { const int v = rd((PB << 16) | PC); PC = (PC + 1) & 0xFFFF; return v; }
int SnesCore::fetch16() { const int l = fetch(); const int h = fetch(); return l | (h << 8); }
int SnesCore::fetch24() { const int l = fetch16(); const int h = fetch(); return l | (h << 16); }
void SnesCore::push8(int v) { wr(S, v); S = E ? (0x100 | ((S - 1) & 0xFF)) : (S - 1) & 0xFFFF; }
int SnesCore::pull8() { S = E ? (0x100 | ((S + 1) & 0xFF)) : (S + 1) & 0xFFFF; return rd(S); }
void SnesCore::push16(int v) { push8(v >> 8); push8(v & 0xFF); }
int SnesCore::pull16() { const int l = pull8(); const int h = pull8(); return l | (h << 8); }
void SnesCore::pushN(int v) { wr(S, v); S = (S - 1) & 0xFFFF; }
int SnesCore::pullN() { S = (S + 1) & 0xFFFF; return rd(S); }
int SnesCore::rd16b0(int a) { const int l = rd(a & 0xFFFF); const int h = rd((a + 1) & 0xFFFF); return l | (h << 8); }

int SnesCore::aDp() { const int o = fetch(); dpBase(); w0 = 1; return (D + o) & 0xFFFF; }
int SnesCore::aDpX() { const int o = fetch(); dpBase(); io(); w0 = 1; return (E && !(D & 0xFF)) ? (D | ((o + X) & 0xFF)) : (D + o + X) & 0xFFFF; }
int SnesCore::aDpY() { const int o = fetch(); dpBase(); io(); w0 = 1; return (E && !(D & 0xFF)) ? (D | ((o + Y) & 0xFF)) : (D + o + Y) & 0xFFFF; }
int SnesCore::aAbs() { w0 = 0; return (DB << 16) | fetch16(); }
int SnesCore::aAbsX(bool w) { const int b = fetch16(); w0 = 0; if (w || !fX || ((b & 0xFF00) != ((b + X) & 0xFF00))) io(); return (((DB << 16) | b) + X) & 0xFFFFFF; }
int SnesCore::aAbsY(bool w) { const int b = fetch16(); w0 = 0; if (w || !fX || ((b & 0xFF00) != ((b + Y) & 0xFF00))) io(); return (((DB << 16) | b) + Y) & 0xFFFFFF; }
int SnesCore::aLong() { w0 = 0; return fetch24(); }
int SnesCore::aLongX() { w0 = 0; return (fetch24() + X) & 0xFFFFFF; }
int SnesCore::ptrDp(int d) {
  if (E && !(D & 0xFF)) { const int l = rd(d); const int h = rd((d & 0xFF00) | ((d + 1) & 0xFF)); return l | (h << 8); }
  return rd16b0(d);
}
int SnesCore::aInd() { const int d = aDp(); w0 = 0; return (DB << 16) | ptrDp(d); }
int SnesCore::aIndX() { const int d = aDpX(); w0 = 0; return (DB << 16) | ptrDp(d); }
int SnesCore::aIndY(bool w) { const int d = aDp(); const int p = ptrDp(d); w0 = 0; if (w || !fX || ((p & 0xFF00) != ((p + Y) & 0xFF00))) io(); return (((DB << 16) | p) + Y) & 0xFFFFFF; }
int SnesCore::aIndL() {
  const int d = aDp(); w0 = 0;
  const int b0 = rd(d); const int b1 = rd((d + 1) & 0xFFFF); const int b2 = rd((d + 2) & 0xFFFF);
  return b0 | (b1 << 8) | (b2 << 16);
}
int SnesCore::aIndLY() { return (aIndL() + Y) & 0xFFFFFF; }
int SnesCore::aSr() { const int o = fetch(); io(); w0 = 1; return (S + o) & 0xFFFF; }
int SnesCore::aSrY() { const int s = aSr(); const int p = rd16b0(s); io(); w0 = 0; return (((DB << 16) | p) + Y) & 0xFFFFFF; }
int SnesCore::rdM(int a) { if (fM) return rd(a); const int l = rd(a); const int h = rd(nxt(a)); return l | (h << 8); }
int SnesCore::rdX(int a) { if (fX) return rd(a); const int l = rd(a); const int h = rd(nxt(a)); return l | (h << 8); }
void SnesCore::wrM(int a, int v) { if (fM) wr(a, v); else { wr(a, v & 0xFF); wr(nxt(a), (v >> 8) & 0xFF); } }
void SnesCore::wrX(int a, int v) { if (fX) wr(a, v); else { wr(a, v & 0xFF); wr(nxt(a), (v >> 8) & 0xFF); } }

// ----- ALU
void SnesCore::bcd(int a, int b, int c, int nib, bool sub, int& rOut, int& cOut, int& preOut) {
  int r = 0, pre = 0;
  for (int i = 0; i < nib; i++) {
    int d = ((a >> (4 * i)) & 15) + ((b >> (4 * i)) & 15) + c;
    if (i == nib - 1) pre = r | (d << (4 * i));
    if (!sub) { if (d > 9) d += 6; } else { if (d <= 15) d -= 6; }
    c = d > 15 ? 1 : 0;
    r |= (d & 15) << (4 * i);
  }
  rOut = r; cOut = c; preOut = pre;
}
void SnesCore::ADC(int v) {
  if (fM) {
    const int a = A & 0xFF; int r;
    if (fD) { int res, c, pre; bcd(a, v, fC, 2, false, res, c, pre); fV = (~(a ^ v) & (a ^ pre) & 0x80) ? 1 : 0; r = res; fC = c; }
    else { r = a + v + fC; fV = (~(a ^ v) & (a ^ r) & 0x80) ? 1 : 0; fC = r > 0xFF ? 1 : 0; }
    A = (A & 0xFF00) | (r & 0xFF); setNZ8(r);
  } else {
    const int a = A; int r;
    if (fD) { int res, c, pre; bcd(a, v, fC, 4, false, res, c, pre); fV = (~(a ^ v) & (a ^ pre) & 0x8000) ? 1 : 0; r = res; fC = c; }
    else { r = a + v + fC; fV = (~(a ^ v) & (a ^ r) & 0x8000) ? 1 : 0; fC = r > 0xFFFF ? 1 : 0; }
    A = r & 0xFFFF; setNZ16(A);
  }
}
void SnesCore::SBC(int v) {
  if (fM) {
    const int a = A & 0xFF, b = (~v) & 0xFF; int r;
    if (fD) { int res, c, pre; bcd(a, b, fC, 2, true, res, c, pre); fV = (~(a ^ b) & (a ^ pre) & 0x80) ? 1 : 0; r = res; fC = c; }
    else { r = a + b + fC; fV = (~(a ^ b) & (a ^ r) & 0x80) ? 1 : 0; fC = r > 0xFF ? 1 : 0; }
    A = (A & 0xFF00) | (r & 0xFF); setNZ8(r);
  } else {
    const int a = A, b = (~v) & 0xFFFF; int r;
    if (fD) { int res, c, pre; bcd(a, b, fC, 4, true, res, c, pre); fV = (~(a ^ b) & (a ^ pre) & 0x8000) ? 1 : 0; r = res; fC = c; }
    else { r = a + b + fC; fV = (~(a ^ b) & (a ^ r) & 0x8000) ? 1 : 0; fC = r > 0xFFFF ? 1 : 0; }
    A = r & 0xFFFF; setNZ16(A);
  }
}
void SnesCore::CMPw(int reg, int v, int eight) {
  if (eight) { const int r = (reg & 0xFF) - v; fC = r >= 0 ? 1 : 0; setNZ8(r & 0xFF); }
  else { const int r = reg - v; fC = r >= 0 ? 1 : 0; setNZ16(r & 0xFFFF); }
}
void SnesCore::setA(int v) { if (fM) { A = (A & 0xFF00) | (v & 0xFF); setNZ8(v); } else { A = v & 0xFFFF; setNZ16(A); } }
void SnesCore::setXr(int v) { if (fX) { X = v & 0xFF; setNZ8(X); } else { X = v & 0xFFFF; setNZ16(X); } }
void SnesCore::setYr(int v) { if (fX) { Y = v & 0xFF; setNZ8(Y); } else { Y = v & 0xFFFF; setNZ16(Y); } }
void SnesCore::BIT(int v, bool imm) {
  if (fM) { fZ = ((A & v) & 0xFF) == 0 ? 1 : 0; if (!imm) { fN = (v >> 7) & 1; fV = (v >> 6) & 1; } }
  else { fZ = (A & v & 0xFFFF) == 0 ? 1 : 0; if (!imm) { fN = (v >> 15) & 1; fV = (v >> 14) & 1; } }
}
void SnesCore::rmw(int a, AluFn f) {
  const int v = rdM(a); io();
  const int r = (this->*f)(v);
  if (fM) { wr(a, r & 0xFF); }
  else { wr(nxt(a), (r >> 8) & 0xFF); wr(a, r & 0xFF); }
}
int SnesCore::ASLf(int v) { if (fM) { fC = (v >> 7) & 1; v = (v << 1) & 0xFF; setNZ8(v); } else { fC = (v >> 15) & 1; v = (v << 1) & 0xFFFF; setNZ16(v); } return v; }
int SnesCore::LSRf(int v) { fC = v & 1; v >>= 1; if (fM) setNZ8(v); else setNZ16(v); return v; }
int SnesCore::ROLf(int v) { const int c = fC; if (fM) { fC = (v >> 7) & 1; v = ((v << 1) | c) & 0xFF; setNZ8(v); } else { fC = (v >> 15) & 1; v = ((v << 1) | c) & 0xFFFF; setNZ16(v); } return v; }
int SnesCore::RORf(int v) { const int c = fC; fC = v & 1; if (fM) { v = (v >> 1) | (c << 7); setNZ8(v); } else { v = (v >> 1) | (c << 15); setNZ16(v); } return v; }
int SnesCore::INCf(int v) { if (fM) { v = (v + 1) & 0xFF; setNZ8(v); } else { v = (v + 1) & 0xFFFF; setNZ16(v); } return v; }
int SnesCore::DECf(int v) { if (fM) { v = (v - 1) & 0xFF; setNZ8(v); } else { v = (v - 1) & 0xFFFF; setNZ16(v); } return v; }
void SnesCore::accOp(AluFn f) { io(); if (fM) A = (A & 0xFF00) | (this->*f)(A & 0xFF); else A = (this->*f)(A); }
void SnesCore::TSB(int a) {
  const int v = rdM(a); io();
  fZ = ((A & v) & (fM ? 0xFF : 0xFFFF)) == 0 ? 1 : 0;
  const int r = v | A;
  if (fM) wr(a, r & 0xFF); else { wr(nxt(a), (r >> 8) & 0xFF); wr(a, r & 0xFF); }
}
void SnesCore::TRB(int a) {
  const int v = rdM(a); io();
  fZ = ((A & v) & (fM ? 0xFF : 0xFFFF)) == 0 ? 1 : 0;
  const int r = v & ~A;
  if (fM) wr(a, r & 0xFF); else { wr(nxt(a), (r >> 8) & 0xFF); wr(a, r & 0xFF); }
}
void SnesCore::branch(bool cond) {
  const int o = fetch();
  if (cond) { io(); const int n = (PC + s8(o)) & 0xFFFF; if (E && ((n ^ PC) & 0xFF00)) io(); PC = n; }
}
void SnesCore::interrupt(int nativeVec, int emuVec, bool isBrk) {
  if (!E) push8(PB);
  push16(PC);
  push8(E ? (getP() | (isBrk ? 0x10 : 0) | 0x20) & (isBrk ? 0xFF : 0xEF) : getP());
  fI = 1; fD = 0; PB = 0;
  const int v = E ? emuVec : nativeVec;
  const int l = rd(v); const int h = rd(v + 1);
  PC = l | (h << 8);
}
void SnesCore::xce() { const int c = fC; fC = E; E = c; if (E) { fM = 1; fX = 1; X &= 0xFF; Y &= 0xFF; S = 0x100 | (S & 0xFF); } }
void SnesCore::blockMove(int stepv) {
  const int dst = fetch(); const int src = fetch();
  DB = dst;
  const int v = rd((src << 16) | X); wr((dst << 16) | Y, v); io(); io();
  if (fX) { X = (X + stepv) & 0xFF; Y = (Y + stepv) & 0xFF; } else { X = (X + stepv) & 0xFFFF; Y = (Y + stepv) & 0xFFFF; }
  A = (A - 1) & 0xFFFF;
  if (A != 0xFFFF) PC = (PC - 3) & 0xFFFF;
}

void SnesCore::step() {
  const int op = fetch();
  switch (op) {
    case 0x09: { const int v = immM(); setA(A | v); break; }
    case 0x29: { const int v = immM(); setA(A & v); break; }
    case 0x49: { const int v = immM(); setA(A ^ v); break; }
    case 0x69: ADC(immM()); break;
    case 0x89: BIT(immM(), true); break;
    case 0xA9: setA(immM()); break;
    case 0xC9: { const int v = immM(); CMPw(A, v, fM); break; }
    case 0xE9: SBC(immM()); break;
    case 0xA2: setXr(immX()); break;
    case 0xA0: setYr(immX()); break;
    case 0xE0: { const int v = immX(); CMPw(X, v, fX); break; }
    case 0xC0: { const int v = immX(); CMPw(Y, v, fX); break; }
    default: step2(op); return;
  }
}

// memory-operand table for the 8 classic ALU ops (ORA AND EOR ADC STA LDA CMP SBC)
void SnesCore::aluOp(int k, int a) {
  switch (k) {
    case 0: { const int v = rdM(a); setA(A | v); break; }
    case 1: { const int v = rdM(a); setA(A & v); break; }
    case 2: { const int v = rdM(a); setA(A ^ v); break; }
    case 3: ADC(rdM(a)); break;
    case 4: wrM(a, A); break;
    case 5: setA(rdM(a)); break;
    case 6: { const int v = rdM(a); CMPw(A, v, fM); break; }
    default: SBC(rdM(a)); break;
  }
}

void SnesCore::step2(int op) {
  const int lo = op & 0x1F, k = op >> 5;
  if ((op & 3) == 1 || ((op & 3) == 3 && (lo == 0x03 || lo == 0x07 || lo == 0x0F || lo == 0x13 || lo == 0x17 || lo == 0x1F)) || lo == 0x12) {
    const bool st = k == 4;
    int a;
    switch (lo) {
      case 0x01: a = aIndX(); break;
      case 0x03: a = aSr(); break;
      case 0x05: a = aDp(); break;
      case 0x07: a = aIndL(); break;
      case 0x0D: a = aAbs(); break;
      case 0x0F: a = aLong(); break;
      case 0x11: a = aIndY(st); break;
      case 0x12: a = aInd(); break;
      case 0x13: a = aSrY(); break;
      case 0x15: a = aDpX(); break;
      case 0x17: a = aIndLY(); break;
      case 0x19: a = aAbsY(st); break;
      case 0x1D: a = aAbsX(st); break;
      case 0x1F: a = aLongX(); break;
      default: a = -1;
    }
    if (a >= 0) { aluOp(k, a); return; }
  }
  int v;
  switch (op) {
    // ---- shifts / inc / dec
    case 0x06: rmw(aDp(), &SnesCore::ASLf); break; case 0x16: rmw(aDpX(), &SnesCore::ASLf); break; case 0x0E: rmw(aAbs(), &SnesCore::ASLf); break; case 0x1E: rmw(aAbsX(true), &SnesCore::ASLf); break; case 0x0A: accOp(&SnesCore::ASLf); break;
    case 0x46: rmw(aDp(), &SnesCore::LSRf); break; case 0x56: rmw(aDpX(), &SnesCore::LSRf); break; case 0x4E: rmw(aAbs(), &SnesCore::LSRf); break; case 0x5E: rmw(aAbsX(true), &SnesCore::LSRf); break; case 0x4A: accOp(&SnesCore::LSRf); break;
    case 0x26: rmw(aDp(), &SnesCore::ROLf); break; case 0x36: rmw(aDpX(), &SnesCore::ROLf); break; case 0x2E: rmw(aAbs(), &SnesCore::ROLf); break; case 0x3E: rmw(aAbsX(true), &SnesCore::ROLf); break; case 0x2A: accOp(&SnesCore::ROLf); break;
    case 0x66: rmw(aDp(), &SnesCore::RORf); break; case 0x76: rmw(aDpX(), &SnesCore::RORf); break; case 0x6E: rmw(aAbs(), &SnesCore::RORf); break; case 0x7E: rmw(aAbsX(true), &SnesCore::RORf); break; case 0x6A: accOp(&SnesCore::RORf); break;
    case 0xE6: rmw(aDp(), &SnesCore::INCf); break; case 0xF6: rmw(aDpX(), &SnesCore::INCf); break; case 0xEE: rmw(aAbs(), &SnesCore::INCf); break; case 0xFE: rmw(aAbsX(true), &SnesCore::INCf); break; case 0x1A: accOp(&SnesCore::INCf); break;
    case 0xC6: rmw(aDp(), &SnesCore::DECf); break; case 0xD6: rmw(aDpX(), &SnesCore::DECf); break; case 0xCE: rmw(aAbs(), &SnesCore::DECf); break; case 0xDE: rmw(aAbsX(true), &SnesCore::DECf); break; case 0x3A: accOp(&SnesCore::DECf); break;
    case 0x04: TSB(aDp()); break; case 0x0C: TSB(aAbs()); break;
    case 0x14: TRB(aDp()); break; case 0x1C: TRB(aAbs()); break;
    // ---- BIT
    case 0x24: BIT(rdM(aDp()), false); break; case 0x2C: BIT(rdM(aAbs()), false); break;
    case 0x34: BIT(rdM(aDpX()), false); break; case 0x3C: BIT(rdM(aAbsX(false)), false); break;
    // ---- loads / stores X Y, STZ
    case 0xA6: setXr(rdX(aDp())); break; case 0xB6: setXr(rdX(aDpY())); break; case 0xAE: setXr(rdX(aAbs())); break; case 0xBE: setXr(rdX(aAbsY(false))); break;
    case 0xA4: setYr(rdX(aDp())); break; case 0xB4: setYr(rdX(aDpX())); break; case 0xAC: setYr(rdX(aAbs())); break; case 0xBC: setYr(rdX(aAbsX(false))); break;
    case 0x86: { const int a = aDp(); wrX(a, X); break; } case 0x96: { const int a = aDpY(); wrX(a, X); break; } case 0x8E: { const int a = aAbs(); wrX(a, X); break; }
    case 0x84: { const int a = aDp(); wrX(a, Y); break; } case 0x94: { const int a = aDpX(); wrX(a, Y); break; } case 0x8C: { const int a = aAbs(); wrX(a, Y); break; }
    case 0x64: wrM(aDp(), 0); break; case 0x74: wrM(aDpX(), 0); break; case 0x9C: wrM(aAbs(), 0); break; case 0x9E: wrM(aAbsX(true), 0); break;
    case 0xE4: { const int m = rdX(aDp()); CMPw(X, m, fX); break; } case 0xEC: { const int m = rdX(aAbs()); CMPw(X, m, fX); break; }
    case 0xC4: { const int m = rdX(aDp()); CMPw(Y, m, fX); break; } case 0xCC: { const int m = rdX(aAbs()); CMPw(Y, m, fX); break; }
    // ---- register inc/dec, transfers
    case 0xE8: io(); setXr(X + 1); break; case 0xC8: io(); setYr(Y + 1); break;
    case 0xCA: io(); setXr(X - 1); break; case 0x88: io(); setYr(Y - 1); break;
    case 0xAA: io(); setXr(A); break; case 0xA8: io(); setYr(A); break;
    case 0x8A: io(); setA(X); break; case 0x98: io(); setA(Y); break;
    case 0x9B: io(); setYr(X); break; case 0xBB: io(); setXr(Y); break;
    case 0xBA: io(); setXr(S); break;
    case 0x9A: io(); S = E ? (0x100 | (X & 0xFF)) : X; break;
    case 0x5B: io(); D = A; setNZ16(D); break;
    case 0x7B: io(); A = D; setNZ16(A); break;
    case 0x1B: io(); S = E ? (0x100 | (A & 0xFF)) : A; break;
    case 0x3B: io(); A = S; setNZ16(A); break;
    case 0xEB: io(); io(); A = ((A >> 8) | (A << 8)) & 0xFFFF; setNZ8(A & 0xFF); break;
    case 0xFB: io(); xce(); break;
    // ---- flags
    case 0x18: io(); fC = 0; break; case 0x38: io(); fC = 1; break;
    case 0x58: io(); fI = 0; break; case 0x78: io(); fI = 1; break;
    case 0xD8: io(); fD = 0; break; case 0xF8: io(); fD = 1; break;
    case 0xB8: io(); fV = 0; break;
    case 0xC2: v = fetch(); io(); setP(getP() & ~v); break;
    case 0xE2: v = fetch(); io(); setP(getP() | v); break;
    // ---- branches
    case 0x10: branch(!fN); break; case 0x30: branch(fN); break;
    case 0x50: branch(!fV); break; case 0x70: branch(fV); break;
    case 0x90: branch(!fC); break; case 0xB0: branch(fC); break;
    case 0xD0: branch(!fZ); break; case 0xF0: branch(fZ); break;
    case 0x80: branch(true); break;
    case 0x82: { const int o = fetch16(); io(); PC = (PC + s16(o)) & 0xFFFF; break; }
    // ---- jumps / calls
    case 0x4C: PC = fetch16(); break;
    case 0x5C: { const int t = fetch24(); PC = t & 0xFFFF; PB = t >> 16; break; }
    case 0x6C: { const int p = fetch16(); const int l = rd(p); const int h = rd((p + 1) & 0xFFFF); PC = l | (h << 8); break; }
    case 0x7C: { const int p = fetch16(); io(); const int q = (p + X) & 0xFFFF; const int l = rd((PB << 16) | q); const int h = rd((PB << 16) | ((q + 1) & 0xFFFF)); PC = l | (h << 8); break; }
    case 0xDC: { const int p = fetch16(); const int l = rd(p); const int h = rd((p + 1) & 0xFFFF); PC = l | (h << 8); PB = rd((p + 2) & 0xFFFF); break; }
    case 0x20: { const int t = fetch16(); io(); push16((PC - 1) & 0xFFFF); PC = t; break; }
    case 0xFC: {
      const int lo2 = fetch(); pushN(PC >> 8); pushN(PC & 0xFF); const int hi = fetch(); io();
      const int q = ((lo2 | (hi << 8)) + X) & 0xFFFF;
      const int l = rd((PB << 16) | q); const int h = rd((PB << 16) | ((q + 1) & 0xFFFF));
      PC = l | (h << 8); fixS(); break;
    }
    case 0x22: { const int lo2 = fetch16(); pushN(PB); io(); const int hi = fetch(); const int ret = (PC - 1) & 0xFFFF; pushN(ret >> 8); pushN(ret & 0xFF); PC = lo2; PB = hi; fixS(); break; }
    case 0x60: io(); io(); PC = (pull16() + 1) & 0xFFFF; io(); break;
    case 0x6B: { io(); io(); const int l = pullN(); const int h = pullN(); PB = pullN(); PC = ((l | (h << 8)) + 1) & 0xFFFF; fixS(); break; }
    case 0x40: { io(); io(); setP(pull8()); PC = pull16(); if (!E) PB = pull8(); break; }
    // ---- stack
    case 0x48: io(); if (fM) push8(A & 0xFF); else push16(A); break;
    case 0x68: io(); io(); if (fM) { const int t = pull8(); A = (A & 0xFF00) | t; setNZ8(t); } else { A = pull16(); setNZ16(A); } break;
    case 0xDA: io(); if (fX) push8(X); else push16(X); break;
    case 0xFA: io(); io(); if (fX) { X = pull8(); setNZ8(X); } else { X = pull16(); setNZ16(X); } break;
    case 0x5A: io(); if (fX) push8(Y); else push16(Y); break;
    case 0x7A: io(); io(); if (fX) { Y = pull8(); setNZ8(Y); } else { Y = pull16(); setNZ16(Y); } break;
    case 0x08: io(); push8(getP()); break;
    case 0x28: io(); io(); setP(pull8()); break;
    case 0x8B: io(); push8(DB); break;
    case 0xAB: io(); io(); DB = pullN(); setNZ8(DB); fixS(); break;
    case 0x0B: io(); pushN(D >> 8); pushN(D & 0xFF); fixS(); break;
    case 0x2B: { io(); io(); const int l = pullN(); const int h = pullN(); D = l | (h << 8); setNZ16(D); fixS(); break; }
    case 0x4B: io(); push8(PB); break;
    case 0xF4: { const int v2 = fetch16(); pushN(v2 >> 8); pushN(v2 & 0xFF); fixS(); break; }
    case 0xD4: { const int d = aDp(); const int v2 = rd16b0(d); pushN(v2 >> 8); pushN(v2 & 0xFF); fixS(); break; }
    case 0x62: { const int o = fetch16(); io(); const int v2 = (PC + s16(o)) & 0xFFFF; pushN(v2 >> 8); pushN(v2 & 0xFF); fixS(); break; }
    // ---- misc
    case 0x00: fetch(); interrupt(0xFFE6, 0xFFFE, true); break;
    case 0x02: fetch(); interrupt(0xFFE4, 0xFFF4, false); break;
    case 0x42: fetch(); break;  // WDM
    case 0xEA: io(); break;
    case 0xCB: io(); io(); waiting = 1; break;
    case 0xDB: io(); io(); stopped = 1; break;
    case 0x54: blockMove(1); break;   // MVN
    case 0x44: blockMove(-1); break;  // MVP
    default: io(); break;
  }
}

/* ============================ PPU ============================ */
int SnesCore::vramAddr() {
  int a = vmadd;
  switch ((vmain >> 2) & 3) {
    case 1: a = (a & 0xFF00) | ((a & 0xE0) >> 5) | ((a & 0x1F) << 3); break;
    case 2: a = (a & 0xFE00) | ((a & 0x1C0) >> 6) | ((a & 0x3F) << 3); break;
    case 3: a = (a & 0xFC00) | ((a & 0x380) >> 7) | ((a & 0x7F) << 3); break;
  }
  return a & 0x7FFF;
}
static const int VINC[4] = {1, 32, 128, 128};

void SnesCore::ppuWrite(int o, int v) {
  switch (o) {
    case 0x00: inidisp = v; return;
    case 0x01: obsel = v; return;
    case 0x02: oamReload = (oamReload & 0x200) | (v << 1); oamAddr = oamReload; return;
    case 0x03: oamReload = (oamReload & 0x1FE) | ((v & 1) << 9); oamAddr = oamReload; oamPrio = v & 0x80; return;
    case 0x04:
      if (oamAddr < 512) { if (!(oamAddr & 1)) oamLatch = v; else { oam[oamAddr - 1] = (uint8_t)oamLatch; oam[oamAddr] = (uint8_t)v; } }
      else oam[512 + (oamAddr & 31)] = (uint8_t)v;
      oamAddr = (oamAddr + 1) & 0x3FF;
      return;
    case 0x05: bgmode = v; return;
    case 0x06: mosaic = v; return;
    case 0x07: case 0x08: case 0x09: case 0x0A: bgsc[o - 7] = (uint8_t)v; return;
    case 0x0B: bgnba[0] = (uint8_t)v; return;
    case 0x0C: bgnba[1] = (uint8_t)v; return;
    case 0x0D: case 0x0F: case 0x11: case 0x13: {
      const int i = (o - 0x0D) >> 1;
      hofs[i] = (uint16_t)(((v << 8) | (scrollPrev & ~7) | ((hofs[i] >> 8) & 7)) & 0x3FF);
      scrollPrev = v; scrollPrev2 = v;
      if (o == 0x0D) { m7hofs = s13((v << 8) | m7prev); m7prev = v; }
      return;
    }
    case 0x0E: case 0x10: case 0x12: case 0x14: {
      const int i = (o - 0x0E) >> 1;
      vofs[i] = (uint16_t)(((v << 8) | scrollPrev) & 0x3FF);
      scrollPrev = v;
      if (o == 0x0E) { m7vofs = s13((v << 8) | m7prev); m7prev = v; }
      return;
    }
    case 0x15: vmain = v; return;
    case 0x16: vmadd = (vmadd & 0xFF00) | v; vramLatch = vram[vramAddr()]; return;
    case 0x17: vmadd = (vmadd & 0x00FF) | (v << 8); vramLatch = vram[vramAddr()]; return;
    // o console ignora escritas na VRAM enquanto a tela está sendo desenhada
    case 0x18: { const int a = vramAddr(); if (vramOpen()) vram[a] = (uint16_t)((vram[a] & 0xFF00) | v); if (!(vmain & 0x80)) vmadd = (vmadd + VINC[vmain & 3]) & 0xFFFF; return; }
    case 0x19: { const int a = vramAddr(); if (vramOpen()) vram[a] = (uint16_t)((vram[a] & 0x00FF) | (v << 8)); if (vmain & 0x80) vmadd = (vmadd + VINC[vmain & 3]) & 0xFFFF; return; }
    case 0x1A: m7sel = v; return;
    case 0x1B: m7a = s16((v << 8) | m7prev); m7prev = v; mpy = m7a * (m7b >> 8); return;
    case 0x1C: m7b = s16((v << 8) | m7prev); m7prev = v; mpy = m7a * s8(v); return;
    case 0x1D: m7c = s16((v << 8) | m7prev); m7prev = v; return;
    case 0x1E: m7d = s16((v << 8) | m7prev); m7prev = v; return;
    case 0x1F: m7x = s13((v << 8) | m7prev); m7prev = v; return;
    case 0x20: m7y = s13((v << 8) | m7prev); m7prev = v; return;
    case 0x21: cgadd = v; cgLatch = -1; cgReadToggle = 0; return;
    case 0x22: if (cgLatch < 0) cgLatch = v; else { cgram[cgadd] = (uint16_t)((cgLatch | (v << 8)) & 0x7FFF); cgadd = (cgadd + 1) & 0xFF; cgLatch = -1; } return;
    case 0x23: w12sel = v; return; case 0x24: w34sel = v; return; case 0x25: wobjsel = v; return;
    case 0x26: wh0 = v; return; case 0x27: wh1 = v; return; case 0x28: wh2 = v; return; case 0x29: wh3 = v; return;
    case 0x2A: wbglog = v; return; case 0x2B: wobjlog = v; return;
    case 0x2C: tm = v; return; case 0x2D: ts = v; return; case 0x2E: tmw = v; return; case 0x2F: tsw = v; return;
    case 0x30: cgwsel = v; return; case 0x31: cgadsub = v; return;
    case 0x32: {
      const int i = v & 0x1F;
      if (v & 0x20) fixedColor = (fixedColor & ~0x1F) | i;
      if (v & 0x40) fixedColor = (fixedColor & ~0x3E0) | (i << 5);
      if (v & 0x80) fixedColor = (fixedColor & ~0x7C00) | (i << 10);
      return;
    }
    case 0x33: setini = v; return;
  }
}

int SnesCore::ppuRead(int o) {
  switch (o) {
    case 0x34: return mpy & 0xFF;
    case 0x35: return (mpy >> 8) & 0xFF;
    case 0x36: return (mpy >> 16) & 0xFF;
    case 0x37: latchH = (cyc >> 2) % 341; latchV = vcount; latched = 1; return openBus;
    case 0x38: { int v; if (oamAddr < 512) v = oam[oamAddr]; else v = oam[512 + (oamAddr & 31)]; oamAddr = (oamAddr + 1) & 0x3FF; return v; }
    case 0x39: { const int v = vramLatch & 0xFF; if (!(vmain & 0x80)) { vramLatch = vram[vramAddr()]; vmadd = (vmadd + VINC[vmain & 3]) & 0xFFFF; } return v; }
    case 0x3A: { const int v = vramLatch >> 8; if (vmain & 0x80) { vramLatch = vram[vramAddr()]; vmadd = (vmadd + VINC[vmain & 3]) & 0xFFFF; } return v; }
    case 0x3B: { const int c = cgram[cgadd]; int v; if (!cgReadToggle) v = c & 0xFF; else { v = (c >> 8) | (openBus & 0x80); cgadd = (cgadd + 1) & 0xFF; } cgReadToggle ^= 1; return v; }
    case 0x3C: { const int v = hToggle ? (latchH >> 8) & 1 : latchH & 0xFF; hToggle ^= 1; return v; }
    case 0x3D: { const int v = vToggle ? (latchV >> 8) & 1 : latchV & 0xFF; vToggle ^= 1; return v; }
    case 0x3E: return 0x01;
    case 0x3F: { const int v = (latched ? 0x40 : 0) | 0x03; latched = 0; hToggle = 0; vToggle = 0; return v; }
  }
  return openBus;
}

/* ----- per-line rendering ----- */
static const int BPP[8][4] = {{2, 2, 2, 2}, {4, 4, 2, 0}, {4, 4, 0, 0}, {8, 4, 0, 0}, {8, 2, 0, 0}, {4, 2, 0, 0}, {4, 0, 0, 0}, {8, 0, 0, 0}};

static inline int directColor(int c, int p) {
  return (((c & 7) << 2) | ((p & 1) << 1)) | (((((c >> 3) & 7) << 2) | (p & 2)) << 5) | (((((c >> 6) & 3) << 3) | (p & 4)) << 10);
}

int SnesCore::bg3Entry(int hx, int vy) {
  const bool hires = (bgmode & 7) == 5 || (bgmode & 7) == 6;
  const int th = (bgmode & 0x40) ? 4 : 3, tw = hires ? 4 : th;
  const int sc = (bgsc[2] & 0xFC) << 8, scSize = bgsc[2] & 3;
  const int tx = (hx & 0x3FF) >> tw, ty = (vy & 0x3FF) >> th;
  int addr = sc + ((ty & 31) << 5) + (tx & 31);
  if ((scSize & 1) && (tx & 32)) addr += 0x400;
  if ((scSize & 2) && (ty & 32)) addr += (scSize & 1) ? 0x800 : 0x400;
  return vram[addr & 0x7FFF];
}

void SnesCore::buildOpt(int i, int mode) {
  const int valid = 0x2000 << i, hs3 = hofs[2] & ~7, vs3 = vofs[2];
  for (int col = 1; col < 66; col++) {
    const int lx = (col - 1) * 8 + hs3, hl = bg3Entry(lx, vs3);
    optH[col] = -1; optV[col] = -1;
    if (mode == 4) {
      if (hl & valid) { if (hl & 0x8000) optV[col] = hl & 0x3FF; else optH[col] = hl & 0x3F8; }
    } else {
      const int vl = bg3Entry(lx, vs3 + 8);
      if (hl & valid) optH[col] = hl & 0x3F8;
      if (vl & valid) optV[col] = vl & 0x3FF;
    }
  }
}

void SnesCore::renderBG(int i, int y, int32_t* out, int bpp) {
  const int mode = bgmode & 7, size16 = (bgmode >> (4 + i)) & 1;
  const bool hires = mode == 5 || mode == 6;
  const int tw = size16 ? 16 : 8;
  const int sc = (bgsc[i] & 0xFC) << 8, scSize = bgsc[i] & 3;
  const int charBase = ((bgnba[i >> 1] >> ((i & 1) * 4)) & 15) << 12;
  const int mos = ((mosaic >> i) & 1) ? (mosaic >> 4) + 1 : 1;
  const int yy = mos > 1 ? y - ((y - 1) % mos) : y;
  const int ly = (hires && (setini & 1)) ? yy * 2 : yy;
  const int py0 = (ly + vofs[i]) & 0x3FF;
  const int twx = hires ? 16 : tw;
  const int wpt = bpp * 4;  // words per 8x8 tile
  const bool direct = (cgwsel & 1) && bpp == 8;
  const int palBase = mode == 0 ? i * 32 : 0;
  const bool opt = (mode == 2 || mode == 4 || mode == 6) && i < 2;
  if (opt) buildOpt(i, mode);
  int lastTile = -1, row0 = 0, row1 = 0, row2 = 0, row3 = 0, pal = 0, prio = 0, hf = 0;
  for (int x = 0; x < W; x++) {
    const int xx = mos > 1 ? x - (x % mos) : x;
    const int sx = hires ? xx * 2 : xx;
    int px = (sx + hofs[i]) & 0x3FF, py = py0;
    if (opt) {
      const int ox = sx + (hofs[i] & 7), col = ox >> 3;
      if (col >= 1 && col < 66) {
        const int h = optH[col], v = optV[col];
        if (h >= 0) px = (ox + h) & 0x3FF;
        if (v >= 0) py = (ly + v) & 0x3FF;
      }
    }
    const int mapX = px >> (twx == 16 ? 4 : 3), mapY = py >> (tw == 16 ? 4 : 3);
    int addr = sc + ((mapY & 31) << 5) + (mapX & 31);
    if ((scSize & 1) && (mapX & 32)) addr += 0x400;
    if ((scSize & 2) && (mapY & 32)) addr += (scSize & 1) ? 0x800 : 0x400;
    const int e = vram[addr & 0x7FFF];
    hf = (e >> 14) & 1;
    const int vf = (e >> 15) & 1;
    int c = e & 0x3FF, fy = py & 7;
    if (twx == 16 || tw == 16) {
      int subX = twx == 16 ? (px >> 3) & 1 : 0, subY = tw == 16 ? (py >> 3) & 1 : 0;
      if (hf && twx == 16) subX ^= 1;
      if (vf && tw == 16) subY ^= 1;
      c = (c + subX + (subY << 4)) & 0x3FF;
    }
    if (vf) fy = 7 - fy;
    const int tileKey = (((c << 3) | fy) << 6) | (e >> 10);
    if (tileKey != lastTile) {
      lastTile = tileKey;
      const int ra = (charBase + c * wpt + fy) & 0x7FFF;
      row0 = vram[ra];
      if (bpp >= 4) row1 = vram[(ra + 8) & 0x7FFF];
      if (bpp == 8) { row2 = vram[(ra + 16) & 0x7FFF]; row3 = vram[(ra + 24) & 0x7FFF]; }
      pal = (e >> 10) & 7; prio = (e >> 13) & 1;
    }
    int fx = px & 7;
    if (hf) fx = 7 - fx;
    const int sh = 7 - fx;
    int idx = ((row0 >> sh) & 1) | ((row0 >> (sh + 7)) & 2);
    if (bpp >= 4) idx |= (((row1 >> sh) & 1) << 2) | (((row1 >> (sh + 8)) & 1) << 3);
    if (bpp == 8) idx |= (((row2 >> sh) & 1) << 4) | (((row2 >> (sh + 8)) & 1) << 5) | (((row3 >> sh) & 1) << 6) | (((row3 >> (sh + 8)) & 1) << 7);
    if (!idx) { out[x] = -1; continue; }
    int color;
    if (direct) color = directColor(idx, pal);
    else if (bpp == 2) color = cgram[palBase + pal * 4 + idx];
    else if (bpp == 4) color = cgram[pal * 16 + idx];
    else color = cgram[idx];
    out[x] = color | (prio << 16);
  }
}

void SnesCore::renderMode7(int y, int32_t* out, int32_t* out2) {
  const int a = m7a, b = m7b, c = m7c, d = m7d;
  auto clip = [](int v) { return (v & 0x2000) ? (v | ~0x3FF) : (v & 0x3FF); };
  const int mos = (mosaic & 1) ? (mosaic >> 4) + 1 : 1;
  int yy = mos > 1 ? y - ((y - 1) % mos) : y;
  if (m7sel & 2) yy = 255 - yy;
  const int cx = m7x, cy = m7y, ho = clip(m7hofs - cx), vo = clip(m7vofs - cy);
  const int sx = ((a * ho) & ~63) + ((b * vo) & ~63) + ((b * yy) & ~63) + (cx << 8);
  const int sy = ((c * ho) & ~63) + ((d * vo) & ~63) + ((d * yy) & ~63) + (cy << 8);
  const int wrapMode = m7sel >> 6, direct = cgwsel & 1;
  for (int x = 0; x < W; x++) {
    int xx = mos > 1 ? x - (x % mos) : x;
    if (m7sel & 1) xx = 255 - xx;
    int px = (sx + a * xx) >> 8, py = (sy + c * xx) >> 8;
    int pix = -1;  // -1 = "undefined" do JS
    if ((px | py) & ~0x3FF) {
      if (wrapMode == 2) { out[x] = -1; if (out2) out2[x] = -1; continue; }
      if (wrapMode == 3) pix = vram[(((py & 7) << 3) | (px & 7))] >> 8;
      else { px &= 0x3FF; py &= 0x3FF; }
    }
    if (pix < 0) {
      const int tile = vram[((py >> 3) << 7) | (px >> 3)] & 0xFF;
      pix = vram[(tile << 6) | ((py & 7) << 3) | (px & 7)] >> 8;
    }
    out[x] = pix ? (direct ? directColor(pix, 0) : cgram[pix]) : -1;
    if (out2) { const int p2 = pix & 0x7F; out2[x] = p2 ? (cgram[p2] | ((pix >> 7) << 16)) : -1; }
  }
}

static const int OBJ_SIZES[8][4] = {{8, 8, 16, 16}, {8, 8, 32, 32}, {8, 8, 64, 64}, {16, 16, 32, 32}, {16, 16, 64, 64}, {32, 32, 64, 64}, {16, 32, 32, 64}, {16, 32, 32, 32}};

void SnesCore::renderObj(int y) {
  for (int x = 0; x < W; x++) objCol[x] = -1;
  const int* sz = OBJ_SIZES[obsel >> 5];
  const int base = (obsel & 7) << 13, base2 = base + ((((obsel >> 3) & 3) + 1) << 12);
  const int line = (y - 1) & 0xFF;
  int count = 0;
  int list[32];
  const int first = oamPrio ? (oamReload >> 2) & 127 : 0;
  for (int n = 0; n < 128 && count < 32; n++) {
    const int i = (first + n) & 127;
    const int hi = (oam[512 + (i >> 2)] >> ((i & 3) * 2)) & 3;
    const int large = hi & 2, w = large ? sz[2] : sz[0], h = large ? sz[3] : sz[1];
    const int sy = oam[i * 4 + 1];
    const int rel = (line - sy) & 0xFF;
    if (rel >= h) continue;
    int sxp = oam[i * 4] | ((hi & 1) << 8);
    if (sxp >= 256) sxp -= 512;
    if (sxp + w <= 0 || sxp >= 256) continue;
    list[count++] = i;
  }
  for (int k = count - 1; k >= 0; k--) {
    const int i = list[k];
    const int hi = (oam[512 + (i >> 2)] >> ((i & 3) * 2)) & 3;
    const int large = hi & 2, w = large ? sz[2] : sz[0], h = large ? sz[3] : sz[1];
    const int sy = oam[i * 4 + 1], attr = oam[i * 4 + 3], tile = oam[i * 4 + 2] | ((attr & 1) << 8);
    int sxp = oam[i * 4] | ((hi & 1) << 8);
    if (sxp >= 256) sxp -= 512;
    int rel = (line - sy) & 0xFF;
    if (attr & 0x80) rel = h - 1 - rel;
    const int pal = (attr >> 1) & 7, prio = (attr >> 4) & 3, hflip = attr & 0x40;
    const int tbase = (tile & 0x100) ? base2 : base, t = tile & 0xFF;
    const int row = rel >> 3;
    for (int px = 0; px < w; px++) {
      const int x = sxp + px;
      if (x < 0 || x >= 256) continue;
      const int rx = hflip ? w - 1 - px : px, col = rx >> 3;
      const int tn = (((t & 0xF0) + (row << 4)) & 0xF0) | ((t + col) & 0x0F);
      const int ra = (tbase + tn * 16 + (rel & 7)) & 0x7FFF;
      const int ww0 = vram[ra], ww1 = vram[(ra + 8) & 0x7FFF], sh = 7 - (rx & 7);
      const int idx = ((ww0 >> sh) & 1) | (((ww0 >> (sh + 8)) & 1) << 1) | (((ww1 >> sh) & 1) << 2) | (((ww1 >> (sh + 8)) & 1) << 3);
      if (!idx) continue;
      objCol[x] = cgram[128 + pal * 16 + idx];
      objPrio[x] = (uint8_t)prio;
      objPal[x] = (uint8_t)pal;
    }
  }
}

void SnesCore::windowFor(int layer, uint8_t* out) {
  int sel, logic;
  if (layer < 4) { sel = ((layer < 2 ? w12sel : w34sel) >> ((layer & 1) * 4)) & 15; logic = (wbglog >> (layer * 2)) & 3; }
  else { sel = (wobjsel >> ((layer - 4) * 4)) & 15; logic = (wobjlog >> ((layer - 4) * 2)) & 3; }
  const int e1 = sel & 2, i1 = sel & 1, e2 = sel & 8, i2 = sel & 4;
  if (!e1 && !e2) { std::memset(out, 0, W); return; }
  for (int x = 0; x < W; x++) {
    int a = (x >= wh0 && x <= wh1) ? 1 : 0; if (i1) a ^= 1;
    int b = (x >= wh2 && x <= wh3) ? 1 : 0; if (i2) b ^= 1;
    int r;
    if (e1 && e2) r = logic == 0 ? (a | b) : logic == 1 ? (a & b) : logic == 2 ? (a ^ b) : 1 - (a ^ b);
    else r = e1 ? a : b;
    out[x] = (uint8_t)r;
  }
}

void SnesCore::buildLuts() {
  for (int b = 0; b < 16; b++) {
    uint32_t* L = &lutB[b * 0x8000];
    const double k = b ? (b + 1) / 16.0 : 0;  // brilho 0 = tela preta
    for (int c = 0; c < 0x8000; c++) {
      const int r = c & 31, g = (c >> 5) & 31, bl = (c >> 10) & 31;
      int R, G, B;
      if (colorFix) {
        R = ((r * 26 + g * 4 + bl * 2) >> 2); G = ((g * 24 + bl * 8) >> 2); B = ((r * 6 + g * 4 + bl * 22) >> 2);
        R = std::min(255, R); G = std::min(255, G); B = std::min(255, B);
      } else { R = (r << 3) | (r >> 2); G = (g << 3) | (g >> 2); B = (bl << 3) | (bl >> 2); }
      // JS: (B * k) << 16 converte para inteiro truncando (valores sempre >= 0)
      const uint32_t bk = (uint32_t)(int32_t)(B * k), gk = (uint32_t)(int32_t)(G * k), rk = (uint32_t)(int32_t)(R * k);
      L[c] = 0xFF000000u | (bk << 16) | (gk << 8) | rk;
    }
  }
  lutReady = true;
}

void SnesCore::renderLine(int y) {
  if (!lutReady) buildLuts();
  const int out = (y - 1) * W;
  if (inidisp & 0x80) { for (int x = 0; x < W; x++) frame[out + x] = 0xFF000000u; return; }
  const int mode = bgmode & 7;
  const int* bpp = BPP[mode];
  // z order (higher = in front). index: [layer][prio]; layer 4 = OBJ
  int Z[5][4];
  if (mode == 0) { int t[5][4] = {{8, 11}, {7, 10}, {2, 5}, {1, 4}, {3, 6, 9, 12}}; std::memcpy(Z, t, sizeof Z); }
  else if (mode == 1) { int t[5][4] = {{8, 11}, {7, 10}, {2, (bgmode & 8) ? 13 : 5}, {0, 0}, {3, 6, 9, 12}}; std::memcpy(Z, t, sizeof Z); }
  else if (mode == 7) { int t[5][4] = {{2, 2}, {1, 5}, {0, 0}, {0, 0}, {3, 6, 9, 12}}; std::memcpy(Z, t, sizeof Z); }
  else { int t[5][4] = {{5, 11}, {2, 8}, {0, 0}, {0, 0}, {3, 6, 9, 12}}; std::memcpy(Z, t, sizeof Z); }
  const int used = tm | ts;
  for (int i = 0; i < 4; i++) {
    if (!bpp[i] && !(mode == 7 && i == 1)) { for (int x = 0; x < W; x++) bgCol[i][x] = -1; continue; }
    if (!(used & (1 << i))) { for (int x = 0; x < W; x++) bgCol[i][x] = -1; continue; }
    if (mode == 7) {
      if (i == 0) renderMode7(y, bgCol[0], (setini & 0x40) ? bgCol[1] : nullptr);
      else if (!(setini & 0x40)) { for (int x = 0; x < W; x++) bgCol[1][x] = -1; }
    } else renderBG(i, y, bgCol[i], bpp[i]);
  }
  if (used & 0x10) renderObj(y); else { for (int x = 0; x < W; x++) objCol[x] = -1; }
  for (int l = 0; l < 6; l++) windowFor(l, winBuf[l]);
  const int back = cgram[0];
  const uint32_t* L = &lutB[(inidisp & 15) * 0x8000];
  const int mathOn = cgadsub & 0x3F, sub = cgadsub & 0x80, half = cgadsub & 0x40, useSub = cgwsel & 2;
  const int clipMode = cgwsel >> 6, mathMode = (cgwsel >> 4) & 3;
  for (int x = 0; x < W; x++) {
    int mc = back, ml = 5, mz = -1;
    int sc = -1, sz = -1;
    for (int i = 0; i < 4; i++) {
      const int v = bgCol[i][x];
      if (v < 0) continue;
      const int z = Z[i][(v >> 16) & 1];
      if ((tm & (1 << i)) && !((tmw & (1 << i)) && winBuf[i][x]) && z > mz) { mz = z; mc = v & 0x7FFF; ml = i; }
      if ((ts & (1 << i)) && !((tsw & (1 << i)) && winBuf[i][x]) && z > sz) { sz = z; sc = v & 0x7FFF; }
    }
    const int oc = objCol[x];
    if (oc >= 0) {
      const int z = Z[4][objPrio[x]];
      if ((tm & 0x10) && !((tmw & 0x10) && winBuf[4][x]) && z > mz) { mz = z; mc = oc; ml = objPal[x] >= 4 ? 4 : 6; }
      if ((ts & 0x10) && !((tsw & 0x10) && winBuf[4][x]) && z > sz) { sz = z; sc = oc; }
    }
    const int inWin = winBuf[5][x];
    const bool clip = clipMode == 3 || (clipMode == 1 && !inWin) || (clipMode == 2 && inWin);
    const bool allow = mathMode == 0 || (mathMode == 1 && inWin) || (mathMode == 2 && !inWin);
    int col = clip ? 0 : mc;
    const int layerMath = ml == 6 ? 0 : (mathOn >> ml) & 1;
    if (allow && layerMath) {
      int other;
      bool halve = half && !clip;
      if (useSub) { if (sc >= 0) other = sc; else { other = fixedColor; halve = false; } }
      else other = fixedColor;
      int r = col & 31, g = (col >> 5) & 31, b = (col >> 10) & 31;
      const int r2 = other & 31, g2 = (other >> 5) & 31, b2 = (other >> 10) & 31;
      if (sub) { r -= r2; g -= g2; b -= b2; if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0; }
      else { r += r2; g += g2; b += b2; }
      if (halve) { r >>= 1; g >>= 1; b >>= 1; }
      if (r > 31) r = 31;
      if (g > 31) g = 31;
      if (b > 31) b = 31;
      col = r | (g << 5) | (b << 10);
    }
    frame[out + x] = L[col];
  }
}

/* ============================ APU: SPC700 + DSP ============================ */
void SnesCore::iplReset() { ipl.on = 1; ipl.state = 0; ipl.pending = 0; apuOut[0] = 0xAA; apuOut[1] = 0xBB; apuOut[2] = 0; apuOut[3] = 0; }
void SnesCore::iplPortWrite(int) {
  if (!ipl.on) return;
  if (!ipl.pending) { ipl.pending = 1; ipl.at = (master + cyc) * SPC_RATIO + 6; }
}
void SnesCore::iplProcess() {
  ipl.pending = 0;
  const int v = apuIn[0];
  if (ipl.state == 0) {
    if (v == 0xCC) {
      ipl.addr = apuIn[2] | (apuIn[3] << 8); apuOut[0] = (uint8_t)v; ipl.echo = v;
      if (apuIn[1] == 0) { ipl.on = 0; sPC = ipl.addr; sA = 0; sX = 0; sY = 0; sSP = 0xEF; sPSW = 0x02; spcHalt = 0; }
      else { ipl.state = 1; ipl.expect = 0; }
    }
    return;
  }
  if (v == ipl.echo) return;
  if (v == ipl.expect) {  // next data byte
    aram[ipl.addr] = apuIn[1]; ipl.addr = (ipl.addr + 1) & 0xFFFF; ipl.expect = (v + 1) & 0xFF; ipl.echo = v; apuOut[0] = (uint8_t)v;
    return;
  }
  // new block or jump
  const int target = apuIn[2] | (apuIn[3] << 8);
  apuOut[0] = (uint8_t)v; ipl.echo = v;
  if (apuIn[1] == 0) { ipl.on = 0; sPC = target; sA = 0; sX = 0; sY = 0; sSP = 0xEF; sPSW = 0x02; spcHalt = 0; }
  else { ipl.addr = target; ipl.expect = 0; }
}
void SnesCore::apuCatchUp() {
  spcTarget = (master + cyc) * SPC_RATIO;
  while (spcCycles < spcTarget) {
    if (ipl.on || spcHalt) {
      if (ipl.on && ipl.pending) {
        if (spcCycles < ipl.at) {
          const int n = (int)std::ceil(std::min(ipl.at, spcTarget) - spcCycles);
          spcTick(n); spcCycles += n; continue;
        }
        iplProcess(); continue;
      }
      const double left = spcTarget - spcCycles;
      const int n = (int)std::ceil(left);
      spcTick(n); spcCycles += n;
      break;
    }
    if (sPC >= 0xFFC0 && (spcCtl & 0x80)) { iplReset(); continue; }
    const int c = spcStep(); spcTick(c); spcCycles += c;
  }
}

/* ----- SPC700 memory ----- */
int SnesCore::sRead(int a) {
  if (a >= 0xF0 && a < 0x100) {
    switch (a) {
      case 0xF2: return dspAddr;
      case 0xF3: return dspRead(dspAddr & 0x7F);
      case 0xF4: case 0xF5: case 0xF6: case 0xF7: return apuIn[a - 0xF4];
      case 0xFD: case 0xFE: case 0xFF: { const int i = a - 0xFD, v = timerOut[i]; timerOut[i] = 0; return v; }
      case 0xF8: case 0xF9: return aram[a];
    }
    return 0;
  }
  return aram[a];
}
void SnesCore::sWrite(int a, int v) {
  if (a >= 0xF0 && a < 0x100) {
    switch (a) {
      case 0xF1:
        for (int i = 0; i < 3; i++) { if (!(spcCtl & (1 << i)) && (v & (1 << i))) { timerStage[i] = 0; timerOut[i] = 0; timerCnt[i] = 0; } }
        if (v & 0x10) { apuIn[0] = 0; apuIn[1] = 0; }
        if (v & 0x20) { apuIn[2] = 0; apuIn[3] = 0; }
        spcCtl = v; return;
      case 0xF2: dspAddr = v; return;
      case 0xF3: if (!(dspAddr & 0x80)) dspWrite(dspAddr, v); return;
      case 0xF4: case 0xF5: case 0xF6: case 0xF7: apuOut[a - 0xF4] = (uint8_t)v; return;
      case 0xFA: case 0xFB: case 0xFC: timerDiv[a - 0xFA] = (uint8_t)v; return;
      case 0xF8: case 0xF9: aram[a] = (uint8_t)v; return;
    }
    return;
  }
  aram[a] = (uint8_t)v;
}
void SnesCore::spcTick(int c) {
  for (int i = 0; i < 3; i++) {
    if (!(spcCtl & (1 << i))) continue;
    const int per = i == 2 ? 16 : 128;
    timerStage[i] = (uint16_t)(timerStage[i] + c);  // Uint16Array no JS
    while (timerStage[i] >= per) {
      timerStage[i] = (uint16_t)(timerStage[i] - per);
      timerCnt[i] = (uint8_t)((timerCnt[i] + 1) & 0xFF);
      if (timerCnt[i] == timerDiv[i]) { timerCnt[i] = 0; timerOut[i] = (uint8_t)((timerOut[i] + 1) & 15); }
    }
  }
  dspCycle += c;
  while (dspCycle >= 32) { dspCycle -= 32; dspSample(); }
}

/* ----- SPC700 CPU ----- */
enum { SN = 0x80, SV = 0x40, SP_ = 0x20, SB = 0x10, SH = 0x08, SI = 0x04, SZ = 0x02, SC = 0x01 };
int SnesCore::sf() { const int v = sRead(sPC); sPC = (sPC + 1) & 0xFFFF; return v; }
int SnesCore::sf16() { const int l = sf(); const int h = sf(); return l | (h << 8); }
int SnesCore::dpg() const { return (sPSW & SP_) ? 0x100 : 0; }
int SnesCore::r16dp(int o) { const int l = sRead(dpA(o)); const int h = sRead(dpA(o + 1)); return l | (h << 8); }
int SnesCore::nz(int v) { sPSW = (sPSW & ~(SN | SZ)) | (v & 0x80) | ((v & 0xFF) == 0 ? SZ : 0); return v & 0xFF; }
int SnesCore::sadc(int a, int b) {
  const int c = sPSW & SC, r = a + b + c;
  sPSW &= ~(SV | SH | SC);
  if (~(a ^ b) & (a ^ r) & 0x80) sPSW |= SV;
  if (((a & 15) + (b & 15) + c) > 15) sPSW |= SH;
  if (r > 0xFF) sPSW |= SC;
  return nz(r);
}
void SnesCore::scmp(int a, int b) { const int r = a - b; sPSW = (sPSW & ~SC) | (r >= 0 ? SC : 0); nz(r & 0xFF); }
int SnesCore::sop(int k, int a, int b) {
  switch (k) {
    case 0: return nz(a | b);
    case 1: return nz(a & b);
    case 2: return nz(a ^ b);
    case 3: scmp(a, b); return a;
    case 4: return sadc(a, b);
    default: return ssbc(a, b);
  }
}
int SnesCore::sbranch(bool c) { const int o = sf(); if (c) { sPC = (sPC + s8(o)) & 0xFFFF; return 2; } return 0; }
int SnesCore::sasl(int v) { sPSW = (sPSW & ~SC) | ((v >> 7) & 1); return nz((v << 1) & 0xFF); }
int SnesCore::slsr(int v) { sPSW = (sPSW & ~SC) | (v & 1); return nz(v >> 1); }
int SnesCore::srol(int v) { const int c = sPSW & 1; sPSW = (sPSW & ~SC) | ((v >> 7) & 1); return nz(((v << 1) | c) & 0xFF); }
int SnesCore::sror(int v) { const int c = sPSW & 1; sPSW = (sPSW & ~SC) | (v & 1); return nz((v >> 1) | (c << 7)); }
void SnesCore::bitAddr(int& a, int& b) { const int w = sf16(); a = w & 0x1FFF; b = w >> 13; }

int SnesCore::spcStep() {
  const int op = sf();
  const int hi = op >> 4, lo = op & 15;
  // ---- ALU group: OR AND EOR CMP ADC SBC
  if (lo >= 4 && lo <= 9 && hi <= 0xB) {
    const int k = hi >> 1, odd = hi & 1;
    switch (lo) {
      case 4: { int a; if (odd) { const int o = sf(); a = dpA(o + sX); } else a = dpA(sf()); sA = sop(k, sA, sRead(a)); return odd ? 4 : 3; }
      case 5: { const int w = sf16(); const int a = (w + (odd ? sX : 0)) & 0xFFFF; sA = sop(k, sA, sRead(a)); return odd ? 5 : 4; }
      case 6: {
        if (!odd) { sA = sop(k, sA, sRead(dpA(sX))); return 3; }
        const int a = (sf16() + sY) & 0xFFFF; sA = sop(k, sA, sRead(a)); return 5;
      }
      case 7: {
        int a;
        if (!odd) { const int d = sf(); const int l = sRead(dpA(d + sX)); const int h = sRead(dpA(d + sX + 1)); a = l | (h << 8); }
        else { const int d = sf(); const int l = sRead(dpA(d)); const int h = sRead(dpA(d + 1)); a = ((l | (h << 8)) + sY) & 0xFFFF; }
        sA = sop(k, sA, sRead(a)); return 6;
      }
      case 8: {
        if (!odd) { sA = sop(k, sA, sf()); return 2; }
        const int imm = sf(); const int d = dpA(sf());
        const int r = sop(k, sRead(d), imm); if (k != 3) sWrite(d, r); return 5;
      }
      case 9: {
        if (!odd) {
          const int s = sf(); const int d = dpA(sf());
          const int m1 = sRead(d); const int m2 = sRead(dpA(s));
          const int r = sop(k, m1, m2); if (k != 3) sWrite(d, r); return 6;
        }
        const int m1 = sRead(dpA(sX)); const int m2 = sRead(dpA(sY));
        const int r = sop(k, m1, m2); if (k != 3) sWrite(dpA(sX), r); return 5;
      }
    }
  }
  if (lo == 1) { const int v = 0xFFDE - hi * 2; spush(sPC >> 8); spush(sPC & 0xFF); const int l = sRead(v); const int h = sRead(v + 1); sPC = l | (h << 8); return 8; }
  if (lo == 2) { const int d = dpA(sf()), bit = hi >> 1; int v = sRead(d); v = (hi & 1) ? v & ~(1 << bit) : v | (1 << bit); sWrite(d, v); return 4; }
  if (lo == 3) { const int d = dpA(sf()), bit = hi >> 1, v = sRead(d), set = (v >> bit) & 1; return 5 + sbranch((hi & 1) ? !set : !!set); }
  if (lo == 0 && (hi & 1)) {  // branches
    const bool conds[8] = {!(sPSW & SN), !!(sPSW & SN), !(sPSW & SV), !!(sPSW & SV), !(sPSW & SC), !!(sPSW & SC), !(sPSW & SZ), !!(sPSW & SZ)};
    return 2 + sbranch(conds[hi >> 1]);
  }
  switch (op) {
    case 0x00: return 2;
    case 0x20: sPSW &= ~SP_; return 2; case 0x40: sPSW |= SP_; return 2;
    case 0x60: sPSW &= ~SC; return 2; case 0x80: sPSW |= SC; return 2;
    case 0xA0: sPSW |= SI; return 3; case 0xC0: sPSW &= ~SI; return 3;
    case 0xE0: sPSW &= ~(SV | SH); return 2;
    // MOV stores
    case 0xC4: sWrite(dpA(sf()), sA); return 4;
    case 0xD4: { const int o = sf(); sWrite(dpA(o + sX), sA); return 5; }
    case 0xC5: sWrite(sf16(), sA); return 5;
    case 0xD5: sWrite((sf16() + sX) & 0xFFFF, sA); return 6;
    case 0xC6: sWrite(dpA(sX), sA); return 4;
    case 0xD6: sWrite((sf16() + sY) & 0xFFFF, sA); return 6;
    case 0xC7: { const int d = sf(); const int l = sRead(dpA(d + sX)); const int h = sRead(dpA(d + sX + 1)); sWrite(l | (h << 8), sA); return 7; }
    case 0xD7: { const int d = sf(); const int l = sRead(dpA(d)); const int h = sRead(dpA(d + 1)); sWrite(((l | (h << 8)) + sY) & 0xFFFF, sA); return 7; }
    case 0xD8: sWrite(dpA(sf()), sX); return 4;
    case 0xD9: { const int o = sf(); sWrite(dpA(o + sY), sX); return 5; }
    case 0xC9: sWrite(sf16(), sX); return 5;
    case 0xCB: sWrite(dpA(sf()), sY); return 4;
    case 0xDB: { const int o = sf(); sWrite(dpA(o + sX), sY); return 5; }
    case 0xCC: sWrite(sf16(), sY); return 5;
    case 0x8F: { const int imm = sf(); const int d = dpA(sf()); sWrite(d, imm); return 5; }
    case 0xFA: { const int s = sf(); const int d = sf(); const int v = sRead(dpA(s)); sWrite(dpA(d), v); return 5; }
    case 0xAF: sWrite(dpA(sX), sA); sX = (sX + 1) & 0xFF; return 4;
    // MOV loads
    case 0xE4: sA = nz(sRead(dpA(sf()))); return 3;
    case 0xF4: { const int o = sf(); sA = nz(sRead(dpA(o + sX))); return 4; }
    case 0xE5: sA = nz(sRead(sf16())); return 4;
    case 0xF5: sA = nz(sRead((sf16() + sX) & 0xFFFF)); return 5;
    case 0xE6: sA = nz(sRead(dpA(sX))); return 3;
    case 0xF6: sA = nz(sRead((sf16() + sY) & 0xFFFF)); return 5;
    case 0xE7: { const int d = sf(); const int l = sRead(dpA(d + sX)); const int h = sRead(dpA(d + sX + 1)); sA = nz(sRead(l | (h << 8))); return 6; }
    case 0xF7: { const int d = sf(); const int l = sRead(dpA(d)); const int h = sRead(dpA(d + 1)); sA = nz(sRead(((l | (h << 8)) + sY) & 0xFFFF)); return 6; }
    case 0xE8: sA = nz(sf()); return 2;
    case 0xBF: sA = nz(sRead(dpA(sX))); sX = (sX + 1) & 0xFF; return 4;
    case 0xF8: sX = nz(sRead(dpA(sf()))); return 3;
    case 0xF9: { const int o = sf(); sX = nz(sRead(dpA(o + sY))); return 4; }
    case 0xE9: sX = nz(sRead(sf16())); return 4;
    case 0xCD: sX = nz(sf()); return 2;
    case 0xEB: sY = nz(sRead(dpA(sf()))); return 3;
    case 0xFB: { const int o = sf(); sY = nz(sRead(dpA(o + sX))); return 4; }
    case 0xEC: sY = nz(sRead(sf16())); return 4;
    case 0x8D: sY = nz(sf()); return 2;
    // register moves
    case 0x5D: sX = nz(sA); return 2; case 0x7D: sA = nz(sX); return 2;
    case 0xFD: sY = nz(sA); return 2; case 0xDD: sA = nz(sY); return 2;
    case 0x9D: sX = nz(sSP); return 2; case 0xBD: sSP = sX; return 2;
    // compares with X/Y
    case 0xC8: scmp(sX, sf()); return 2; case 0x3E: scmp(sX, sRead(dpA(sf()))); return 3; case 0x1E: scmp(sX, sRead(sf16())); return 4;
    case 0xAD: scmp(sY, sf()); return 2; case 0x7E: scmp(sY, sRead(dpA(sf()))); return 3; case 0x5E: scmp(sY, sRead(sf16())); return 4;
    // shifts / inc / dec
    case 0x0B: { const int d = dpA(sf()); sWrite(d, sasl(sRead(d))); return 4; }
    case 0x1B: { const int o = sf(); const int d = dpA(o + sX); sWrite(d, sasl(sRead(d))); return 5; }
    case 0x0C: { const int a = sf16(); sWrite(a, sasl(sRead(a))); return 5; }
    case 0x1C: sA = sasl(sA); return 2;
    case 0x2B: { const int d = dpA(sf()); sWrite(d, srol(sRead(d))); return 4; }
    case 0x3B: { const int o = sf(); const int d = dpA(o + sX); sWrite(d, srol(sRead(d))); return 5; }
    case 0x2C: { const int a = sf16(); sWrite(a, srol(sRead(a))); return 5; }
    case 0x3C: sA = srol(sA); return 2;
    case 0x4B: { const int d = dpA(sf()); sWrite(d, slsr(sRead(d))); return 4; }
    case 0x5B: { const int o = sf(); const int d = dpA(o + sX); sWrite(d, slsr(sRead(d))); return 5; }
    case 0x4C: { const int a = sf16(); sWrite(a, slsr(sRead(a))); return 5; }
    case 0x5C: sA = slsr(sA); return 2;
    case 0x6B: { const int d = dpA(sf()); sWrite(d, sror(sRead(d))); return 4; }
    case 0x7B: { const int o = sf(); const int d = dpA(o + sX); sWrite(d, sror(sRead(d))); return 5; }
    case 0x6C: { const int a = sf16(); sWrite(a, sror(sRead(a))); return 5; }
    case 0x7C: sA = sror(sA); return 2;
    case 0x8B: { const int d = dpA(sf()); sWrite(d, nz(sRead(d) - 1)); return 4; }
    case 0x9B: { const int o = sf(); const int d = dpA(o + sX); sWrite(d, nz(sRead(d) - 1)); return 5; }
    case 0x8C: { const int a = sf16(); sWrite(a, nz(sRead(a) - 1)); return 5; }
    case 0x9C: sA = nz(sA - 1); return 2;
    case 0xAB: { const int d = dpA(sf()); sWrite(d, nz(sRead(d) + 1)); return 4; }
    case 0xBB: { const int o = sf(); const int d = dpA(o + sX); sWrite(d, nz(sRead(d) + 1)); return 5; }
    case 0xAC: { const int a = sf16(); sWrite(a, nz(sRead(a) + 1)); return 5; }
    case 0xBC: sA = nz(sA + 1); return 2;
    case 0x1D: sX = nz(sX - 1); return 2; case 0x3D: sX = nz(sX + 1); return 2;
    case 0xDC: sY = nz(sY - 1); return 2; case 0xFC: sY = nz(sY + 1); return 2;
    // 16-bit
    case 0x1A: { const int o = sf(); const int v = (r16dp(o) - 1) & 0xFFFF; sWrite(dpA(o), v & 0xFF); sWrite(dpA(o + 1), v >> 8); sPSW = (sPSW & ~(SN | SZ)) | ((v >> 8) & 0x80) | (v == 0 ? SZ : 0); return 6; }
    case 0x3A: { const int o = sf(); const int v = (r16dp(o) + 1) & 0xFFFF; sWrite(dpA(o), v & 0xFF); sWrite(dpA(o + 1), v >> 8); sPSW = (sPSW & ~(SN | SZ)) | ((v >> 8) & 0x80) | (v == 0 ? SZ : 0); return 6; }
    case 0x5A: { const int v = r16dp(sf()), ya = (sY << 8) | sA, r = ya - v; sPSW = (sPSW & ~(SN | SZ | SC)) | (r >= 0 ? SC : 0) | ((r >> 8) & 0x80) | ((r & 0xFFFF) == 0 ? SZ : 0); return 4; }
    case 0x7A: case 0x9A: {
      int v = r16dp(sf()); const int ya = (sY << 8) | sA;
      if (op == 0x9A) v = (~v) & 0xFFFF;
      const int c = op == 0x9A ? 1 : 0, r = ya + v + c;
      sPSW &= ~(SN | SV | SH | SZ | SC);
      if (~(ya ^ v) & (ya ^ r) & 0x8000) sPSW |= SV;
      if (((ya & 0xFFF) + (v & 0xFFF) + c) > 0xFFF) sPSW |= SH;
      if (r > 0xFFFF) sPSW |= SC;
      const int rr = r & 0xFFFF; if (rr & 0x8000) sPSW |= SN; if (!rr) sPSW |= SZ;
      sA = rr & 0xFF; sY = rr >> 8; return 5;
    }
    case 0xBA: { const int v = r16dp(sf()); sA = v & 0xFF; sY = v >> 8; sPSW = (sPSW & ~(SN | SZ)) | ((v >> 8) & 0x80) | (v == 0 ? SZ : 0); return 5; }
    case 0xDA: { const int o = sf(); sWrite(dpA(o), sA); sWrite(dpA(o + 1), sY); return 5; }
    case 0xCF: { const int r = sY * sA; sA = r & 0xFF; sY = r >> 8; nz(sY); return 9; }
    case 0x9E: {
      int ya = (sY << 8) | sA; const int x = sX;
      sPSW &= ~(SV | SH);
      if ((x & 15) <= (sY & 15)) sPSW |= SH;
      for (int i = 0; i < 9; i++) {
        ya <<= 1; if (ya & 0x20000) ya ^= 0x20001;
        if (ya >= (x << 9)) ya ^= 1;
        if (ya & 1) ya = (ya - (x << 9)) & 0x1FFFF;
      }
      if (ya & 0x100) sPSW |= SV;
      sA = ya & 0xFF; sY = (ya >> 9) & 0xFF; nz(sA); return 12;
    }
    case 0xDF: { if ((sPSW & SC) || sA > 0x99) { sA = (sA + 0x60) & 0xFF; sPSW |= SC; } if ((sPSW & SH) || (sA & 15) > 9) sA = (sA + 6) & 0xFF; nz(sA); return 3; }
    case 0xBE: { if (!(sPSW & SC) || sA > 0x99) { sA = (sA - 0x60) & 0xFF; sPSW &= ~SC; } if (!(sPSW & SH) || (sA & 15) > 9) sA = (sA - 6) & 0xFF; nz(sA); return 3; }
    case 0x9F: sA = nz(((sA >> 4) | (sA << 4)) & 0xFF); return 5;
    // bit ops on absolute bits
    case 0x0A: { int a, b; bitAddr(a, b); if ((sRead(a) >> b) & 1) sPSW |= SC; return 5; }
    case 0x2A: { int a, b; bitAddr(a, b); if (!((sRead(a) >> b) & 1)) sPSW |= SC; return 5; }
    case 0x4A: { int a, b; bitAddr(a, b); if (!((sRead(a) >> b) & 1)) sPSW &= ~SC; return 4; }
    case 0x6A: { int a, b; bitAddr(a, b); if ((sRead(a) >> b) & 1) sPSW &= ~SC; return 4; }
    case 0x8A: { int a, b; bitAddr(a, b); if ((sRead(a) >> b) & 1) sPSW ^= SC; return 5; }
    case 0xAA: { int a, b; bitAddr(a, b); sPSW = (sPSW & ~SC) | ((sRead(a) >> b) & 1); return 4; }
    case 0xCA: { int a, b; bitAddr(a, b); int v = sRead(a); v = (sPSW & SC) ? v | (1 << b) : v & ~(1 << b); sWrite(a, v); return 6; }
    case 0xEA: { int a, b; bitAddr(a, b); const int v = sRead(a); sWrite(a, v ^ (1 << b)); return 5; }
    case 0x0E: { const int a = sf16(); const int v = sRead(a); nz((sA - v) & 0xFF); sWrite(a, v | sA); return 6; }
    case 0x4E: { const int a = sf16(); const int v = sRead(a); nz((sA - v) & 0xFF); sWrite(a, v & ~sA); return 6; }
    case 0xED: sPSW ^= SC; return 3;
    // branches / jumps
    case 0x2F: return 2 + sbranch(true);
    case 0x2E: { const int d = sRead(dpA(sf())); return 5 + sbranch(d != sA); }
    case 0xDE: { const int o = sf(); const int d = sRead(dpA(o + sX)); return 6 + sbranch(d != sA); }
    case 0x6E: { const int a = dpA(sf()); const int v = (sRead(a) - 1) & 0xFF; sWrite(a, v); return 5 + sbranch(v != 0); }
    case 0xFE: sY = (sY - 1) & 0xFF; return 4 + sbranch(sY != 0);
    case 0x5F: sPC = sf16(); return 3;
    case 0x1F: { const int a = (sf16() + sX) & 0xFFFF; const int l = sRead(a); const int h = sRead((a + 1) & 0xFFFF); sPC = l | (h << 8); return 6; }
    case 0x3F: { const int a = sf16(); spush(sPC >> 8); spush(sPC & 0xFF); sPC = a; return 8; }
    case 0x4F: { const int u = sf(); spush(sPC >> 8); spush(sPC & 0xFF); sPC = 0xFF00 | u; return 6; }
    case 0x6F: { const int l = spop(); const int h = spop(); sPC = l | (h << 8); return 5; }
    case 0x7F: { sPSW = spop(); const int l = spop(); const int h = spop(); sPC = l | (h << 8); return 6; }
    case 0x0F: { spush(sPC >> 8); spush(sPC & 0xFF); spush(sPSW); sPSW = (sPSW | SB) & ~SI; const int l = sRead(0xFFDE); const int h = sRead(0xFFDF); sPC = l | (h << 8); return 8; }
    // stack
    case 0x0D: spush(sPSW); return 4; case 0x2D: spush(sA); return 4; case 0x4D: spush(sX); return 4; case 0x6D: spush(sY); return 4;
    case 0x8E: sPSW = spop(); return 4; case 0xAE: sA = spop(); return 4; case 0xCE: sX = spop(); return 4; case 0xEE: sY = spop(); return 4;
    case 0xEF: case 0xFF: spcHalt = 1; return 3;
  }
  return 2;
}

/* ----- S-DSP ----- */
static const int RATE[32] = {0, 2048, 1536, 1280, 1024, 768, 640, 512, 384, 320, 256, 192, 160, 128, 96, 80, 64, 48, 40, 32, 24, 20, 16, 12, 10, 8, 6, 5, 4, 3, 2, 1};

int SnesCore::dspRead(int a) {
  if ((a & 15) == 8) return voices[a >> 4].env >> 4;
  if ((a & 15) == 9) return (voices[a >> 4].out >> 8) & 0xFF;
  if (a == 0x7C) return endx;
  return dsp[a];
}
void SnesCore::dspWrite(int a, int v) {
  dsp[a] = (uint8_t)v;
  if (a == 0x4C) { for (int i = 0; i < 8; i++) if (v & (1 << i)) keyOn(i); dsp[0x4C] = 0; }
  else if (a == 0x5C) { for (int i = 0; i < 8; i++) if (v & (1 << i)) voices[i].mode = 3; }
  else if (a == 0x7C) endx = 0;
}
void SnesCore::keyOn(int i) {
  Voice& vc = voices[i];
  const int dir = dsp[0x5D] << 8, src = dsp[(i << 4) | 4];
  const int e = (dir + src * 4) & 0xFFFF;
  vc.addr = aram[e] | (aram[(e + 1) & 0xFFFF] << 8);
  vc.loopAddr = aram[(e + 2) & 0xFFFF] | (aram[(e + 3) & 0xFFFF] << 8);
  vc.on = 1; vc.bufPos = 16; vc.hist1 = vc.hist2 = 0; vc.prev = vc.cur = vc.old = vc.older = 0; vc.counter = 0;
  vc.env = 0; vc.mode = 0; vc.rateCnt = 0; vc.end = 0; vc.konDelay = 5;
  endx &= ~(1 << i);
}
void SnesCore::decodeBlock(Voice& vc, int i) {
  const int hdrB = aram[vc.addr], shift = hdrB >> 4, filter = (hdrB >> 2) & 3;
  for (int k = 0; k < 16; k++) {
    const int byte = aram[(vc.addr + 1 + (k >> 1)) & 0xFFFF];
    int n = (k & 1) ? byte & 15 : byte >> 4;
    if (n >= 8) n -= 16;
    int s = shift <= 12 ? (n << shift) >> 1 : (n < 0 ? -2048 : 0);
    const int p1 = vc.hist1, p2 = vc.hist2;
    const int h2 = p2 >> 1;
    if (filter == 1) s += (p1 >> 1) + ((-p1) >> 5);
    else if (filter == 2) s += p1 - h2 + (h2 >> 4) + ((p1 * -3) >> 6);
    else if (filter == 3) s += p1 - h2 + ((p1 * -13) >> 7) + ((h2 * 3) >> 4);
    s = clamp16(s);
    s = (int32_t)((uint32_t)s << 17) >> 16;  // keep 15 bits, doubled
    vc.hist2 = vc.hist1; vc.hist1 = s;
    vc.buf[k] = (int16_t)s;
  }
  vc.bufPos = 0;
  if (hdrB & 1) {  // end block
    endx |= 1 << i;
    if (hdrB & 2) vc.addr = vc.loopAddr;
    else { vc.end = 1; vc.addr = (vc.addr + 9) & 0xFFFF; }
  } else vc.addr = (vc.addr + 9) & 0xFFFF;
}
void SnesCore::envStep(Voice& vc, int i) {
  const int a1 = dsp[(i << 4) | 5], a2 = dsp[(i << 4) | 6], gain = dsp[(i << 4) | 7];
  if (vc.mode == 3) { vc.env -= 8; if (vc.env <= 0) { vc.env = 0; vc.on = 0; } return; }
  int rate, step = 0;
  if (a1 & 0x80) {
    if (vc.mode == 0) { rate = ((a1 & 15) << 1) | 1; step = rate == 31 ? 1024 : 32; }
    else if (vc.mode == 1) { rate = (((a1 >> 4) & 7) << 1) | 16; step = -(((vc.env - 1) >> 8) + 1); }
    else { rate = a2 & 31; step = -(((vc.env - 1) >> 8) + 1); }
  } else if (gain & 0x80) {
    rate = gain & 31;
    const int m = (gain >> 5) & 3;
    if (m == 0) step = -32;
    else if (m == 1) step = -(((vc.env - 1) >> 8) + 1);
    else if (m == 2) step = 32;
    else step = vc.env < 0x600 ? 32 : 8;
  } else { vc.env = (gain & 0x7F) << 4; return; }
  if (!RATE[rate]) return;
  if (++vc.rateCnt < RATE[rate]) return;
  vc.rateCnt = 0;
  int e = vc.env + step;
  if (e < 0) e = 0;
  if (e > 0x7FF) e = 0x7FF;
  vc.env = e;
  if (a1 & 0x80) {
    if (vc.mode == 0 && e >= 0x7E0) { vc.mode = 1; if (e > 0x7FF) vc.env = 0x7FF; }
    else if (vc.mode == 1 && (e >> 8) == (a2 >> 5)) vc.mode = 2;
  }
}
void SnesCore::dspSample() {
  const int flg = dsp[0x6C];
  if (flg & 0x80) { for (Voice& v : voices) { v.on = 0; v.env = 0; } }
  const int nr = flg & 31;
  if (nr && ++noiseCnt >= RATE[nr]) { noiseCnt = 0; const int fb = (noise ^ (noise >> 1)) & 1; noise = (noise >> 1) | (fb << 14); }
  int sumL = 0, sumR = 0, echoInL = 0, echoInR = 0;
  const int pmon = dsp[0x2D], non = dsp[0x3D], eon = dsp[0x4D];
  int prevOut = 0;
  for (int i = 0; i < 8; i++) {
    Voice& vc = voices[i];
    if (!vc.on) { vc.out = 0; prevOut = 0; continue; }
    if (vc.konDelay > 0) { vc.konDelay--; prevOut = 0; vc.out = 0; continue; }
    int pitch = (dsp[(i << 4) | 2] | (dsp[(i << 4) | 3] << 8)) & 0x3FFF;
    if (i && (pmon & (1 << i))) pitch = (pitch + ((pitch * (prevOut >> 5)) >> 10)) & 0x7FFF;
    vc.counter += pitch;
    while (vc.counter >= 0x1000) {
      vc.counter -= 0x1000;
      if (vc.bufPos >= 16) { if (vc.end) { vc.on = 0; vc.env = 0; break; } decodeBlock(vc, i); }
      vc.older = vc.old; vc.old = vc.prev; vc.prev = vc.cur; vc.cur = vc.buf[vc.bufPos++];
    }
    if (!vc.on) { vc.out = 0; prevOut = 0; continue; }
    const int t = vc.counter >> 4;
    int s = (non & (1 << i)) ? ((int32_t)((uint32_t)noise << 17) >> 16)
                             : ((SMOOTH[t * 4] * vc.older + SMOOTH[t * 4 + 1] * vc.old + SMOOTH[t * 4 + 2] * vc.prev + SMOOTH[t * 4 + 3] * vc.cur) >> 11);
    s = clamp16(s);
    envStep(vc, i);
    const int out = (s * vc.env) >> 11;
    vc.out = out; prevOut = out;
    const int vl = s8(dsp[i << 4]), vr = s8(dsp[(i << 4) | 1]);
    const int l = (out * vl) >> 7, r = (out * vr) >> 7;
    sumL += l; sumR += r;
    if (eon & (1 << i)) { echoInL += l; echoInR += r; }
  }
  // echo
  const int esa = dsp[0x6D] << 8, edl = dsp[0x7D] & 15, len = edl ? edl * 2048 : 4;
  const int ea = (esa + echoPos) & 0xFFFF;
  const int eL = (int32_t)((uint32_t)(aram[ea] | (aram[(ea + 1) & 0xFFFF] << 8)) << 16) >> 17;
  const int eR = (int32_t)((uint32_t)(aram[(ea + 2) & 0xFFFF] | (aram[(ea + 3) & 0xFFFF] << 8)) << 16) >> 17;
  firL[firPos] = (int16_t)eL; firR[firPos] = (int16_t)eR;
  int fl = 0, fr = 0;
  for (int k = 0; k < 8; k++) { const int c = s8(dsp[(k << 4) | 0x0F]), idx = (firPos + k + 1) & 7; fl += (firL[idx] * c) >> 6; fr += (firR[idx] * c) >> 6; }
  firPos = (firPos + 1) & 7;
  fl = clamp16(fl); fr = clamp16(fr);
  if (!(flg & 0x20)) {
    const int efb = s8(dsp[0x0D]);
    const int wl = clamp16(echoInL + ((fl * efb) >> 7)) & ~1, wr2 = clamp16(echoInR + ((fr * efb) >> 7)) & ~1;
    aram[ea] = wl & 0xFF; aram[(ea + 1) & 0xFFFF] = (wl >> 8) & 0xFF; aram[(ea + 2) & 0xFFFF] = wr2 & 0xFF; aram[(ea + 3) & 0xFFFF] = (wr2 >> 8) & 0xFF;
  }
  echoPos += 4;
  if (echoPos >= len) echoPos = 0;
  int L = clamp16(((sumL * s8(dsp[0x0C])) >> 7) + ((fl * s8(dsp[0x2C])) >> 7));
  int R = clamp16(((sumR * s8(dsp[0x1C])) >> 7) + ((fr * s8(dsp[0x3C])) >> 7));
  if (flg & 0x40) { L = 0; R = 0; }
  if (audioSink && audioOn) audioSink(audioUser, (int16_t)L, (int16_t)R);
}

/* ============================ Run loop ============================ */
static const int LINE = 1364, LINES = 262, HDMA_AT = 1104;
int SnesCore::irqPoint() {
  const int m = (nmitimen >> 4) & 3;
  if (m == 0) return -1;
  if (m == 1) return htime < 340 ? htime * 4 + 14 : -1;
  if (m == 2) return vcount == vtime ? 10 : -1;
  return vcount == vtime && htime < 340 ? htime * 4 + 14 : -1;
}
void SnesCore::runLine() {
  if (vcount == 0) { inVblank = 0; nmiFlag = 0; hdmaInit(); }
  if (vcount >= 1 && vcount <= 224) renderLine(vcount);
  const int irqAt = irqPoint();
  bool irqDone = false, hdmaDone = vcount >= 225;
  if (vcount == 225) {
    inVblank = 1; nmiFlag = 0x80;
    if (nmitimen & 0x80) nmiPending = 1;
    if (!(inidisp & 0x80)) oamAddr = oamReload;
    if (nmitimen & 1) { joyAuto = pad1; joyAuto2 = pad2; padShift = 0xFFFF; padShift2 = 0xFFFF; hvbjoyBusy = 1; }
    frameDone = 1;
  }
  if (vcount == 228) hvbjoyBusy = 0;
  while (cyc < LINE) {
    if (!irqDone && irqAt >= 0 && cyc >= irqAt) { irqDone = true; timeup = 0x80; irqLine = 1; }
    if (nmiPending) { nmiPending = 0; waiting = 0; cyc += 12; interrupt(0xFFEA, 0xFFFA, false); }
    else if (irqLine && !fI) { waiting = 0; cyc += 12; interrupt(0xFFEE, 0xFFFE, false); }
    else if (irqLine && waiting) waiting = 0;
    if (waiting || stopped) {
      int next = LINE;
      if (!irqDone && irqAt > cyc && irqAt < next) next = irqAt;
      if (!hdmaDone && HDMA_AT > cyc && HDMA_AT < next) next = HDMA_AT;
      cyc = next;
    } else step();
    if (!hdmaDone && cyc >= HDMA_AT) { hdmaDone = true; if (hdmaen) hdmaRun(); }
  }
  // uma DMA longa pode passar do fim da linha: o HDMA desta linha ainda precisa acontecer
  if (!hdmaDone && hdmaen) hdmaRun();
  if (!irqDone && irqAt >= 0 && irqAt < LINE + 20) { timeup = 0x80; irqLine = 1; }
  cyc -= LINE; master += LINE;
  vcount++;
}
void SnesCore::runFrameInner() {
  frameDone = 0;
  int guard = 0;
  while (!frameDone && guard++ < LINES * 2) {
    runLine();
    if (vcount >= LINES) vcount = 0;
  }
  while (vcount != 0) { runLine(); if (vcount >= LINES) vcount = 0; }
  apuCatchUp();
  if (master > 2e9) { const double s = master; master -= s; spcCycles -= s * SPC_RATIO; }
}
void SnesCore::applyRamCheats() { for (auto& c : ramCheats) wram[c.first] = (uint8_t)c.second; }
void SnesCore::runFrame() { applyRamCheats(); runFrameInner(); }

/* ============================ Reset / load ============================ */
void SnesCore::reset() {
  std::memset(wram, 0x55, sizeof wram);
  std::memset(vram, 0, sizeof vram);
  std::memset(cgram, 0, sizeof cgram);
  std::memset(oam, 0, sizeof oam);
  std::memset(aram, 0, sizeof aram);
  std::memset(dsp, 0, sizeof dsp);
  dsp[0x6C] = 0xE0;
  for (Voice& v : voices) { v.on = 0; v.env = 0; v.out = 0; }
  std::memset(timerDiv, 0, sizeof timerDiv); std::memset(timerOut, 0, sizeof timerOut);
  std::memset(timerStage, 0, sizeof timerStage); std::memset(timerCnt, 0, sizeof timerCnt);
  spcCtl = 0x80; dspAddr = 0;
  std::memset(apuIn, 0, sizeof apuIn);
  iplReset();
  spcHalt = 0; spcCycles = 0; echoPos = 0;
  std::memset(firL, 0, sizeof firL); std::memset(firR, 0, sizeof firR);
  master = 0; cyc = 0; vcount = 0; frameDone = 0; inVblank = 0;
  nmitimen = 0; nmiFlag = 0; timeup = 0; htime = 0x1FF; vtime = 0x1FF; hdmaen = 0; memsel = 0; romSpeed = 8;
  irqLine = 0; nmiPending = 0; joyAuto = 0; joyAuto2 = 0; padShift = padShift2 = 0; hvbjoyBusy = 0;
  std::memset(dmaRegs, 0xFF, sizeof dmaRegs);
  inidisp = 0x80; bgmode = 0; tm = 0; ts = 0; cgadsub = 0; cgwsel = 0; setini = 0; fixedColor = 0;
  lutReady = false;
  A = 0; X = 0; Y = 0; S = 0x1FF; D = 0; DB = 0; PB = 0; E = 1; setP(0x34); waiting = 0; stopped = 0;
  const int n = (int)rom.size();
  PC = rom[(hdr + 0x3C) % n] | (rom[(hdr + 0x3D) % n] << 8);
  for (int i = 0; i < W * H; i++) frame[i] = 0xFF000000u;
}
bool SnesCore::loadRom(const uint8_t* data, size_t len) { loadCart(data, len); reset(); return true; }

/* ============================ Cheats ============================ */
// Pro Action Replay: AAAAAAVV (8 hex). Também "AAAAAA:VV" / "7E1234=05".
int SnesCore::setCheats(const std::vector<std::string>& list) {
  cheatPatch.clear(); ramCheats.clear();
  int ok = 0;
  for (const std::string& raw : list) {
    std::string c;
    for (char ch : raw) {
      char u = (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : ch;
      if ((u >= '0' && u <= '9') || (u >= 'A' && u <= 'F')) c += u;
    }
    if (c.size() != 8) continue;
    const int addr = (int)std::stoul(c.substr(0, 6), nullptr, 16), val = (int)std::stoul(c.substr(6), nullptr, 16);
    const int bank = addr >> 16;
    if (bank == 0x7E || bank == 0x7F || ((bank & 0x40) == 0 && (addr & 0xFFFF) < 0x2000))
      ramCheats.emplace_back(bank == 0x7E || bank == 0x7F ? addr & 0x1FFFF : addr & 0x1FFF, val);
    else cheatPatch[addr] = val;
    ok++;
  }
  cheatOn = !cheatPatch.empty();
  return ok;
}

void SnesCore::setSave(const uint8_t* data, size_t len) {
  if (data && !sram.empty()) std::memcpy(sram.data(), data, std::min(len, sram.size()));
}

/* ============================ Estado ============================ */
// Formato: "RGS2" e depois campos [tamanho do nome][nome][u32 tamanho][bytes].
// Ao carregar, só os campos presentes e com o tamanho certo são aplicados.
namespace {
struct Writer {
  std::vector<uint8_t> out;
  void raw(const char* name, const void* p, size_t n) {
    const size_t nl = std::strlen(name);
    out.push_back((uint8_t)nl);
    out.insert(out.end(), name, name + nl);
    const uint32_t len = (uint32_t)n;
    const uint8_t* lp = (const uint8_t*)&len;
    out.insert(out.end(), lp, lp + 4);
    const uint8_t* b = (const uint8_t*)p;
    out.insert(out.end(), b, b + n);
  }
  void i32(const char* name, int& v) { int32_t x = v; raw(name, &x, 4); }
  void f64(const char* name, double& v) { raw(name, &v, 8); }
  void bytes(const char* name, void* p, size_t n) { raw(name, p, n); }
};
struct Reader {
  std::unordered_map<std::string, std::pair<const uint8_t*, uint32_t>> m;
  bool parse(const uint8_t* d, size_t len) {
    if (len < 4 || std::memcmp(d, "RGS2", 4) != 0) return false;
    size_t p = 4;
    while (p < len) {
      const size_t nl = d[p++];
      if (p + nl + 4 > len) return false;
      std::string name((const char*)d + p, nl); p += nl;
      uint32_t n; std::memcpy(&n, d + p, 4); p += 4;
      if (p + n > len) return false;
      m[name] = {d + p, n}; p += n;
    }
    return true;
  }
  void i32(const char* name, int& v) { auto it = m.find(name); if (it != m.end() && it->second.second == 4) { int32_t x; std::memcpy(&x, it->second.first, 4); v = x; } }
  void f64(const char* name, double& v) { auto it = m.find(name); if (it != m.end() && it->second.second == 8) std::memcpy(&v, it->second.first, 8); }
  void bytes(const char* name, void* p, size_t n) { auto it = m.find(name); if (it != m.end() && it->second.second == n) std::memcpy(p, it->second.first, n); }
};
}  // namespace

template <class Ar>
void SnesCore::visit(Ar& ar) {
  ar.bytes("wram", wram, sizeof wram); ar.bytes("vram", vram, sizeof vram); ar.bytes("cgram", cgram, sizeof cgram); ar.bytes("oam", oam, sizeof oam);
  if (!sram.empty()) ar.bytes("sram", sram.data(), sram.size());
  ar.bytes("aram", aram, sizeof aram); ar.bytes("dsp", dsp, sizeof dsp); ar.bytes("dmaRegs", dmaRegs, sizeof dmaRegs);
  // cpu
  ar.i32("A", A); ar.i32("X", X); ar.i32("Y", Y); ar.i32("S", S); ar.i32("D", D); ar.i32("DB", DB); ar.i32("PB", PB); ar.i32("PC", PC); ar.i32("E", E);
  ar.i32("fN", fN); ar.i32("fV", fV); ar.i32("fM", fM); ar.i32("fX", fX); ar.i32("fD", fD); ar.i32("fI", fI); ar.i32("fZ", fZ); ar.i32("fC", fC);
  ar.i32("waiting", waiting); ar.i32("stopped", stopped);
  // io
  ar.i32("cyc", cyc); ar.f64("master", master); ar.i32("vcount", vcount); ar.i32("nmitimen", nmitimen); ar.i32("nmiFlag", nmiFlag); ar.i32("timeup", timeup);
  ar.i32("htime", htime); ar.i32("vtime", vtime); ar.i32("wrio", wrio); ar.i32("mulA", mulA); ar.i32("mulB", mulB); ar.i32("divA", divA); ar.i32("rdmpy", rdmpy);
  ar.i32("rddiv", rddiv); ar.i32("hdmaen", hdmaen); ar.i32("memsel", memsel); ar.i32("romSpeed", romSpeed); ar.i32("irqLine", irqLine); ar.i32("nmiPending", nmiPending);
  ar.i32("wramAddr", wramAddr); ar.i32("joyAuto", joyAuto); ar.i32("joyAuto2", joyAuto2);
  ar.i32("padLatch", padLatch); ar.i32("padShift", padShift); ar.i32("padShift2", padShift2); ar.i32("hvbjoyBusy", hvbjoyBusy); ar.i32("openBus", openBus);
  ar.i32("inVblank", inVblank);
  // ppu
  ar.i32("inidisp", inidisp); ar.i32("obsel", obsel); ar.i32("oamAddr", oamAddr); ar.i32("oamReload", oamReload); ar.i32("oamLatch", oamLatch); ar.i32("bgmode", bgmode); ar.i32("mosaic", mosaic);
  ar.bytes("bgsc", bgsc, sizeof bgsc); ar.bytes("bgnba", bgnba, sizeof bgnba); ar.bytes("hofs", hofs, sizeof hofs); ar.bytes("vofs", vofs, sizeof vofs);
  ar.i32("scrollPrev", scrollPrev); ar.i32("scrollPrev2", scrollPrev2); ar.i32("m7prev", m7prev);
  ar.i32("vmain", vmain); ar.i32("vmadd", vmadd); ar.i32("vramLatch", vramLatch); ar.i32("m7sel", m7sel); ar.i32("m7a", m7a); ar.i32("m7b", m7b); ar.i32("m7c", m7c); ar.i32("m7d", m7d);
  ar.i32("m7x", m7x); ar.i32("m7y", m7y); ar.i32("m7hofs", m7hofs); ar.i32("m7vofs", m7vofs); ar.i32("cgadd", cgadd); ar.i32("cgLatch", cgLatch);
  ar.i32("w12sel", w12sel); ar.i32("w34sel", w34sel); ar.i32("wobjsel", wobjsel); ar.i32("wh0", wh0); ar.i32("wh1", wh1); ar.i32("wh2", wh2); ar.i32("wh3", wh3);
  ar.i32("wbglog", wbglog); ar.i32("wobjlog", wobjlog); ar.i32("tm", tm); ar.i32("ts", ts); ar.i32("tmw", tmw); ar.i32("tsw", tsw); ar.i32("cgwsel", cgwsel); ar.i32("cgadsub", cgadsub);
  ar.i32("fixedColor", fixedColor); ar.i32("setini", setini); ar.i32("mpy", mpy); ar.i32("oamPrio", oamPrio);
  ar.i32("latchH", latchH); ar.i32("latchV", latchV); ar.i32("hToggle", hToggle); ar.i32("vToggle", vToggle); ar.i32("cgReadToggle", cgReadToggle); ar.i32("latched", latched);
  // spc
  ar.i32("sA", sA); ar.i32("sX", sX); ar.i32("sY", sY); ar.i32("sSP", sSP); ar.i32("sPC", sPC); ar.i32("sPSW", sPSW); ar.f64("spcCycles", spcCycles); ar.i32("spcHalt", spcHalt);
  ar.i32("spcCtl", spcCtl); ar.i32("dspAddr", dspAddr);
  ar.bytes("timerDiv", timerDiv, sizeof timerDiv); ar.bytes("timerOut", timerOut, sizeof timerOut); ar.bytes("timerStage", timerStage, sizeof timerStage); ar.bytes("timerCnt", timerCnt, sizeof timerCnt);
  ar.bytes("apuIn", apuIn, sizeof apuIn); ar.bytes("apuOut", apuOut, sizeof apuOut);
  ar.i32("ipl.on", ipl.on); ar.i32("ipl.state", ipl.state); ar.i32("ipl.expect", ipl.expect); ar.i32("ipl.echo", ipl.echo); ar.i32("ipl.addr", ipl.addr); ar.i32("ipl.pending", ipl.pending); ar.f64("ipl.at", ipl.at);
  ar.i32("noise", noise); ar.i32("echoPos", echoPos); ar.i32("endx", endx);
  ar.i32("noiseCnt", noiseCnt); ar.i32("dspCycle", dspCycle); ar.i32("firPos", firPos); ar.bytes("firL", firL, sizeof firL); ar.bytes("firR", firR, sizeof firR);
  for (int i = 0; i < 8; i++) {
    Voice& v = voices[i];
    char n[24];
#define VF(f) std::snprintf(n, sizeof n, "v%d." #f, i); ar.i32(n, v.f);
    VF(on) VF(addr) VF(loopAddr) VF(bufPos) VF(hist1) VF(hist2) VF(prev) VF(cur) VF(old) VF(older) VF(counter) VF(env) VF(mode) VF(rateCnt) VF(end) VF(out) VF(konDelay)
#undef VF
    std::snprintf(n, sizeof n, "v%d.buf", i); ar.bytes(n, v.buf, sizeof v.buf);
  }
  for (int i = 0; i < 8; i++) {
    Hdma& h = hdmaState[i];
    char n[24];
#define HF(f) std::snprintf(n, sizeof n, "h%d." #f, i); ar.i32(n, h.f);
    HF(addr) HF(ind) HF(lines) HF(doTransfer) HF(done)
#undef HF
  }
}

std::vector<uint8_t> SnesCore::getState() {
  Writer w;
  w.out.insert(w.out.end(), {'R', 'G', 'S', '2'});
  visit(w);
  return std::move(w.out);
}

bool SnesCore::setState(const uint8_t* data, size_t len) {
  Reader r;
  if (!r.parse(data, len)) return false;
  visit(r);
  if (fX) { X &= 0xFF; Y &= 0xFF; }
  lutReady = false; saveDirty = false; frameDone = 0;
  return true;
}

uint32_t SnesCore::stateHash() {
  const std::vector<uint8_t> s = getState();
  uint32_t h = 0x811c9dc5u;
  for (uint8_t b : s) h = (h ^ b) * 16777619u;
  return h;
}
