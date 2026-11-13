// ZIP (leitura), XML (DOM mínimo) e zlib.
#pragma once
#include <zlib.h>

#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "engine.h"

namespace xp {

/* ================= zlib ================= */
// windowBits: -15 = deflate cru (ZIP), 15 = zlib (PDF FlateDecode)
inline Bytes inflar(const uint8_t* d, size_t n, int windowBits, bool tolerante = false) {
  z_stream s{};
  if (inflateInit2(&s, windowBits) != Z_OK) throw Erro("falha ao descompactar");
  Bytes out;
  out.resize(n * 4 + 1024);
  s.next_in = const_cast<Bytes::value_type*>(d);
  s.avail_in = (uInt)n;
  int r;
  for (;;) {
    if (s.total_out >= out.size()) out.resize(out.size() * 2);
    s.next_out = out.data() + s.total_out;
    s.avail_out = (uInt)(out.size() - s.total_out);
    r = inflate(&s, Z_NO_FLUSH);
    if (r == Z_STREAM_END) break;
    if (r == Z_OK) continue;
    if (r == Z_BUF_ERROR && s.avail_out == 0) continue;
    break;
  }
  size_t tot = s.total_out;
  inflateEnd(&s);
  if (r != Z_STREAM_END && !(tolerante && tot > 0)) throw Erro("dados compactados corrompidos");
  out.resize(tot);
  return out;
}

inline Bytes deflar(const std::string& in) {
  uLongf n = compressBound((uLong)in.size());
  Bytes out(n);
  if (compress2(out.data(), &n, (const Bytef*)in.data(), (uLong)in.size(), 6) != Z_OK)
    throw Erro("falha ao compactar");
  out.resize(n);
  return out;
}

/* ================= ZIP ================= */
class Zip {
 public:
  Zip(const uint8_t* d, size_t n) : b_(d), n_(n) {
    long eocd = -1;
    if (n >= 22)
      for (long i = (long)n - 22; i >= 0 && i >= (long)n - 66000; i--)
        if (u32(i) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) throw Erro("não é um arquivo do Excel válido");
    unsigned cnt = u16(eocd + 10);
    size_t p = u32(eocd + 16);
    for (unsigned k = 0; k < cnt; k++) {
      if (p + 46 > n_ || u32(p) != 0x02014b50) break;
      Ent e;
      e.method = u16(p + 10);
      e.csize = u32(p + 20);
      unsigned nl = u16(p + 28), xl = u16(p + 30), cl = u16(p + 32);
      e.off = u32(p + 42);
      if (p + 46 + nl > n_) break;
      std::string nome((const char*)b_ + p + 46, nl);
      ents_[nome] = e;
      p += 46 + nl + xl + cl;
    }
  }
  bool has(const std::string& nome) const { return ents_.count(nome) > 0; }
  // devolve false se não existir
  bool texto(const std::string& nome, std::string& out) const {
    auto it = ents_.find(nome);
    if (it == ents_.end()) return false;
    const Ent& e = it->second;
    size_t lh = e.off;
    if (lh + 30 > n_) throw Erro("arquivo do Excel corrompido");
    size_t st = lh + 30 + u16(lh + 26) + u16(lh + 28);
    if (st + e.csize > n_) throw Erro("arquivo do Excel corrompido");
    if (e.method == 0) {
      out.assign((const char*)b_ + st, e.csize);
    } else {
      Bytes r = inflar(b_ + st, e.csize, -15);
      out.assign(r.begin(), r.end());
    }
    return true;
  }

 private:
  struct Ent { unsigned method = 0; size_t csize = 0, off = 0; };
  const uint8_t* b_;
  size_t n_;
  std::map<std::string, Ent> ents_;
  unsigned u16(size_t i) const { return i + 2 <= n_ ? (b_[i] | (b_[i + 1] << 8)) : 0; }
  uint32_t u32(size_t i) const {
    return i + 4 <= n_ ? (uint32_t)b_[i] | ((uint32_t)b_[i + 1] << 8) | ((uint32_t)b_[i + 2] << 16) |
                             ((uint32_t)b_[i + 3] << 24)
                       : 0;
  }
};

/* ================= XML ================= */
struct XNode {
  std::string qname, local;
  std::vector<std::pair<std::string, std::string>> attrs;
  std::vector<std::unique_ptr<XNode>> kids;
  std::string text;  // texto direto (concatenado)
  XNode* parent = nullptr;

  const std::string* attr(const std::string& q) const {
    for (auto& a : attrs) if (a.first == q) return &a.second;
    return nullptr;
  }
  bool has(const std::string& q) const { return attr(q) != nullptr; }
  std::string get(const std::string& q, const std::string& def = "") const {
    auto a = attr(q);
    return a ? *a : def;
  }
  std::string textContent() const {
    std::string s = text;  // aproximação: texto direto + filhos (suficiente para <t>, <v>, definedName)
    for (auto& k : kids) s += k->textContent();
    return s;
  }
};

class Xml {
 public:
  explicit Xml(const std::string& s) : s_(s) {
    root_.reset(new XNode());
    root_->qname = root_->local = "#doc";
    parse();
  }
  XNode* root() { return root_.get(); }

 private:
  const std::string& s_;
  size_t p_ = 0;
  std::unique_ptr<XNode> root_;

