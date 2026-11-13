// Juntar PDFs em um só (sem bibliotecas além da zlib). Tradução do módulo PDFM do app em HTML.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>

#include "engine.h"
#include "util.hpp"

namespace xp {
namespace {

struct PV;
using P = std::shared_ptr<PV>;
struct PV {
  enum T { Null, Num, Bool, Name, Str, Arr, Dict, Ref, Kw, Stream } t = Null;
  double num = 0;
  bool b = false;
  std::string s;                               // nome, palavra ou bytes da string
  std::vector<P> arr;
  std::vector<std::pair<std::string, P>> dict;  // mantém a ordem, como o Map do JS
  int n = 0, g = 0;                            // referência
  P sdict;                                     // dicionário do stream
  const uint8_t* data = nullptr;
  size_t len = 0;

  P get(const std::string& k) const {
    for (auto& e : dict) if (e.first == k) return e.second;
    return nullptr;
  }
  bool has(const std::string& k) const {
    for (auto& e : dict) if (e.first == k) return true;
    return false;
  }
  void set(const std::string& k, P v) {
    for (auto& e : dict) if (e.first == k) { e.second = v; return; }
    dict.emplace_back(k, v);
  }
  void del(const std::string& k) {
    for (size_t i = 0; i < dict.size(); i++) if (dict[i].first == k) { dict.erase(dict.begin() + i); return; }
  }
};
P mk(PV::T t) { auto p = std::make_shared<PV>(); p->t = t; return p; }
P mkNum(double v) { auto p = mk(PV::Num); p->num = v; return p; }
P mkName(const std::string& s) { auto p = mk(PV::Name); p->s = s; return p; }
P mkRef(int n) { auto p = mk(PV::Ref); p->n = n; return p; }
P dget(const P& d, const std::string& k) { return d && d->t == PV::Dict ? d->get(k) : nullptr; }
bool isName(const P& v, const char* s) { return v && v->t == PV::Name && v->s == s; }

bool WS(int c) { return c == 0x20 || c == 0x0a || c == 0x0d || c == 0x09 || c == 0x0c || c == 0x00; }
bool DELIM(int c) { return c == 0x28 || c == 0x29 || c == 0x3c || c == 0x3e || c == 0x5b || c == 0x5d || c == 0x7b || c == 0x7d || c == 0x2f || c == 0x25; }

bool soDigitos(const std::string& w) {
  if (w.empty()) return false;
  for (char c : w) if (c < '0' || c > '9') return false;
  return true;
}
bool ehNumero(const std::string& w) {  // ^[+-]?(\d+\.?\d*|\.\d+)$
  size_t i = 0;
  if (i < w.size() && (w[i] == '+' || w[i] == '-')) i++;
  size_t a = i;
  while (i < w.size() && isdigit((unsigned char)w[i])) i++;
  bool antes = i > a;
  if (i < w.size() && w[i] == '.') {
    i++;
    size_t b = i;
    while (i < w.size() && isdigit((unsigned char)w[i])) i++;
    if (!antes && i == b) return false;
  } else if (!antes) return false;
  return i == w.size();
}
double numOuNaN(const std::string& w) {
  if (w.empty()) return NAN;
  char* e;
  double v = strtod(w.c_str(), &e);
  return *e ? NAN : v;
}

struct Lexer {
  const uint8_t* b;
  size_t n, p;
  Lexer(const uint8_t* b_, size_t n_, size_t p_ = 0) : b(b_), n(n_), p(p_) {}
  int at(size_t i) const { return i < n ? b[i] : -1; }
  void skip() {
    for (;;) {
      while (p < n && WS(b[p])) p++;
      if (at(p) == 0x25) { while (p < n && b[p] != 0x0a && b[p] != 0x0d) p++; }
      else break;
    }
  }
  std::string word() {
    size_t s = p;
    while (p < n && !WS(b[p]) && !DELIM(b[p])) p++;
    return std::string((const char*)b + s, p - s);
  }
  P value(int depth = 0) {
    if (depth > 200) throw Erro("PDF muito aninhado");
    skip();
    int c = at(p);
    if (c == 0x3c && at(p + 1) == 0x3c) {
      p += 2;
      auto d = mk(PV::Dict);
      for (;;) {
        skip();
        if (at(p) == 0x3e && at(p + 1) == 0x3e) { p += 2; break; }
        if (p >= n) throw Erro("dicionário quebrado");
        P k = value(depth + 1);
        if (!k || k->t != PV::Name) throw Erro("chave inválida");
        d->set(k->s, value(depth + 1));
      }
      return d;
    }
    if (c == 0x3c) {
      p++;
      std::string hex;
      while (p < n && b[p] != 0x3e) { if (!WS(b[p])) hex += (char)b[p]; p++; }
      p++;
      auto v = mk(PV::Str);
      for (size_t i = 0; i < (hex.size() + 1) / 2; i++) {
        std::string pr = (hex.substr(i * 2, 2) + "0").substr(0, 2);
        char* e;
        long x = strtol(pr.c_str(), &e, 16);
        v->s += (char)(*e && e == pr.c_str() ? 0 : x);
      }
      return v;
    }
    if (c == 0x28) {
      p++;
      int depthS = 1;
      auto v = mk(PV::Str);
      std::string& out = v->s;
      while (p < n) {
        int ch = b[p++];
        if (ch == 0x5c) {
          int m = at(p++);
          switch (m) {
            case 110: out += '\n'; break;
            case 114: out += '\r'; break;
            case 116: out += '\t'; break;
            case 98: out += '\b'; break;
            case 102: out += '\f'; break;
            case 40: case 41: case 92: out += (char)m; break;
            default:
              if (m >= 0x30 && m <= 0x37) {
                int o = m - 0x30;
                for (int k = 0; k < 2 && at(p) >= 0x30 && at(p) <= 0x37; k++) o = o * 8 + (b[p++] - 0x30);
                out += (char)(o & 255);
              } else if (m == 0x0d) {
                if (at(p) == 0x0a) p++;
              } else if (m != 0x0a && m >= 0) out += (char)m;
          }
        } else if (ch == 0x28) { depthS++; out += (char)ch; }
        else if (ch == 0x29) { if (--depthS == 0) break; out += (char)ch; }
        else out += (char)ch;
      }
      return v;
    }
    if (c == 0x2f) { p++; return mkName(word()); }
    if (c == 0x5b) {
      p++;
      auto a = mk(PV::Arr);
      for (;;) {
        skip();
        if (at(p) == 0x5d) { p++; break; }
        if (p >= n) throw Erro("lista quebrada");
        a->arr.push_back(value(depth + 1));
      }
      return a;
    }
    std::string w = word();
    if (w.empty()) { p++; return nullptr; }
    if (w == "true" || w == "false") { auto v = mk(PV::Bool); v->b = w == "true"; return v; }
    if (w == "null") return mk(PV::Null);
    if (ehNumero(w)) {
      if (soDigitos(w)) {
        size_t save = p;
        skip();
        std::string w2 = word();
        if (soDigitos(w2)) {
          skip();
          if (at(p) == 0x52 && (p + 1 >= n || WS(b[p + 1]) || DELIM(b[p + 1]))) {
            p++;
            auto r = mkRef(atoi(w.c_str()));
            r->g = atoi(w2.c_str());
            return r;
          }
        }
        p = save;
      }
      return mkNum(strtod(w.c_str(), nullptr));
    }
    auto k = mk(PV::Kw);
    k->s = w;
    return k;
  }
};

Bytes unpredict(Bytes data, const P& parms) {
  P pr = dget(parms, "Predictor");
  int pred = pr ? (int)pr->num : 1;
  if (pred < 10) return data;
  auto num = [&](const char* k, int def) { P v = dget(parms, k); return v ? (int)v->num : def; };
  int cols = num("Columns", 1), colors = num("Colors", 1), bpc = num("BitsPerComponent", 8);
  int bpp = std::max(1, (colors * bpc + 7) / 8), rl = (cols * colors * bpc + 7) / 8;
  if (rl <= 0) return data;
  size_t rows = data.size() / (rl + 1);
  Bytes out(rows * rl);
  std::vector<uint8_t> prev(rl, 0);
  for (size_t r = 0; r < rows; r++) {
    int ft = data[r * (rl + 1)];
    const uint8_t* row = &data[r * (rl + 1) + 1];
    uint8_t* cur = &out[r * rl];
    for (int i = 0; i < rl; i++) {
      int a = i >= bpp ? cur[i - bpp] : 0, up = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
      int v = row[i];
      if (ft == 1) v += a;
      else if (ft == 2) v += up;
      else if (ft == 3) v += (a + up) / 2;
      else if (ft == 4) {
        int pp = a + up - c, pa = std::abs(pp - a), pb = std::abs(pp - up), pc = std::abs(pp - c);
        v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? up : c);
      }
      cur[i] = (uint8_t)(v & 255);
    }
    prev.assign(cur, cur + rl);
  }
  return out;
}

Bytes decodeStream(const P& st) {
  P f = dget(st->sdict, "Filter"), parms = dget(st->sdict, "DecodeParms");
  std::vector<std::string> fs;
  std::vector<P> ps;
  if (f) { if (f->t == PV::Arr) for (auto& x : f->arr) fs.push_back(x ? x->s : ""); else fs.push_back(f->s); }
  if (parms) { if (parms->t == PV::Arr) ps = parms->arr; else ps.push_back(parms); }
  Bytes d(st->data, st->data + st->len);
  for (size_t i = 0; i < fs.size(); i++) {
    if (fs[i] != "FlateDecode") throw Erro("filtro não suportado: " + fs[i]);
    d = unpredict(inflar(d.data(), d.size(), 15, true), i < ps.size() ? ps[i] : nullptr);
  }
  return d;
}

std::string latin1(const uint8_t* b, size_t n) { return std::string((const char*)b, n); }

class Doc {
 public:
  Doc(const uint8_t* b, size_t n) : b_(b), n_(n) {}
  P trailer;

