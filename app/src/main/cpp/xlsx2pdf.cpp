// Planilha (.xlsm/.xlsx) -> PDF. Tradução para C++ do app em HTML.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <unordered_map>

#include "engine.h"
#include "util.hpp"

namespace xp {

/* ================= larguras Helvetica (=Liberation Sans) para bytes 32..255, por mil ================= */
static const int WR[224] = {278,278,355,556,556,889,667,191,333,333,389,584,278,333,278,278,556,556,556,556,556,556,556,556,556,556,278,278,584,584,584,556,1015,667,667,722,722,667,611,778,722,278,500,667,556,833,722,778,667,778,722,667,611,722,667,944,667,667,611,278,278,278,469,556,333,556,556,500,556,556,278,556,556,222,222,500,222,833,556,556,556,556,333,500,278,556,500,722,500,500,500,334,260,334,584,556,556,556,222,556,333,1000,556,556,333,1000,667,333,1000,556,611,556,556,222,222,333,333,350,556,1000,333,1000,500,333,944,556,500,667,278,333,556,556,556,556,260,556,333,737,370,556,584,333,737,552,400,549,333,333,333,576,537,333,333,333,365,556,834,834,834,611,667,667,667,667,667,667,1000,722,667,667,667,667,278,278,278,278,722,722,778,778,778,778,778,584,778,722,722,722,722,667,667,611,556,556,556,556,556,556,889,500,556,556,556,556,278,278,278,278,556,556,556,556,556,556,556,549,611,556,556,556,556,500,556,500};
static const int WB[224] = {278,333,474,556,556,889,722,238,333,333,389,584,278,333,278,278,556,556,556,556,556,556,556,556,556,556,333,333,584,584,584,611,975,722,722,722,722,667,611,778,722,278,556,722,611,833,722,778,667,778,722,667,611,722,667,944,667,667,611,333,278,333,584,556,333,556,611,556,611,556,333,611,611,278,278,556,278,889,611,611,611,611,389,556,333,611,556,778,556,556,500,389,280,389,584,556,556,556,278,556,500,1000,556,556,333,1000,667,333,1000,556,611,556,556,278,278,500,500,350,556,1000,333,1000,556,333,944,556,500,667,278,333,556,556,556,556,280,556,333,737,370,556,584,333,737,552,400,549,333,333,333,576,556,333,333,333,365,556,834,834,834,611,722,722,722,722,722,722,1000,722,667,667,667,667,278,278,278,278,722,722,778,778,778,778,778,584,778,722,722,722,722,667,667,611,556,556,556,556,556,556,889,556,556,556,556,556,278,278,278,278,611,611,611,611,611,611,611,549,611,611,611,611,611,556,611,556};

/* ================= tipos ================= */
struct RGB { int r = 0, g = 0, b = 0; };
using Cor = std::optional<RGB>;

struct Fonte { bool b = false, i = false, u = false; double sz = 11; RGB color; };
struct Borda { double w = .5; RGB c; };
struct Bordas { std::optional<Borda> left, right, top, bottom; bool vazia() const { return !left && !right && !top && !bottom; } };
struct Xf {
  std::string fmt = "0";
  const Fonte* font = nullptr;
  Cor fill;
  Bordas border;
  std::string h, v = "bottom";
  bool wrap = false;
  int indent = 0;
};
struct Valor {
  enum K { Nulo, Num, Txt } k = Nulo;
  double n = 0;
  std::string s;
  bool vazio() const { return k == Nulo || (k == Txt && s.empty()); }
};
struct Celula { std::string t = "n"; Valor v; int s = 0; };
struct ColInfo { double w; bool hidden; std::string s; };
struct RowInfo { double h; bool hidden; };
struct Merge { int r1, c1, r2, c2; };

struct Ctx {
  std::vector<std::string> sst;
  std::vector<Fonte> fonts;
  std::vector<Xf> xfs;
  std::map<std::string, std::string> numFmts;
  bool date1904 = false;
  std::string printArea;
  bool temPrintArea = false;
};

static inline long long chave(long long r, long long c) { return r * 20000 + c; }

struct Aba {
  std::string name;
  std::unordered_map<long long, Celula> cells;
  std::map<int, RowInfo> rows;
  std::map<int, ColInfo> cols;
  std::vector<Merge> merges;
  double defRowH = 15, defColW = 8.71;
  std::shared_ptr<Ctx> ctx;
  double ml = 50, mr = 50, mt = 54, mb = 54;
  bool landscape = false;
};

/* ================= cores ================= */
static const char* INDEXED[] = {"000000","FFFFFF","FF0000","00FF00","0000FF","FFFF00","FF00FF","00FFFF","000000","FFFFFF","FF0000","00FF00","0000FF","FFFF00","FF00FF","00FFFF","800000","008000","000080","808000","800080","008080","C0C0C0","808080","9999FF","993366","FFFFCC","CCFFFF","660066","FF8080","0066CC","CCCCFF","000080","FF00FF","FFFF00","00FFFF","800080","800000","008080","0000FF","00CCFF","CCFFFF","CCFFCC","FFFF99","99CCFF","FF99CC","CC99FF","FFCC99","3366FF","33CCCC","99CC00","FFCC00","FF9900","FF6600","666699","969696","003366","339966","003300","333300","993300","993366","333399","333333"};

static RGB hex2rgb(std::string h) {
  if (!h.empty() && h[0] == '#') h = h.substr(1);
  if (h.size() == 8) h = h.substr(2);
  unsigned long n = strtoul(h.c_str(), nullptr, 16);
  return RGB{(int)((n >> 16) & 255), (int)((n >> 8) & 255), (int)(n & 255)};
}
static RGB tintRGB(RGB c, double t) {
  if (t == 0) return c;
  double r = c.r / 255.0, g = c.g / 255.0, b = c.b / 255.0;
  double mx = std::max({r, g, b}), mn = std::min({r, g, b});
  double h = 0, s = 0, l = (mx + mn) / 2;
  if (mx != mn) {
    double d = mx - mn;
    s = l > .5 ? d / (2 - mx - mn) : d / (mx + mn);
    h = mx == r ? (g - b) / d + (g < b ? 6 : 0) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4;
    h /= 6;
  }
  l = t < 0 ? l * (1 + t) : l * (1 - t) + t;
  auto f = [](double p, double q, double x) {
    if (x < 0) x += 1;
    if (x > 1) x -= 1;
    if (x < 1.0 / 6) return p + (q - p) * 6 * x;
    if (x < 1.0 / 2) return q;
    if (x < 2.0 / 3) return p + (q - p) * (2.0 / 3 - x) * 6;
    return p;
  };
  double R, G, B;
  if (s == 0) R = G = B = l;
  else {
    double q = l < .5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
    R = f(p, q, h + 1.0 / 3); G = f(p, q, h); B = f(p, q, h - 1.0 / 3);
  }
  auto rd = [](double v) { return (int)std::floor(v * 255 + 0.5); };
  return RGB{rd(R), rd(G), rd(B)};
}

struct Paleta { std::vector<std::string> idx; std::vector<std::string> theme; };

static Cor colorOf(const XNode* el, const Paleta& P, Cor def) {
  if (!el) return def;
  if (el->get("auto") == "1") return def;
  Cor c;
  double t = el->has("tint") ? atof(el->get("tint").c_str()) : 0;
  if (el->has("rgb")) c = hex2rgb(el->get("rgb"));
  else if (el->has("theme")) {
    int i = atoi(el->get("theme").c_str());
    static const int map[] = {1, 0, 3, 2, 4, 5, 6, 7, 8, 9, 10, 11};
    int j = (i >= 0 && i < 12) ? map[i] : i;
    if (j >= 0 && j < (int)P.theme.size() && !P.theme[j].empty()) c = hex2rgb(P.theme[j]);
  } else if (el->has("indexed")) {
    int i = atoi(el->get("indexed").c_str());
    if (i == 64 || i == 65) return def;
    if (i >= 0 && i < (int)P.idx.size()) c = hex2rgb(P.idx[i]);
  }
  return c ? Cor(tintRGB(*c, t)) : def;
}

static const std::map<std::string, std::string> BUILTIN = {
    {"0", "General"}, {"1", "0"}, {"2", "0.00"}, {"3", "#,##0"}, {"4", "#,##0.00"}, {"9", "0%"}, {"10", "0.00%"},
    {"11", "0.00E+00"}, {"12", "# ?/?"}, {"13", "# ?\?/??"}, {"14", "dd/mm/yyyy"}, {"15", "d-mmm-yy"}, {"16", "d-mmm"},
    {"17", "mmm-yy"}, {"18", "h:mm AM/PM"}, {"19", "h:mm:ss AM/PM"}, {"20", "h:mm"}, {"21", "h:mm:ss"},
    {"22", "dd/mm/yyyy h:mm"}, {"37", "#,##0 ;(#,##0)"}, {"38", "#,##0 ;[Red](#,##0)"},
    {"39", "#,##0.00;(#,##0.00)"}, {"40", "#,##0.00;[Red](#,##0.00)"}, {"44", "_(\"R$\"* #,##0.00_)"},
    {"45", "mm:ss"}, {"46", "[h]:mm:ss"}, {"47", "mmss.0"}, {"48", "##0.0E+0"}, {"49", "@"}};

// "AB12" -> (coluna 28, linha 12)
static std::pair<int, int> colIdx(const std::string& ref) {
  int n = 0;
  size_t i = 0;
  while (i < ref.size() && isalpha((unsigned char)ref[i])) { n = n * 26 + (toupper(ref[i]) - 64); i++; }
  return {n, atoi(ref.c_str() + i)};
}
static bool verdade(const std::string& s) { return s == "1" || s == "true"; }

/* ================= leitura da planilha ================= */
static Aba lerAba(const std::string& text, const std::string& name, std::shared_ptr<Ctx> ctx);

static std::vector<Aba> lerPasta(const uint8_t* d, size_t n) {
  Zip z(d, n);
  std::string t;
  if (!z.texto("xl/workbook.xml", t)) t = "";
  Xml wbx(t);
  if (!one(wbx.root(), "workbook")) throw Erro("planilha sem workbook");
  auto wbPr = one(wbx.root(), "workbookPr");
  std::string d1904 = wbPr ? wbPr->get("date1904") : "";
  bool date1904 = verdade(d1904);

  std::map<std::string, std::string> rels;
  std::string rt;
  if (!z.texto("xl/_rels/workbook.xml.rels", rt)) rt = "<x/>";
  Xml relx(rt);
  for (auto r : all(relx.root(), "Relationship")) {
    std::string tg = r->get("Target");
    if (!tg.empty() && tg[0] == '/') tg = tg.substr(1);
    else {
      tg = "xl/" + tg;
      auto k = tg.find("xl/../");
      if (k != std::string::npos) tg.erase(k, 6);
    }
    rels[r->get("Id")] = tg;
  }
  // tema
  Paleta P;
  std::string thx;
  if (z.texto("xl/theme/theme1.xml", thx)) {
    Xml th(thx);
    if (auto cs = one(th.root(), "clrScheme"))
      for (auto& ch : cs->kids) {
        auto s = one(ch.get(), "srgbClr");
        auto y = one(ch.get(), "sysClr");
        P.theme.push_back(s ? s->get("val") : y ? y->get("lastClr", "000000") : "000000");
      }
  }
  auto ctx = std::make_shared<Ctx>();
  ctx->date1904 = date1904;
  // textos compartilhados
  std::string ssx;
  if (z.texto("xl/sharedStrings.xml", ssx)) {
    Xml sx(ssx);
    for (auto si : kids(one(sx.root(), "sst"), "si")) {
      std::string s;
      for (auto tn : all(si, "t"))
        if (!(tn->parent && tn->parent->local == "rPh")) s += tn->textContent();
      ctx->sst.push_back(s);
    }
  }
  // estilos
  std::string stt;
  if (!z.texto("xl/styles.xml", stt)) stt = "<x/>";
  Xml stx(stt);
  auto ic = one(stx.root(), "indexedColors");
  if (ic) {
    for (auto x : all(ic, "rgbColor")) {
      std::string v = x->get("rgb", "000000");
      P.idx.push_back(v.size() > 6 ? v.substr(v.size() - 6) : v);
    }
  } else {
    for (auto s : INDEXED) P.idx.push_back(s);
  }
  ctx->numFmts = BUILTIN;
  for (auto nf : all(stx.root(), "numFmt")) ctx->numFmts[nf->get("numFmtId")] = nf->get("formatCode");

  for (auto f : kids(one(stx.root(), "fonts"), "font")) {
    Fonte fo;
    auto b = one(f, "b"), i = one(f, "i"), sz = one(f, "sz");
    fo.b = b && b->get("val") != "0" && b->get("val") != "false";
    fo.i = i && i->get("val") != "0" && i->get("val") != "false";
    fo.u = one(f, "u") != nullptr;
    fo.sz = sz && sz->has("val") ? atof(sz->get("val").c_str()) : 11;
    if (!(fo.sz > 0)) fo.sz = 11;
    fo.color = *colorOf(one(f, "color"), P, RGB{0, 0, 0});
    ctx->fonts.push_back(fo);
  }
  if (ctx->fonts.empty()) ctx->fonts.push_back(Fonte());

  std::vector<Cor> fills;
  for (auto f : kids(one(stx.root(), "fills"), "fill")) {
    auto p = one(f, "patternFill");
    if (!p) {
      auto g = one(f, "stop");
      fills.push_back(g ? colorOf(one(g, "color"), P, std::nullopt) : std::nullopt);
      continue;
    }
    std::string pt = p->get("patternType");
    if (pt.empty() || pt == "none") { fills.push_back(std::nullopt); continue; }
    Cor fg = colorOf(one(p, "fgColor"), P, std::nullopt);
    if (pt == "solid") fills.push_back(fg ? fg : Cor(RGB{0, 0, 0}));
    else fills.push_back(fg ? fg : colorOf(one(p, "bgColor"), P, std::nullopt));
  }
  static const std::map<std::string, double> bw = {
      {"hair", .25}, {"thin", .5}, {"dotted", .5}, {"dashed", .5}, {"dashDot", .5}, {"dashDotDot", .5},
      {"medium", 1}, {"mediumDashed", 1}, {"mediumDashDot", 1}, {"mediumDashDotDot", 1}, {"slantDashDot", 1},
      {"thick", 1.6}, {"double", 1.4}};
  std::vector<Bordas> borders;
  for (auto b : kids(one(stx.root(), "borders"), "border")) {
    Bordas o;
    const char* lados[] = {"left", "right", "top", "bottom"};
    for (int k = 0; k < 4; k++) {
      auto ks = kids(b, lados[k]);
      if (!one(b, lados[k]) || ks.empty()) continue;
      auto e = ks[0];
      std::string st = e->get("style");
      if (st.empty() || st == "none") continue;
      Borda bo;
      auto it = bw.find(st);
      bo.w = it != bw.end() ? it->second : .5;
      bo.c = *colorOf(one(e, "color"), P, RGB{0, 0, 0});
      std::optional<Borda>* alvo[] = {&o.left, &o.right, &o.top, &o.bottom};
      *alvo[k] = bo;
    }
    borders.push_back(o);
  }
  for (auto x : kids(one(stx.root(), "cellXfs"), "xf")) {
    Xf xf;
    auto a = one(x, "alignment");
    xf.fmt = x->get("numFmtId", "0");
    size_t fi = (size_t)atoi(x->get("fontId", "0").c_str());
    xf.font = fi < ctx->fonts.size() ? &ctx->fonts[fi] : &ctx->fonts[0];
    size_t fl = (size_t)atoi(x->get("fillId", "0").c_str());
    if (fl < fills.size()) xf.fill = fills[fl];
    size_t bi = (size_t)atoi(x->get("borderId", "0").c_str());
    if (bi < borders.size()) xf.border = borders[bi];
    if (a) {
      xf.h = a->get("horizontal");
      xf.v = a->get("vertical", "bottom");
      xf.wrap = verdade(a->get("wrapText"));
      xf.indent = atoi(a->get("indent", "0").c_str());
    }
    ctx->xfs.push_back(xf);
  }
  if (ctx->xfs.empty()) { Xf xf; xf.font = &ctx->fonts[0]; ctx->xfs.push_back(xf); }

  // nomes definidos (área de impressão)
  std::map<std::string, std::string> printArea;
  for (auto dn : all(wbx.root(), "definedName"))
    if (dn->get("name") == "_xlnm.Print_Area" && dn->has("localSheetId")) printArea[dn->get("localSheetId")] = dn->textContent();

  std::vector<Aba> sheets;
  auto shEls = all(wbx.root(), "sheet");
  for (size_t si = 0; si < shEls.size(); si++) {
    auto s = shEls[si];
    std::string st = s->get("state");
    if (st == "hidden" || st == "veryHidden") continue;
    std::string rid = s->get("r:id");
    if (rid.empty())
      for (auto& a : s->attrs) if (a.first.size() > 3 && a.first.compare(a.first.size() - 3, 3, ":id") == 0) rid = a.second;
    auto it = rels.find(rid);
    if (it == rels.end() || !z.has(it->second)) continue;
    std::string sx;
    z.texto(it->second, sx);
    auto c2 = std::make_shared<Ctx>(*ctx);  // cópia com a área de impressão desta aba
    // as fontes precisam apontar para a cópia
    for (auto& x : c2->xfs) x.font = &c2->fonts[x.font - &ctx->fonts[0]];
    auto pa = printArea.find(std::to_string(si));
    if (pa != printArea.end()) { c2->printArea = pa->second; c2->temPrintArea = true; }
    sheets.push_back(lerAba(sx, s->get("name"), c2));
  }
  return sheets;
}

static Aba lerAba(const std::string& text, const std::string& name, std::shared_ptr<Ctx> ctx) {
  Xml d(text);
  Aba sh;
  sh.name = name;
  sh.ctx = ctx;
  auto fp = one(d.root(), "sheetFormatPr");
  sh.defRowH = fp && fp->has("defaultRowHeight") ? atof(fp->get("defaultRowHeight").c_str()) : 15;
  sh.defColW = fp && fp->has("defaultColWidth") ? atof(fp->get("defaultColWidth").c_str())
                                                : (fp && fp->has("baseColWidth") ? atof(fp->get("baseColWidth").c_str()) : 8) + 0.71;
  for (auto c : all(d.root(), "col")) {
    int mn = atoi(c->get("min").c_str()), mx = atoi(c->get("max").c_str());
    for (int i = mn; i <= std::min(mx, 16384) && i <= mn + 2000; i++)
      sh.cols[i] = ColInfo{c->has("width") ? atof(c->get("width").c_str()) : sh.defColW, verdade(c->get("hidden")), c->get("style")};
  }
  for (auto r : all(d.root(), "row")) {
    int rn = atoi(r->get("r").c_str());
    sh.rows[rn] = RowInfo{r->has("ht") ? atof(r->get("ht").c_str()) : sh.defRowH, verdade(r->get("hidden"))};
    for (auto c : kids(r, "c")) {
      auto [ci, ri] = colIdx(c->get("r"));
      Celula cl;
      cl.t = c->get("t", "n");
      cl.s = atoi(c->get("s", "0").c_str());
      auto vs = kids(c, "v");
      const XNode* vn = vs.empty() ? nullptr : vs[0];
      const std::string& t = cl.t;
      if (t == "s") {
        if (vn) {
          size_t k = (size_t)atol(vn->textContent().c_str());
          cl.v.k = Valor::Txt;
          cl.v.s = k < ctx->sst.size() ? ctx->sst[k] : "";
        }
      } else if (t == "inlineStr") {
        auto is = kids(c, "is");
        cl.v.k = Valor::Txt;
        if (!is.empty()) for (auto x : all(is[0], "t")) cl.v.s += x->textContent();
      } else if (t == "str" || t == "e") {
        if (vn) { cl.v.k = Valor::Txt; cl.v.s = vn->textContent(); }
      } else if (t == "b") {
        if (vn) { cl.v.k = Valor::Txt; cl.v.s = vn->textContent() == "1" ? "VERDADEIRO" : "FALSO"; }
      } else {
        if (vn && !vn->textContent().empty()) {
          cl.v.k = Valor::Num;
          cl.v.n = strtod(vn->textContent().c_str(), nullptr);
        }
      }
      sh.cells[chave(ri, ci)] = cl;
    }
  }
  for (auto m : all(d.root(), "mergeCell")) {
    std::string ref = m->get("ref");
    auto k = ref.find(':');
    std::string a = ref.substr(0, k), b = k == std::string::npos ? a : ref.substr(k + 1);
    auto [c1, r1] = colIdx(a);
    auto [c2, r2] = colIdx(b);
    sh.merges.push_back(Merge{r1, c1, r2, c2});
  }
  auto pm = one(d.root(), "pageMargins");
  auto ps = one(d.root(), "pageSetup");
  if (pm) {
    sh.ml = atof(pm->get("left").c_str()) * 72;
    sh.mr = atof(pm->get("right").c_str()) * 72;
    sh.mt = atof(pm->get("top").c_str()) * 72;
    sh.mb = atof(pm->get("bottom").c_str()) * 72;
  }
  sh.landscape = ps && ps->get("orientation") == "landscape";
  return sh;
}

/* ================= números no estilo do JavaScript ================= */
// dígitos significativos e expoente de v com 'prec' dígitos (sem zeros à direita)
static std::string jsNumero(double v, int prec) {
  if (v == 0) return "0";
  if (std::isnan(v)) return "NaN";
  if (std::isinf(v)) return v < 0 ? "-Infinity" : "Infinity";
  char buf[64];
  int p = prec;
  if (prec <= 0) {  // menor representação que volta ao mesmo número
    for (p = 1; p <= 17; p++) {
      snprintf(buf, sizeof buf, "%.*e", p - 1, v);
      if (strtod(buf, nullptr) == v) break;
    }
    if (p > 17) p = 17;
  }
  snprintf(buf, sizeof buf, "%.*e", p - 1, v);
  std::string s = buf;
  bool neg = s[0] == '-';
  if (neg) s = s.substr(1);
  auto e = s.find('e');
  int ex = atoi(s.c_str() + e + 1);
  std::string dig;
  for (size_t i = 0; i < e; i++) if (isdigit((unsigned char)s[i])) dig += s[i];
  while (dig.size() > 1 && dig.back() == '0') dig.pop_back();
  std::string out;
  int k = (int)dig.size(), n = ex + 1;
  if (k <= n && n <= 21) out = dig + std::string(n - k, '0');
  else if (0 < n && n <= 21) out = dig.substr(0, n) + "." + dig.substr(n);
  else if (-6 < n && n <= 0) out = "0." + std::string(-n, '0') + dig;
  else {
    out = dig.substr(0, 1);
    if (k > 1) out += "." + dig.substr(1);
    out += std::string("e") + (n - 1 >= 0 ? "+" : "-") + std::to_string(std::abs(n - 1));
  }
  return (neg ? "-" : "") + out;
}
// Number.prototype.toFixed (empate arredonda para cima, como no JS)
static std::string toFixed(double a, int dec) {
  char buf[400];
  double esc = a * std::pow(10.0, dec);
  if (std::fabs(esc) < 9007199254740992.0 && esc - std::floor(esc) == 0.5) {
    snprintf(buf, sizeof buf, "%.0f", std::floor(esc) + 1);
    std::string s = buf;
    if (dec > 0) {
      while ((int)s.size() <= dec) s = "0" + s;
      s.insert(s.size() - dec, ".");
    }
    return s;
  }
  snprintf(buf, sizeof buf, "%.*f", dec, a);
  return buf;
}
static std::string trocar(std::string s, char a, char b) { std::replace(s.begin(), s.end(), a, b); return s; }
static std::string nf(double n, int dec, bool sep) {
  std::string s = toFixed(std::fabs(n), dec);
  auto pt = s.find('.');
  std::string i = s.substr(0, pt), f = pt == std::string::npos ? "" : s.substr(pt + 1);
  if (sep) {
    std::string o;
    int c = 0;
    for (int k = (int)i.size() - 1; k >= 0; k--) {
      o = i[k] + o;
      if (++c % 3 == 0 && k > 0) o = "." + o;
    }
    i = o;
  }
  return i + (f.empty() ? "" : "," + f);
}
static std::string p2(int n) { char b[16]; snprintf(b, sizeof b, "%02d", n); return b; }

static std::string rx(const std::string& s, const char* pat, const char* rep) {
  return std::regex_replace(s, std::regex(pat), rep);
}
static bool tem(const std::string& s, const char* pat, bool icase = false) {
  return std::regex_search(s, std::regex(pat, icase ? std::regex::ECMAScript | std::regex::icase : std::regex::ECMAScript));
}
static std::string trimStart(std::string s) { size_t i = 0; while (i < s.size() && isspace((unsigned char)s[i])) i++; return s.substr(i); }
static std::string trimEnd(std::string s) { while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); return s; }
static std::string trim(const std::string& s) { return trimEnd(trimStart(s)); }

