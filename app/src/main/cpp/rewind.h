// Voltar no tempo: guarda o estado do jogo a cada poucos quadros, só com a diferença
// em relação ao estado anterior (XOR + compressão de zeros), num limite de memória.
#pragma once
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>

class Rewinder {
public:
  explicit Rewinder(size_t budgetBytes = 48u << 20) : budget(budgetBytes) {}

  void clear() { deltas.clear(); head.clear(); used = 0; }
  bool empty() const { return deltas.empty(); }
  size_t count() const { return deltas.size(); }

  // guarda um estado novo (o mais recente)
  void push(const std::vector<uint8_t>& st) {
    if (head.empty() || head.size() != st.size()) { clear(); head = st; return; }
    // delta = novo XOR anterior: aplicado no novo, devolve o anterior
    scratch.resize(st.size());
    for (size_t i = 0; i < st.size(); i++) scratch[i] = st[i] ^ head[i];
    std::vector<uint8_t> packed;
    pack(scratch, packed);
    used += packed.size();
    deltas.push_back(std::move(packed));
    head = st;
    while (used > budget && !deltas.empty()) { used -= deltas.front().size(); deltas.pop_front(); }
  }

  // volta um passo: devolve o estado anterior ao mais recente (e ele passa a ser o mais recente)
  bool pop(std::vector<uint8_t>& out) {
    if (deltas.empty()) return false;
    std::vector<uint8_t>& d = deltas.back();
    if (!unpackXor(d, head)) { clear(); return false; }
    used -= d.size();
    deltas.pop_back();
    out = head;
    return true;
  }

private:
  // formato: [u32 zeros][u32 n][n bytes] repetido
  static void pack(const std::vector<uint8_t>& x, std::vector<uint8_t>& out) {
    const size_t n = x.size();
    size_t i = 0;
    out.clear();
    out.reserve(256);
    while (i < n) {
      size_t z = i;
      while (z < n && x[z] == 0) z++;
      size_t lit = z;
      // termina o trecho literal quando aparecem 8 zeros seguidos
      size_t zeros = 0;
      while (lit < n) {
        if (x[lit] == 0) { if (++zeros >= 8) { lit -= 7; break; } }
        else zeros = 0;
        lit++;
      }
      const uint32_t zc = (uint32_t)(z - i), lc = (uint32_t)(lit - z);
      put32(out, zc); put32(out, lc);
      out.insert(out.end(), x.begin() + (long)z, x.begin() + (long)lit);
      i = lit;
    }
  }
  static bool unpackXor(const std::vector<uint8_t>& p, std::vector<uint8_t>& dst) {
    size_t pos = 0, i = 0;
    while (pos < p.size()) {
      if (pos + 8 > p.size()) return false;
      uint32_t zc, lc;
      std::memcpy(&zc, &p[pos], 4); std::memcpy(&lc, &p[pos + 4], 4); pos += 8;
      i += zc;
      if (i + lc > dst.size() || pos + lc > p.size()) return false;
      for (uint32_t k = 0; k < lc; k++) dst[i + k] ^= p[pos + k];
      i += lc; pos += lc;
    }
    return true;
  }
  static void put32(std::vector<uint8_t>& o, uint32_t v) { const uint8_t* b = (const uint8_t*)&v; o.insert(o.end(), b, b + 4); }

  std::deque<std::vector<uint8_t>> deltas;
  std::vector<uint8_t> head, scratch;
  size_t used = 0, budget;
};