  void load() {
    size_t t0 = n_ > 2048 ? n_ - 2048 : 0;
    std::string tail = latin1(b_ + t0, n_ - t0);
    long last = -1;
    for (size_t pos = tail.find("startxref"); pos != std::string::npos; pos = tail.find("startxref", pos + 1)) {
      size_t i = pos + 9, a;
      while (i < tail.size() && isspace((unsigned char)tail[i])) i++;
      if (i == pos + 9) continue;
      a = i;
      while (i < tail.size() && isdigit((unsigned char)tail[i])) i++;
      if (i > a) last = atol(tail.substr(a, i - a).c_str());
    }
    bool ok = false;
    if (last >= 0) {
      try { readXref((size_t)last); ok = trailer && !xref_.empty(); } catch (...) {}
    }
    if (!ok) { xref_.clear(); trailer = nullptr; scan(); }
    if (dget(trailer, "Encrypt")) throw Erro("PDF protegido por senha");
  }

  P get(const P& ref) {
    if (!ref || ref->t != PV::Ref) return ref;
    auto c = cache_.find(ref->n);
    if (c != cache_.end()) return c->second;
    cache_[ref->n] = mk(PV::Null);  // evita laço infinito
    P v = mk(PV::Null);
    auto e = xref_.find(ref->n);
    if (e != xref_.end()) {
      try {
        if (e->second.stm >= 0) v = fromObjStm(e->second.stm, ref->n, e->second.idx);
        else v = parseAt(e->second.o);
      } catch (...) {}
    }
    if (!v) v = mk(PV::Null);
    cache_[ref->n] = v;
    return v;
  }