// dias desde 1970-01-01 -> ano, mês, dia (algoritmo de Howard Hinnant)
static void civil(long long z, int& y, int& m, int& d) {
  z += 719468;
  long long era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = (unsigned)(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long long yy = (long long)yoe + era * 400;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  d = (int)(doy - (153 * mp + 2) / 5 + 1);
  m = (int)(mp < 10 ? mp + 3 : mp - 9);
  y = (int)(yy + (m <= 2));
}

static std::string formatar(const Celula& cell, const Xf& xf, const Ctx& ctx) {
  const Valor& v = cell.v;
  if (v.k == Valor::Nulo) return "";
  if (v.k == Valor::Txt) return v.s;
  double val = v.n;
  std::string code = "General";
  auto it = ctx.numFmts.find(xf.fmt);
  if (it != ctx.numFmts.end()) code = it->second;
  // seções separadas por ';' fora de aspas
  std::vector<std::string> secs;
  {
    std::string cur;
    bool q = false;
    for (char ch : code) {
      if (ch == '"') q = !q;
      if (ch == ';' && !q) { secs.push_back(cur); cur.clear(); } else cur += ch;
    }
    secs.push_back(cur);
  }
  std::string sec = secs[0];
  bool neg = false;
  if (val < 0 && secs.size() > 1) { sec = secs[1]; neg = true; }
  else if (val == 0 && secs.size() > 2) sec = secs[2];
  std::string limpo = rx(rx(rx(rx(rx(sec, "\"[^\"]*\"", ""), "\\[[^\\]]*\\]", ""), "\\\\.", ""), "_.", ""), "\\*.", "");
  std::string lt = trim(limpo);
  if (tem(lt, "^general$", true) || trim(sec).empty()) {
    if (val == std::floor(val) && std::fabs(val) < 1e11) { char b[32]; snprintf(b, sizeof b, "%.0f", val); return b; }
    return trocar(jsNumero(val, 10), '.', ',');
  }
  if (lt == "@") return jsNumero(val, 0);
  std::string semPonto0 = std::regex_replace(limpo, std::regex("\\.0+"), "", std::regex_constants::format_first_only);
  if (tem(limpo, "[dmyhs]", true) && !tem(semPonto0, "[0#?]")) {
    double dias = (ctx.date1904 ? val + 1462 : val) - 25569;
    long long ms = (long long)std::floor(dias * 86400000.0 + 0.5);
    long long dd = ms >= 0 ? ms / 86400000 : -((-ms + 86400000 - 1) / 86400000);
    long long resto = ms - dd * 86400000;
    int Y, M, D;
    civil(dd, Y, M, D);
    int hh = (int)(resto / 3600000), mi = (int)(resto / 60000 % 60), ss = (int)(resto / 1000 % 60);
    bool temData = tem(limpo, "[dy]", true) || (tem(limpo, "m", true) && !tem(limpo, "[hs]", true));
    bool temHora = tem(limpo, "h", true);
    std::string out;
    if (temData) out = p2(D) + "/" + p2(M) + "/" + std::to_string(Y);
    if (temHora) {
      size_t ph = limpo.find_last_of("hH");
      std::string depois = limpo.substr(ph + 1);
      std::string hora = p2(hh) + ":" + p2(mi) + (tem(depois, "s", true) ? ":" + p2(ss) : "");
      out = out.empty() ? hora : out + " " + hora;
    }
    return out;
  }
  // número
  auto firstPh = sec.find_first_of("0#?");
  auto lastPh = sec.find_last_of("0#?");
  auto limpaPrePos = [](std::string s) {
    s = rx(s, "\\[\\$([^\\]-]*)[^\\]]*\\]", "$1");
    s = rx(s, "\\[[^\\]]*\\]", "");
    s = rx(s, "\"([^\"]*)\"", "$1");
    s = rx(s, "\\\\(.)", "$1");
    s = rx(s, "_.", " ");
    s = rx(s, "\\*.", "");
    s = rx(s, "[()]", "");
    return s;
  };
  std::string pre = (firstPh != std::string::npos && firstPh > 0) ? limpaPrePos(sec.substr(0, firstPh)) : "";
  std::string pos = lastPh != std::string::npos ? rx(limpaPrePos(sec.substr(lastPh + 1)), "%", "") : "";
  const std::string& core = limpo;
  bool pct = core.find('%') != std::string::npos;
  double x = pct ? val * 100 : val;
  if (tem(core, "E\\+", true)) {
    std::string mant = core.substr(0, core.find_first_of("eE"));
    auto pp = mant.find('.');
    int dec = 0;
    if (pp != std::string::npos) {
      std::string fr = mant.substr(pp + 1);
      auto f2 = fr.find('.');
      if (f2 != std::string::npos) fr = fr.substr(0, f2);
      for (char c : fr) if (c == '0' || c == '#') dec++;
    }
    char b[64];
    snprintf(b, sizeof b, "%.*e", dec, x);
    std::string s = b;
    auto e = s.find('e');
    int ex = atoi(s.c_str() + e + 1);
    s = s.substr(0, e) + "E" + (ex < 0 ? "-" : "+") + std::to_string(std::abs(ex));
    return trocar(s, '.', ',');
  }
  std::string dps;
  {
    auto pp = core.find('.');
    if (pp != std::string::npos) {
      std::string fr = core.substr(pp + 1);
      auto f2 = fr.find('.');
      if (f2 != std::string::npos) fr = fr.substr(0, f2);
      dps = fr;
    }
  }
  int dp = 0;
  while (dp < (int)dps.size() && (dps[dp] == '0' || dps[dp] == '#' || dps[dp] == '?')) dp++;
  int zeros = 0;
  while (zeros < (int)dps.size() && dps[zeros] == '0') zeros++;
  std::string inteiro = core.substr(0, core.find('.'));
  std::string s = nf(x, dp, inteiro.find(',') != std::string::npos);
  if (zeros < dp && s.find(',') != std::string::npos) {
    auto c = s.find(',');
    std::string i = s.substr(0, c), f = s.substr(c + 1);
    while (!f.empty() && f.back() == '0') f.pop_back();
    while ((int)f.size() < zeros) f += "0";
    s = f.empty() ? i : i + "," + f;
  }
  std::string sinal = (x < 0 && !neg) ? "-" : "";
  bool parens = neg && sec.find('(') != std::string::npos;
  return (parens ? "(" : "") + sinal + trimStart(pre) + s + (pct ? "%" : "") + trimEnd(pos) + (parens ? ")" : "");
}

/* ================= texto: codificação WinAnsi e medidas ================= */
static std::vector<int> toWin(const std::string& s) {
  static std::unordered_map<unsigned, int> WIN = [] {
    std::unordered_map<unsigned, int> w;
    for (int i = 32; i < 127; i++) w[i] = i;
    for (int i = 160; i < 256; i++) w[i] = i;
    std::pair<unsigned, int> extra[] = {{0x20AC,0x80},{0x201A,0x82},{0x0192,0x83},{0x201E,0x84},{0x2026,0x85},{0x2020,0x86},{0x2021,0x87},{0x02C6,0x88},{0x2030,0x89},{0x0160,0x8A},{0x2039,0x8B},{0x0152,0x8C},{0x017D,0x8E},{0x2018,0x91},{0x2019,0x92},{0x201C,0x93},{0x201D,0x94},{0x2022,0x95},{0x2013,0x96},{0x2014,0x97},{0x02DC,0x98},{0x2122,0x99},{0x0161,0x9A},{0x203A,0x9B},{0x0153,0x9C},{0x017E,0x9E},{0x0178,0x9F}};
    for (auto& e : extra) w[e.first] = e.second;
    return w;
  }();
  // acentos combinados (forma NFD) mais comuns -> letra composta
  static const std::map<std::pair<unsigned, unsigned>, unsigned> NFC = [] {
    std::map<std::pair<unsigned, unsigned>, unsigned> m;
    const char* base = "AEIOUaeiou";
    unsigned agudo[] = {0xC1,0xC9,0xCD,0xD3,0xDA,0xE1,0xE9,0xED,0xF3,0xFA};
    unsigned grave[] = {0xC0,0xC8,0xCC,0xD2,0xD9,0xE0,0xE8,0xEC,0xF2,0xF9};
    unsigned circ[] = {0xC2,0xCA,0xCE,0xD4,0xDB,0xE2,0xEA,0xEE,0xF4,0xFB};
    unsigned trema[] = {0xC4,0xCB,0xCF,0xD6,0xDC,0xE4,0xEB,0xEF,0xF6,0xFC};
    for (int i = 0; i < 10; i++) {
      m[{(unsigned)base[i], 0x301}] = agudo[i];
      m[{(unsigned)base[i], 0x300}] = grave[i];
      m[{(unsigned)base[i], 0x302}] = circ[i];
      m[{(unsigned)base[i], 0x308}] = trema[i];
    }
    m[{'A', 0x303}] = 0xC3; m[{'O', 0x303}] = 0xD5; m[{'N', 0x303}] = 0xD1;
    m[{'a', 0x303}] = 0xE3; m[{'o', 0x303}] = 0xF5; m[{'n', 0x303}] = 0xF1;
    m[{'C', 0x327}] = 0xC7; m[{'c', 0x327}] = 0xE7;
    return m;
  }();
  std::vector<unsigned> cps;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = s[i];
    unsigned cp;
    int n;
    if (c < 0x80) { cp = c; n = 1; }
    else if ((c >> 5) == 6) { cp = c & 31; n = 2; }
    else if ((c >> 4) == 14) { cp = c & 15; n = 3; }
    else if ((c >> 3) == 30) { cp = c & 7; n = 4; }
    else { cp = 0xFFFD; n = 1; }
    for (int k = 1; k < n; k++) {
      if (i + k >= s.size()) { cp = 0xFFFD; n = k; break; }
      cp = (cp << 6) | (s[i + k] & 63);
    }
    i += n;
    if (!cps.empty()) {
      auto it = NFC.find({cps.back(), cp});
      if (it != NFC.end()) { cps.back() = it->second; continue; }
    }
    cps.push_back(cp);
  }
  std::vector<int> out;
  out.reserve(cps.size());
  for (unsigned cp : cps) {
    if (cp == 9) { out.push_back(32); continue; }
    auto it = WIN.find(cp);
    out.push_back(it == WIN.end() ? 63 : it->second);
  }
  return out;
}
static double medir(const std::string& s, const Fonte* f) {
  const int* tb = f && f->b ? WB : WR;
  double a = 0;
  for (int b : toWin(s)) a += (b >= 32 && b < 256) ? tb[b - 32] : 556;
  return a;
}