  static std::string localOf(const std::string& q) {
    auto i = q.find(':');
    return i == std::string::npos ? q : q.substr(i + 1);
  }
  static void putUtf8(std::string& o, unsigned cp) {
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
    else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 63)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
  }
  static std::string decode(const std::string& s, size_t a, size_t b) {
    std::string o;
    o.reserve(b - a);
    for (size_t i = a; i < b; i++) {
      if (s[i] != '&') { o += s[i]; continue; }
      size_t e = s.find(';', i);
      if (e == std::string::npos || e > b || e - i > 12) { o += s[i]; continue; }
      std::string ent = s.substr(i + 1, e - i - 1);
      if (ent == "amp") o += '&';
      else if (ent == "lt") o += '<';
      else if (ent == "gt") o += '>';
      else if (ent == "quot") o += '"';
      else if (ent == "apos") o += '\'';
      else if (!ent.empty() && ent[0] == '#') {
        unsigned cp = (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X'))
                          ? (unsigned)strtoul(ent.c_str() + 2, nullptr, 16)
                          : (unsigned)strtoul(ent.c_str() + 1, nullptr, 10);
        putUtf8(o, cp);
      } else { o += s.substr(i, e - i + 1); }
      i = e;
    }
    return o;
  }
  static bool ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

  void parse() {
    XNode* cur = root_.get();
    const size_t n = s_.size();
    while (p_ < n) {
      size_t lt = s_.find('<', p_);
      if (lt == std::string::npos) lt = n;
      if (lt > p_ && cur != root_.get()) cur->text += decode(s_, p_, lt);
      if (lt >= n) break;
      p_ = lt;
      if (s_.compare(p_, 4, "<!--") == 0) {
        size_t e = s_.find("-->", p_ + 4);
        p_ = e == std::string::npos ? n : e + 3;
      } else if (s_.compare(p_, 9, "<![CDATA[") == 0) {
        size_t e = s_.find("]]>", p_ + 9);
        if (e == std::string::npos) e = n;
        cur->text += s_.substr(p_ + 9, e - p_ - 9);
        p_ = e + 3;
      } else if (s_.compare(p_, 2, "<?") == 0) {
        size_t e = s_.find("?>", p_ + 2);
        p_ = e == std::string::npos ? n : e + 2;
      } else if (s_.compare(p_, 2, "<!") == 0) {
        size_t e = s_.find('>', p_ + 2);
        p_ = e == std::string::npos ? n : e + 1;
      } else if (s_.compare(p_, 2, "</") == 0) {
        size_t e = s_.find('>', p_ + 2);
        p_ = e == std::string::npos ? n : e + 1;
        if (cur->parent) cur = cur->parent;
      } else {
        p_++;
        size_t a = p_;
        while (p_ < n && !ws(s_[p_]) && s_[p_] != '>' && s_[p_] != '/') p_++;
        auto nd = std::unique_ptr<XNode>(new XNode());
        nd->qname = s_.substr(a, p_ - a);
        nd->local = localOf(nd->qname);
        nd->parent = cur;
        bool selfClose = false;
        for (;;) {
          while (p_ < n && ws(s_[p_])) p_++;
          if (p_ >= n) break;
          if (s_[p_] == '>') { p_++; break; }
          if (s_[p_] == '/') { selfClose = true; p_++; continue; }
          size_t k = p_;
          while (p_ < n && s_[p_] != '=' && !ws(s_[p_]) && s_[p_] != '>' && s_[p_] != '/') p_++;
          std::string key = s_.substr(k, p_ - k);
          while (p_ < n && ws(s_[p_])) p_++;
          std::string val;
          if (p_ < n && s_[p_] == '=') {
            p_++;
            while (p_ < n && ws(s_[p_])) p_++;
            if (p_ < n && (s_[p_] == '"' || s_[p_] == '\'')) {
              char q = s_[p_++];
              size_t e = s_.find(q, p_);
              if (e == std::string::npos) e = n;
              val = decode(s_, p_, e);
              p_ = e + 1;
            }
          }
          if (!key.empty()) nd->attrs.emplace_back(key, val);
        }
        XNode* raw = nd.get();
        cur->kids.push_back(std::move(nd));
        if (!selfClose) cur = raw;
      }
    }
  }
};

// Busca de nós, como no JavaScript original
inline void allRec(const XNode* n, const std::string& tag, std::vector<const XNode*>& out) {
  for (auto& k : n->kids) {
    if (k->local == tag) out.push_back(k.get());
    allRec(k.get(), tag, out);
  }
}
inline std::vector<const XNode*> all(const XNode* n, const std::string& tag) {
  std::vector<const XNode*> out;
  if (n) allRec(n, tag, out);
  return out;
}
inline const XNode* oneRec(const XNode* n, const std::string& tag) {
  for (auto& k : n->kids) {
    if (k->local == tag) return k.get();
    if (auto r = oneRec(k.get(), tag)) return r;
  }
  return nullptr;
}
inline const XNode* one(const XNode* n, const std::string& tag) { return n ? oneRec(n, tag) : nullptr; }
inline std::vector<const XNode*> kids(const XNode* n, const std::string& tag) {
  std::vector<const XNode*> out;
  if (n) for (auto& k : n->kids) if (k->local == tag) out.push_back(k.get());
  return out;
}

}  // namespace xp