 private:
  struct Ent { size_t o = 0; int g = 0; long stm = -1; int idx = 0; };
  const uint8_t* b_;
  size_t n_;
  std::map<int, Ent> xref_;
  std::unordered_map<int, P> cache_;
  struct Stm { Bytes data; size_t first; std::map<int, size_t> offs; };
  std::map<long, std::shared_ptr<Stm>> stms_;

  void readXref(size_t off) {
    std::set<size_t> seen;
    bool tem = true;
    while (tem && !seen.count(off)) {
      seen.insert(off);
      if (off >= n_) throw Erro("xref fora do arquivo");
      Lexer L(b_, n_, off);
      L.skip();
      if (L.p + 4 <= n_ && latin1(b_ + L.p, 4) == "xref") {
        L.p += 4;
        for (;;) {
          L.skip();
          size_t save = L.p;
          std::string w = L.word();
          if (w == "trailer") break;
          double start = numOuNaN(w);
          L.skip();
          double cnt = numOuNaN(L.word());
          if (std::isnan(start) || std::isnan(cnt)) { L.p = save; break; }
          for (long i = 0; i < (long)cnt; i++) {
            L.skip(); double o = numOuNaN(L.word());
            L.skip(); double g = numOuNaN(L.word());
            L.skip(); std::string t = L.word();
            int nn = (int)start + (int)i;
            if (!xref_.count(nn) && t == "n" && !std::isnan(o)) { Ent e; e.o = (size_t)o; e.g = std::isnan(g) ? 0 : (int)g; xref_[nn] = e; }
            if (L.p >= n_) break;
          }
        }
        P tr = L.value();
        if (!trailer) trailer = tr;
        if (P xs = dget(tr, "XRefStm")) { try { readXrefStream((size_t)xs->num); } catch (...) {} }
        P prev = dget(tr, "Prev");
        if (prev) off = (size_t)prev->num; else tem = false;
      } else {
        long prev = readXrefStream(off);
        if (prev >= 0) off = (size_t)prev; else tem = false;
      }
    }
  }