/* ================= layout em páginas ================= */
struct Rect { double x, y, w, h; };
struct Op {
  enum K { Fill, Line, Text } k;
  Rect r{};          // fill / retângulo do texto
  RGB c;             // cor do fill / linha
  double x1 = 0, y1 = 0, x2 = 0, y2 = 0, w = 0;  // linha
  std::string txt;
  Rect clip{};
  std::string h, v;
  bool wrap = false;
  int indent = 0;
  const Fonte* font = nullptr;
  double s = 1;
};
struct Pagina { double pw, ph; std::vector<Op> ops; };

static const double A4[2] = {595.28, 841.89};
static double colPt(double w) { return std::max(0.0, std::floor(w * 7 + 0.5)) * 0.75; }

static std::vector<Pagina> layout(const Aba& sh, bool fit) {
  const Ctx& ctx = *sh.ctx;
  auto rowH = [&](int r) { auto it = sh.rows.find(r); return it != sh.rows.end() ? it->second.h : sh.defRowH; };
  auto rowHid = [&](int r) { auto it = sh.rows.find(r); return it != sh.rows.end() && it->second.hidden; };
  auto colW = [&](int c) { auto it = sh.cols.find(c); return colPt(it != sh.cols.end() ? it->second.w : sh.defColW); };
  auto colHid = [&](int c) { auto it = sh.cols.find(c); return it != sh.cols.end() && (it->second.hidden || it->second.w == 0); };
  auto cellAt = [&](int r, int c) -> const Celula* { auto it = sh.cells.find(chave(r, c)); return it == sh.cells.end() ? nullptr : &it->second; };
  auto xfIdx = [&](int s) -> const Xf& { return (s >= 0 && s < (int)ctx.xfs.size()) ? ctx.xfs[s] : ctx.xfs[0]; };

  int r1 = 1000000000, r2 = 0, c1 = 1000000000, c2 = 0;
  for (auto& kv : sh.cells) {
    int r = (int)(kv.first / 20000), c = (int)(kv.first % 20000);
    if (rowHid(r) || colHid(c)) continue;
    const Celula& cl = kv.second;
    const Xf& xf = xfIdx(cl.s);
    bool t = !cl.v.vazio() || xf.fill || !xf.border.vazia();
    if (t) { r1 = std::min(r1, r); r2 = std::max(r2, r); c1 = std::min(c1, c); c2 = std::max(c2, c); }
  }
  if (ctx.temPrintArea) {
    std::string pa = ctx.printArea;
    pa.erase(std::remove(pa.begin(), pa.end(), '$'), pa.end());
    std::smatch m;
    if (std::regex_search(pa, m, std::regex("!?([A-Z]+\\d+):([A-Z]+\\d+)\\s*$", std::regex::ECMAScript | std::regex::icase))) {
      auto [a1, b1] = colIdx(m[1].str());
      auto [a2, b2] = colIdx(m[2].str());
      c1 = a1; r1 = b1; c2 = a2; r2 = b2;
    }
  }
  if (r2 == 0) return {};
  std::vector<int> vcols, vrows;
  for (int c = c1; c <= c2; c++) if (!colHid(c)) vcols.push_back(c);
  for (int r = r1; r <= r2; r++) if (!rowHid(r)) vrows.push_back(r);
  std::unordered_map<int, double> cx;
  double acc = 0;
  for (int c : vcols) { cx[c] = acc; acc += colW(c); }
  double totalW = acc;

  struct Pg { double pw, ph, aw, s; };
  auto pick = [&](bool land) {
    double pw = land ? A4[1] : A4[0], ph = land ? A4[0] : A4[1];
    double aw = pw - sh.ml - sh.mr;
    double s = fit ? (totalW > 0 ? std::min(1.0, aw / totalW) : 1.0) : 1.0;
    return Pg{pw, ph, aw, s};
  };
  Pg pg = pick(sh.landscape);
  if (!sh.landscape && fit && pg.s < 0.7) { Pg l = pick(true); if (l.s > pg.s * 1.15) pg = l; }
  const double pw = pg.pw, ph = pg.ph, s = pg.s, ah = ph - sh.mt - sh.mb;

  // mapa de mesclagens
  std::unordered_map<long long, const Merge*> mIn;
  for (auto& m : sh.merges) {
    long long area = (long long)(m.r2 - m.r1 + 1) * (m.c2 - m.c1 + 1);
    if (area <= 0 || area > 4000000) continue;
    for (int r = m.r1; r <= m.r2; r++)
      for (int c = m.c1; c <= m.c2; c++) mIn[chave(r, c)] = &m;
  }
  auto mergeAt = [&](int r, int c) -> const Merge* { auto it = mIn.find(chave(r, c)); return it == mIn.end() ? nullptr : it->second; };

  // paginação por linhas
  std::vector<std::vector<int>> pages;
  std::vector<int> cur;
  double used = 0;
  for (int r : vrows) {
    double h = rowH(r) * s;
    if (!cur.empty() && used + h > ah) { pages.push_back(cur); cur.clear(); used = 0; }
    cur.push_back(r);
    used += h;
  }
  if (!cur.empty()) pages.push_back(cur);

  std::vector<Pagina> out;
  for (auto& pr : pages) {
    std::unordered_map<int, double> ry;
    double y = sh.mt;
    for (int r : pr) { ry[r] = y; y += rowH(r) * s; }
    auto X = [&](int c) { return sh.ml + cx[c] * s; };
    auto Y = [&](int r) { return ry[r]; };
    Pagina pag{pw, ph, {}};
    auto& ops = pag.ops;
    auto xfOf = [&](int r, int c) -> const Xf& {
      const Celula* cl = cellAt(r, c);
      if (cl) return xfIdx(cl->s);
      auto it = sh.cols.find(c);
      return xfIdx(it != sh.cols.end() && !it->second.s.empty() ? atoi(it->second.s.c_str()) : 0);
    };
    auto rectOf = [&](const Merge* m, Rect& rc) {
      std::vector<int> rs, cs;
      for (int r : pr) if (r >= m->r1 && r <= m->r2) rs.push_back(r);
      for (int c : vcols) if (c >= m->c1 && c <= m->c2) cs.push_back(c);
      if (rs.empty() || cs.empty()) return false;
      int lc = cs.back(), lr = rs.back();
      rc = Rect{X(cs[0]), Y(rs[0]), X(lc) + colW(lc) * s - X(cs[0]), Y(lr) + rowH(lr) * s - Y(rs[0])};
      return true;
    };
    auto primeiro = [&](const Merge* m, int r, int c) {
      int fr = -1, fc = -1;
      for (int rr : pr) if (rr >= m->r1) { fr = rr; break; }
      for (int cc : vcols) if (cc >= m->c1) { fc = cc; break; }
      return r == fr && c == fc;
    };
    // 1) fundos
    for (int r : pr)
      for (int c : vcols) {
        const Merge* m = mergeAt(r, c);
        const Xf* xf;
        Rect rc;
        if (m) {
          if (!primeiro(m, r, c)) continue;
          xf = &xfOf(m->r1, m->c1);
          if (!rectOf(m, rc)) continue;
        } else {
          xf = &xfOf(r, c);
          rc = Rect{X(c), Y(r), colW(c) * s, rowH(r) * s};
        }
        if (xf->fill) { Op o{Op::Fill}; o.r = rc; o.c = *xf->fill; ops.push_back(o); }
      }
    // 2) textos
    auto temValor = [&](int r, int c) { const Celula* o = cellAt(r, c); return o && !o->v.vazio(); };
    for (int r : pr)
      for (size_t ci = 0; ci < vcols.size(); ci++) {
        int c = vcols[ci];
        const Merge* m = mergeAt(r, c);
        Rect rc;
        const Celula* cl;
        const Xf* xf;
        if (m) {
          if (!primeiro(m, r, c)) continue;
          cl = cellAt(m->r1, m->c1);
          xf = &xfOf(m->r1, m->c1);
          if (!rectOf(m, rc)) continue;
        } else {
          cl = cellAt(r, c);
          xf = &xfOf(r, c);
          rc = Rect{X(c), Y(r), colW(c) * s, rowH(r) * s};
        }
        if (!cl || cl->v.vazio()) continue;
        std::string txt = formatar(*cl, *xf, ctx);
        if (txt.empty()) continue;
        std::string h = xf->h;
        if (h.empty() || h == "general") h = (cl->t == "n" && cl->v.k == Valor::Num) ? "right" : (cl->t == "b" ? "center" : "left");
        if (h == "centerContinuous") h = "center";
        if (h == "fill" || h == "justify" || h == "distributed") h = "left";
        const Fonte* font = xf->font ? xf->font : &ctx.fonts[0];
        // texto à esquerda pode "vazar" para células vazias à direita (como no Excel)
        Rect clip = rc;
        if (!m && !xf->wrap && h == "left") {
          for (size_t k = ci + 1; k < vcols.size(); k++) {
            int cc = vcols[k];
            if (temValor(r, cc) || mergeAt(r, cc)) break;
            clip.w += colW(cc) * s;
            if (medir(txt, font) * font->sz * s / 1000 < clip.w - 3 * s) break;
          }
        }
        if (!m && !xf->wrap && h == "center") {
          double tw = medir(txt, font) * font->sz * s / 1000;
          if (tw > rc.w) {
            int L = (int)ci - 1, R = (int)ci + 1;
            double ex = (tw - rc.w) / 2, addL = 0, addR = 0;
            while (addL < ex && L >= 0) { if (temValor(r, vcols[L]) || mergeAt(r, vcols[L])) break; addL += colW(vcols[L]) * s; L--; }
            while (addR < ex && R < (int)vcols.size()) { if (temValor(r, vcols[R]) || mergeAt(r, vcols[R])) break; addR += colW(vcols[R]) * s; R++; }
            double g = std::min(addL, addR);
            clip = Rect{rc.x - g, rc.y, rc.w + 2 * g, rc.h};
          }
        }
        Op o{Op::Text};
        o.txt = txt; o.r = rc; o.clip = clip; o.h = h; o.v = xf->v; o.wrap = xf->wrap; o.indent = xf->indent; o.font = font; o.s = s;
        ops.push_back(o);
      }
    // 3) bordas (vale a da própria célula OU a da vizinha, mesmo oculta)
    for (int r : pr)
      for (int c : vcols) {
        const Merge* m = mergeAt(r, c);
        const Bordas& b = xfOf(r, c).border;
        auto L = b.left ? b.left : xfOf(r, c - 1).border.right;
        auto R = b.right ? b.right : xfOf(r, c + 1).border.left;
        auto T = b.top ? b.top : xfOf(r - 1, c).border.bottom;
        auto B = b.bottom ? b.bottom : xfOf(r + 1, c).border.top;
        if (!L && !R && !T && !B) continue;
        double x = X(c), yy = Y(r), w = colW(c) * s, hh = rowH(r) * s;
        auto edge = [&](int k) {
          if (!m) return true;
          return (k == 0 && c == m->c1) || (k == 1 && c == m->c2) || (k == 2 && r == m->r1) || (k == 3 && r == m->r2);
        };
        auto linha = [&](double x1, double y1, double x2, double y2, const Borda& bd) {
          Op o{Op::Line}; o.x1 = x1; o.y1 = y1; o.x2 = x2; o.y2 = y2; o.w = bd.w; o.c = bd.c; ops.push_back(o);
        };
        if (T && edge(2)) linha(x, yy, x + w, yy, *T);
        if (B && edge(3)) linha(x, yy + hh, x + w, yy + hh, *B);
        if (L && edge(0)) linha(x, yy, x, yy + hh, *L);
        if (R && edge(1)) linha(x + w, yy, x + w, yy + hh, *R);
      }
    out.push_back(std::move(pag));
  }
  return out;
}