  long readXrefStream(size_t off) {
    P st = parseAt(off);
    if (!st || st->t != PV::Stream) throw Erro("xref inválido");
    if (!trailer) trailer = st->sdict;
    Bytes data = decodeStream(st);
    P Wv = dget(st->sdict, "W"), Sz = dget(st->sdict, "Size"), Ix = dget(st->sdict, "Index");
    if (!Wv || Wv->arr.size() < 3 || !Sz) throw Erro("xref inválido");
    int Wa[3];
    for (int i = 0; i < 3; i++) Wa[i] = Wv->arr[i] ? (int)Wv->arr[i]->num : 0;
    std::vector<long> idx;
    if (Ix) for (auto& x : Ix->arr) idx.push_back(x ? (long)x->num : 0);
    else { idx.push_back(0); idx.push_back((long)Sz->num); }
    size_t rl = Wa[0] + Wa[1] + Wa[2], p = 0;
    auto rd = [&](int k) { unsigned long long v = 0; for (int i = 0; i < k; i++) v = v * 256 + data[p++]; return v; };
    for (size_t k = 0; k + 1 < idx.size(); k += 2) {
      for (long i = 0; i < idx[k + 1]; i++) {
        if (p + rl > data.size()) break;
        unsigned long long t = Wa[0] ? rd(Wa[0]) : 1, a = rd(Wa[1]), c = rd(Wa[2]);
        int nn = (int)(idx[k] + i);
        if (xref_.count(nn)) continue;
        Ent e;
        if (t == 1) { e.o = (size_t)a; e.g = (int)c; xref_[nn] = e; }
        else if (t == 2) { e.stm = (long)a; e.idx = (int)c; xref_[nn] = e; }
      }
    }
    P prev = dget(st->sdict, "Prev");
    return prev ? (long)prev->num : -1;
  }

  void scan() {
    // procura "N G obj" no arquivo inteiro
    for (size_t i = 0; i + 3 <= n_; i++) {
      if (b_[i] != 'o' || b_[i + 1] != 'b' || b_[i + 2] != 'j') continue;
      if (i + 3 < n_ && (isalnum(b_[i + 3]) || b_[i + 3] == '_')) continue;
      long j = (long)i - 1;
      auto sp = [](int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; };
      long w1 = j;
      while (j >= 0 && sp(b_[j])) j--;
      if (j == w1) continue;
      long ge = j;
      while (j >= 0 && isdigit(b_[j])) j--;
      if (j == ge) continue;
      int g = atoi(latin1(b_ + j + 1, ge - j).c_str());
      long w2 = j;
      while (j >= 0 && sp(b_[j])) j--;
      if (j == w2) continue;
      long ne = j;
      while (j >= 0 && isdigit(b_[j])) j--;
      if (j == ne) continue;
      int num = atoi(latin1(b_ + j + 1, ne - j).c_str());
      Ent e;
      e.o = (size_t)(j + 1);
      e.g = g;
      xref_[num] = e;
    }
    // "trailer" mais para o fim
    long t = -1;
    for (long i = (long)n_ - 7; i >= 0; i--)
      if (memcmp(b_ + i, "trailer", 7) == 0) { t = i; break; }
    if (t >= 0) {
      Lexer L(b_, n_, t + 7);
      trailer = L.value();
    } else {
      for (auto& kv : xref_) {
        P o = get(mkRef(kv.first));
        if (o && o->t == PV::Dict && isName(o->get("Type"), "Catalog")) {
          trailer = mk(PV::Dict);
          trailer->set("Root", mkRef(kv.first));
          break;
        }
      }
    }
    if (!trailer) throw Erro("não achei o começo do PDF");
  }

  P parseAt(size_t off) {
    Lexer L(b_, n_, off);
    L.skip(); L.word(); L.skip(); L.word(); L.skip();
    if (L.word() != "obj") throw Erro("objeto inválido");
    P val = L.value();
    L.skip();
    if (L.p + 6 <= n_ && latin1(b_ + L.p, 6) == "stream") {
      size_t p = L.p + 6;
      if (p < n_ && b_[p] == 0x0d) p++;
      if (p < n_ && b_[p] == 0x0a) p++;
      P len = dget(val, "Length");
      if (len && len->t == PV::Ref) len = get(len);
      long n = len && len->t == PV::Num ? (long)len->num : -1;
      bool ok = false;
      if (n >= 0 && p + n <= n_) {
        size_t q = p + n;
        while (q < n_ && WS(b_[q])) q++;
        ok = q + 9 <= n_ && memcmp(b_ + q, "endstream", 9) == 0;
      }
      if (!ok) {
        n = -1;
        size_t lim = std::min(n_, p + (size_t)50000000);
        for (size_t q = p; q + 9 <= lim; q++)
          if (memcmp(b_ + q, "endstream", 9) == 0) { n = (long)(q - p); break; }
        while (n > 0 && (b_[p + n - 1] == 0x0a || b_[p + n - 1] == 0x0d)) n--;
        if (n < 0) n = 0;
      }
      auto st = mk(PV::Stream);
      st->sdict = val;
      st->data = b_ + p;
      st->len = (size_t)n;
      return st;
    }
    return val;
  }