/* ================= linhas de texto ================= */
struct Linha { std::string str; double x, y, size, tw; };

static std::vector<std::string> dividir(const std::string& s, const std::string& sep) {
  std::vector<std::string> o;
  size_t a = 0;
  for (;;) {
    size_t b = s.find(sep, a);
    if (b == std::string::npos) { o.push_back(s.substr(a)); break; }
    o.push_back(s.substr(a, b - a));
    a = b + sep.size();
  }
  return o;
}
static std::vector<std::string> quebrar(const std::string& txt, const Fonte* font, double size, double maxW) {
  std::vector<std::string> linhas;
  std::string t = rx(txt, "\\r\\n", "\n");
  for (auto& par : dividir(t, "\n")) {
    std::string cur;
    for (auto& w : dividir(par, " ")) {
      std::string tt = cur.empty() ? w : cur + " " + w;
      if (!cur.empty() && medir(tt, font) * size / 1000 > maxW) { linhas.push_back(cur); cur = w; }
      else cur = tt;
    }
    linhas.push_back(cur);
  }
  return linhas;
}
static std::vector<Linha> linhasTexto(const Op& o) {
  const Fonte* f = o.font;
  double size = f->sz * o.s, pad = 2 * o.s + o.indent * 9 * o.s;
  const Rect& rc = o.r;
  std::vector<std::string> lines = o.wrap ? quebrar(o.txt, f, size, rc.w - 2 * pad)
                                          : std::vector<std::string>{rx(o.txt, "\\r?\\n", " ")};
  double lh = size * 1.17, tot = lh * lines.size(), y0;
  if (o.v == "top") y0 = rc.y + 2 * o.s + size * 0.9;
  else if (o.v == "center") y0 = rc.y + (rc.h - tot) / 2 + size * 0.9;
  else y0 = rc.y + rc.h - tot + size * 0.9 - 2 * o.s;
  std::vector<Linha> out;
  for (size_t i = 0; i < lines.size(); i++) {
    double tw = medir(lines[i], f) * size / 1000;
    double x = o.h == "right" ? rc.x + rc.w - pad - tw : o.h == "center" ? rc.x + (rc.w - tw) / 2 : rc.x + pad;
    out.push_back(Linha{lines[i], x, y0 + i * lh, size, tw});
  }
  return out;
}