  P fromObjStm(long sn, int n, int idx) {
    (void)idx;
    auto it = stms_.find(sn);
    std::shared_ptr<Stm> s;
    if (it == stms_.end()) {
      P st = get(mkRef((int)sn));
      if (!st || st->t != PV::Stream) return mk(PV::Null);
      s = std::make_shared<Stm>();
      s->data = decodeStream(st);
      P N = dget(st->sdict, "N"), F = dget(st->sdict, "First");
      if (!N || !F) return mk(PV::Null);
      s->first = (size_t)F->num;
      Lexer L(s->data.data(), s->data.size(), 0);
      for (int i = 0; i < (int)N->num; i++) {
        P on = L.value(), oo = L.value();
        if (!on || !oo) break;
        s->offs[(int)on->num] = (size_t)oo->num;
      }
      stms_[sn] = s;
    } else s = it->second;
    auto o = s->offs.find(n);
    if (o == s->offs.end()) return mk(PV::Null);
    Lexer L(s->data.data(), s->data.size(), s->first + o->second);
    P v = L.value();
    return v ? v : mk(PV::Null);
  }
};

struct PagInfo { P node; std::map<std::string, P> herd; };

std::vector<PagInfo> paginas(Doc& doc) {
  P root = doc.get(dget(doc.trailer, "Root"));
  std::vector<PagInfo> out;
  const char* herdaveis[] = {"Resources", "MediaBox", "CropBox", "Rotate"};
  std::set<int> vistos;
  std::function<void(const P&, std::map<std::string, P>, int)> walk = [&](const P& ref, std::map<std::string, P> herd, int depth) {
    if (depth > 50) return;
    if (ref && ref->t == PV::Ref) { if (vistos.count(ref->n)) return; vistos.insert(ref->n); }
    P node = doc.get(ref);
    if (!node || node->t != PV::Dict) return;
    P tp = node->get("Type");
    for (auto k : herdaveis) if (node->has(k)) herd[k] = node->get(k);
    P kidsV = doc.get(node->get("Kids"));
    if (isName(tp, "Pages") || (kidsV && kidsV->t == PV::Arr && !isName(tp, "Page"))) {
      if (kidsV && kidsV->t == PV::Arr) for (auto& k : kidsV->arr) walk(k, herd, depth + 1);
    } else out.push_back(PagInfo{node, herd});
  };
  walk(dget(root, "Pages"), {}, 0);
  return out;
}

/* ---- escrita ---- */
std::string numStr(double v) {
  char b[64];
  if (v == std::floor(v) && std::fabs(v) < 1e15) { snprintf(b, sizeof b, "%.0f", v); return b; }
  snprintf(b, sizeof b, "%.6f", v);
  std::string s = b;
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  if (s == "-0") s = "0";
  return s;
}
void ser(const P& v, std::string& o) {
  if (!v || v->t == PV::Null) { o += "null"; return; }
  switch (v->t) {
    case PV::Num: o += numStr(v->num); break;
    case PV::Bool: o += v->b ? "true" : "false"; break;
    case PV::Name: o += "/" + v->s; break;
    case PV::Ref: o += std::to_string(v->n) + " 0 R"; break;
    case PV::Kw: o += v->s; break;
    case PV::Str: {
      static const char* hx = "0123456789abcdef";
      o += '<';
      for (unsigned char c : v->s) { o += hx[c >> 4]; o += hx[c & 15]; }
      o += '>';
      break;
    }
    case PV::Arr:
      o += '[';
      for (size_t i = 0; i < v->arr.size(); i++) { if (i) o += ' '; ser(v->arr[i], o); }
      o += ']';
      break;
    case PV::Dict:
      o += "<<";
      for (auto& e : v->dict) { o += "/" + e.first + " "; ser(e.second, o); o += " "; }
      o += ">>";
      break;
    default: o += "null";
  }
}

Bytes escrever(std::vector<P>& out, int catN) {
  Bytes r;
  auto put = [&](const std::string& s) { r.insert(r.end(), s.begin(), s.end()); };
  put("%PDF-1.7\n%\xE2\xE3\xCF\xD3\n");
  std::vector<size_t> xr(out.size(), 0);
  for (size_t n = 1; n < out.size(); n++) {
    P v = out[n] ? out[n] : mk(PV::Null);
    xr[n] = r.size();
    if (v->t == PV::Stream) {
      auto d = std::make_shared<PV>(*v->sdict);
      d->set("Length", mkNum((double)v->len));
      std::string p;
      ser(d, p);
      put(std::to_string(n) + " 0 obj\n" + p + "\nstream\n");
      r.insert(r.end(), v->data, v->data + v->len);
      put("\nendstream\nendobj\n");
    } else {
      std::string p;
      ser(v, p);
      put(std::to_string(n) + " 0 obj\n" + p + "\nendobj\n");
    }
  }
  size_t start = r.size();
  std::string x = "xref\n0 " + std::to_string(out.size()) + "\n0000000000 65535 f \n";
  for (size_t n = 1; n < out.size(); n++) { char b[32]; snprintf(b, sizeof b, "%010zu 00000 n \n", xr[n]); x += b; }
  x += "trailer\n<< /Size " + std::to_string(out.size()) + " /Root " + std::to_string(catN) + " 0 R >>\nstartxref\n" + std::to_string(start) + "\n%%EOF\n";
  put(x);
  return r;
}

}  // namespace

Bytes juntarPdfs(const std::vector<Bytes>& buffers, int* paginasOut, Progresso* prog) {
  std::vector<P> out;
  out.push_back(nullptr);  // índice 0 não existe
  const int pagesNum = 1;
  out.push_back(nullptr);  // obj 1 = Pages
  std::vector<int> kidsN;
  for (size_t di = 0; di < buffers.size(); di++) {
    Doc doc(buffers[di].data(), buffers[di].size());
    try {
      doc.load();
    } catch (const Erro& e) {
      throw Erro("arquivo " + std::to_string(di + 1) + ": " + e.what());
    }
    std::map<int, int> map;
    std::deque<std::pair<P, int>> fila;
    auto novo = [&](const P& ref) {
      auto it = map.find(ref->n);
      if (it != map.end()) return it->second;
      int nn = (int)out.size();
      out.push_back(nullptr);
      map[ref->n] = nn;
      fila.emplace_back(ref, nn);
      return nn;
    };
    std::function<P(const P&)> remap = [&](const P& v) -> P {
      if (!v) return v;
      if (v->t == PV::Ref) return mkRef(novo(v));
      if (v->t == PV::Arr) {
        auto a = mk(PV::Arr);
        for (auto& x : v->arr) a->arr.push_back(remap(x));
        return a;
      }
      if (v->t == PV::Dict) {
        auto m = mk(PV::Dict);
        P tp = v->get("Type");
        for (auto& e : v->dict) {
          if (e.first == "Parent") {
            if (tp && !(tp->t == PV::Name && tp->s == "Page")) m->dict.emplace_back(e.first, remap(e.second));
            continue;
          }
          m->dict.emplace_back(e.first, remap(e.second));
        }
        return m;
      }
      if (v->t == PV::Stream) {
        auto s = mk(PV::Stream);
        s->sdict = remap(v->sdict);
        s->data = v->data;
        s->len = v->len;
        return s;
      }
      return v;
    };
    auto pgs = paginas(doc);
    for (auto& pg : pgs) {
      auto m = mk(PV::Dict);
      for (auto& e : pg.node->dict)
        if (e.first != "Parent" && e.first != "B" && e.first != "StructParents") m->dict.push_back(e);
      for (auto& h : pg.herd) if (!m->has(h.first)) m->set(h.first, h.second);
      m->set("Type", mkName("Page"));
      if (!m->has("MediaBox")) {
        auto a = mk(PV::Arr);
        for (double x : {0.0, 0.0, 595.28, 841.89}) a->arr.push_back(mkNum(x));
        m->set("MediaBox", a);
      }
      P pd = remap(m);
      pd->set("Parent", mkRef(pagesNum));
      int pn = (int)out.size();
      out.push_back(pd);
      kidsN.push_back(pn);
    }
    while (!fila.empty()) {
      auto [ref, nn] = fila.front();
      fila.pop_front();
      P v = doc.get(ref);
      if (v && v->t == PV::Dict && isName(v->get("Type"), "Page")) { out[nn] = mk(PV::Null); continue; }
      if (v && v->t == PV::Dict && v->has("P") && isName(v->get("Type"), "Annot")) {
        auto c = std::make_shared<PV>(*v);
        c->del("P");
        v = c;
      }
      out[nn] = remap(v ? v : mk(PV::Null));
    }
    if (prog) prog->passo((int)di + 1, (int)buffers.size());
  }
  auto pages = mk(PV::Dict);
  pages->set("Type", mkName("Pages"));
  auto kids = mk(PV::Arr);
  for (int n : kidsN) kids->arr.push_back(mkRef(n));
  pages->set("Kids", kids);
  pages->set("Count", mkNum((double)kidsN.size()));
  out[pagesNum] = pages;
  int catN = (int)out.size();
  auto cat = mk(PV::Dict);
  cat->set("Type", mkName("Catalog"));
  cat->set("Pages", mkRef(pagesNum));
  out.push_back(cat);
  if (kidsN.empty()) throw Erro("nenhuma página encontrada nos PDFs");
  if (paginasOut) *paginasOut = (int)kidsN.size();
  return escrever(out, catN);
}

}  // namespace xp