/* ================= PDF ================= */
static std::string fmtN(double n) {
  double r = std::floor(n * 100 + 0.5) / 100;
  char b[64];
  snprintf(b, sizeof b, "%.2f", r);
  std::string s = b;
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  if (s == "-0" || s.empty()) s = "0";
  return s;
}
static std::string rgbStr(const RGB& c) { return fmtN(c.r / 255.0) + " " + fmtN(c.g / 255.0) + " " + fmtN(c.b / 255.0); }
static std::string pdfString(const std::vector<int>& bytes) {
  std::string s = "(";
  for (int b : bytes) {
    if (b == 40 || b == 41 || b == 92) { s += '\\'; s += (char)b; }
    else if (b < 32 || b > 126) { char o[8]; snprintf(o, sizeof o, "\\%03o", b); s += o; }
    else s += (char)b;
  }
  return s + ")";
}

static Bytes gerarPDF(const std::vector<Pagina>& pages, const std::string& title) {
  std::vector<Bytes> objs;
  auto add = [&](const std::string& s) { objs.emplace_back(s.begin(), s.end()); return (int)objs.size(); };
  const char* fontNames[] = {"Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique"};
  int fontIds[4];
  for (int i = 0; i < 4; i++)
    fontIds[i] = add(std::string("<< /Type /Font /Subtype /Type1 /BaseFont /") + fontNames[i] + " /Encoding /WinAnsiEncoding >>");
  int pagesId = add("");
  std::vector<int> kidsIds;
  std::string fontRes;
  for (int i = 0; i < 4; i++) fontRes += (i ? " " : "") + std::string("/F") + std::to_string(i + 1) + " " + std::to_string(fontIds[i]) + " 0 R";
  for (auto& p : pages) {
    std::string c;
    auto Y = [&](double y) { return p.ph - y; };
    for (auto& o : p.ops) {
      if (o.k == Op::Fill) {
        c += rgbStr(o.c) + " rg " + fmtN(o.r.x) + " " + fmtN(Y(o.r.y + o.r.h)) + " " + fmtN(o.r.w) + " " + fmtN(o.r.h) + " re f\n";
      } else if (o.k == Op::Line) {
        c += rgbStr(o.c) + " RG " + fmtN(o.w) + " w " + fmtN(o.x1) + " " + fmtN(Y(o.y1)) + " m " + fmtN(o.x2) + " " + fmtN(Y(o.y2)) + " l S\n";
      } else {
        int fi = (o.font->b ? 1 : 0) + (o.font->i ? 2 : 0);
        std::string clip = fmtN(o.clip.x) + " " + fmtN(Y(o.clip.y + o.clip.h)) + " " + fmtN(o.clip.w) + " " + fmtN(o.clip.h) + " re W n ";
        c += "q " + clip + rgbStr(o.font->color) + " rg BT /F" + std::to_string(fi + 1) + " " + fmtN(o.font->sz * o.s) + " Tf ";
        bool first = true;
        double px = 0, py = 0;
        auto linhas = linhasTexto(o);
        for (auto& l : linhas) {
          double x = l.x, y = Y(l.y);
          c += first ? fmtN(x) + " " + fmtN(y) + " Td " : fmtN(x - px) + " " + fmtN(y - py) + " Td ";
          px = x; py = y; first = false;
          c += pdfString(toWin(l.str)) + " Tj ";
        }
        c += "ET Q\n";
        if (o.font->u)
          for (auto& l : linhas)
            c += "q " + clip + rgbStr(o.font->color) + " RG " + fmtN(l.size * 0.06) + " w " + fmtN(l.x) + " " + fmtN(Y(l.y) - l.size * 0.12) +
                 " m " + fmtN(l.x + l.tw) + " " + fmtN(Y(l.y) - l.size * 0.12) + " l S Q\n";
      }
    }
    Bytes z = deflar(c);
    std::string head = "<< /Length " + std::to_string(z.size()) + " /Filter /FlateDecode >>\nstream\n";
    Bytes obj(head.begin(), head.end());
    obj.insert(obj.end(), z.begin(), z.end());
    const char* fim = "\nendstream";
    obj.insert(obj.end(), fim, fim + strlen(fim));
    objs.push_back(std::move(obj));
    int cid = (int)objs.size();
    kidsIds.push_back(add("<< /Type /Page /Parent " + std::to_string(pagesId) + " 0 R /MediaBox [0 0 " + fmtN(p.pw) + " " + fmtN(p.ph) +
                          "] /Resources << /Font << " + fontRes + " >> >> /Contents " + std::to_string(cid) + " 0 R >>"));
  }
  std::string kids;
  for (size_t i = 0; i < kidsIds.size(); i++) kids += (i ? " " : "") + std::to_string(kidsIds[i]) + " 0 R";
  std::string pg = "<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(kidsIds.size()) + " >>";
  objs[pagesId - 1].assign(pg.begin(), pg.end());
  int infoId = add("<< /Title " + pdfString(toWin(title)) + " /Producer (XLSM para PDF) >>");
  int catId = add("<< /Type /Catalog /Pages " + std::to_string(pagesId) + " 0 R >>");

  Bytes out;
  auto put = [&](const std::string& s) { out.insert(out.end(), s.begin(), s.end()); };
  put("%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");
  std::vector<size_t> xref;
  for (size_t i = 0; i < objs.size(); i++) {
    xref.push_back(out.size());
    put(std::to_string(i + 1) + " 0 obj\n");
    out.insert(out.end(), objs[i].begin(), objs[i].end());
    put("\nendobj\n");
  }
  size_t start = out.size();
  std::string x = "xref\n0 " + std::to_string(objs.size() + 1) + "\n0000000000 65535 f \n";
  for (size_t o : xref) { char b[32]; snprintf(b, sizeof b, "%010zu 00000 n \n", o); x += b; }
  x += "trailer\n<< /Size " + std::to_string(objs.size() + 1) + " /Root " + std::to_string(catId) + " 0 R /Info " + std::to_string(infoId) +
       " 0 R >>\nstartxref\n" + std::to_string(start) + "\n%%EOF\n";
  put(x);
  return out;
}

/* ================= entrada ================= */
Bytes converterPlanilha(const uint8_t* dados, size_t tam, const std::string& titulo, bool fit, bool todas, int* paginas) {
  std::vector<Aba> abas = lerPasta(dados, tam);
  std::vector<Pagina> pags;
  for (size_t i = 0; i < abas.size() && (todas || i < 1); i++) {
    auto p = layout(abas[i], fit);
    for (auto& x : p) pags.push_back(std::move(x));
  }
  if (pags.empty()) throw Erro("nenhuma célula visível com conteúdo");
  if (paginas) *paginas = (int)pags.size();
  return gerarPDF(pags, titulo);
}

}  // namespace xp
