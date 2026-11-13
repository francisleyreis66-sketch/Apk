package com.xlsmpdf.app;

import android.app.Activity;
import android.app.Dialog;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.UriPermission;
import android.content.res.Configuration;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.pdf.PdfRenderer;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.MediaStore;
import android.provider.OpenableColumns;
import android.text.InputType;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.text.Collator;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Tela do app. Toda a conversão e a junção de PDFs acontecem no motor em C++ (classe Motor);
 * aqui só ficam os botões, a escolha de arquivos e o envio para o WhatsApp.
 */
public class MainActivity extends Activity {

    /* ================= cores (claro / escuro) ================= */
    private int BG, SURFACE, INK, MUTED, ACCENT, ACCENT_INK, LINE, STRIPE, ERR, OK;
    private static final int ZAP = 0xFF25D366, ZAP_INK = 0xFF0B2E17;

    private static final int REQ_PLANILHAS = 1, REQ_XLSM_ZAP = 2, REQ_PASTA_UMA = 3, REQ_PASTA_GUARDAR = 4,
            REQ_PASTA_E_FAZER = 5, REQ_PDFS = 6, REQ_SALVAR = 7;

    private final Handler ui = new Handler(Looper.getMainLooper());
    private final ExecutorService fundo = Executors.newSingleThreadExecutor();
    private SharedPreferences prefs;

    /* ================= dados ================= */
    static final class Item { String nome; File pdf; int paginas; String erro; View linha; ImageView img; TextView meta; LinearLayout acts; }
    static final class Pdf { String nome; File arq; Uri uri; long tam; Pdf(String n, File f, Uri u, long t) { nome = n; arq = f; uri = u; tam = t; } }
    static final class Fonte { Uri uri; String nome; Fonte(Uri u, String n) { uri = u; nome = n; } }

    private final List<Item> ITENS = new ArrayList<>();
    private final List<Pdf> JL = new ArrayList<>();
    private File juntoFile;
    private boolean nomeMexeu = false, mudandoNome = false;
    private byte[] pendenteSalvar;

    /* ================= views ================= */
    private LinearLayout painelConv, painelJunta, lista, plist, saida, autoPasta;
    private Button tabConv, tabJunta, fazerTudo, definirPasta, dlAll, shareAll, clear, usarConv, juntar, limpar2, dlJunto, shareJunto;
    private CheckBox optFit, optAll;
    private TextView status, status2, pastaInfo;
    private EditText nomeSaida;
    private ScrollView scroll;

    /* janela de progresso */
    private Dialog modal;
    private TextView jobTit, jobNum, jobFase, jobMsg, jobEtapa, jobPct;
    private View jobConta;
    private ProgressBar jobBar;
    private Button jobEnviar, jobCancel;
    private int jobTotal = 4;
    private double jobPctV = 0;
    private Job JOB = null;
    private boolean OCUPADO = false;

    /* ================= ciclo de vida ================= */
    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        prefs = getSharedPreferences("xlsm-para-pdf", MODE_PRIVATE);
        definirCores();
        Window w = getWindow();
        w.setStatusBarColor(BG);
        w.setNavigationBarColor(BG);
        if (!escuro() && Build.VERSION.SDK_INT >= 23)
            w.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR);
        montarTela();
        montarJanela();
        limparCacheAntigo();
        receberIntent(getIntent());
    }

    @Override
    protected void onNewIntent(Intent i) {
        super.onNewIntent(i);
        receberIntent(i);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        fundo.shutdownNow();
    }

    private boolean escuro() {
        return (getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES;
    }

    private void definirCores() {
        if (escuro()) {
            BG = 0xFF141812; SURFACE = 0xFF1D231A; INK = 0xFFE7EEE0; MUTED = 0xFFA2AE99; ACCENT = 0xFF8CC152;
            ACCENT_INK = 0xFF11160D; LINE = 0xFF334030; STRIPE = 0xFF232B1F; ERR = 0xFFFF8A80; OK = 0xFF81C784;
        } else {
            BG = 0xFFF4F6F1; SURFACE = 0xFFFFFFFF; INK = 0xFF1D241A; MUTED = 0xFF5F6B58; ACCENT = 0xFF4F7A28;
            ACCENT_INK = 0xFFFFFFFF; LINE = 0xFFD5DCCD; STRIPE = 0xFFEEF3E6; ERR = 0xFFC62828; OK = 0xFF2E7D32;
        }
    }

    /* ================= peças de interface ================= */
    private int dp(float v) { return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics()); }

    private GradientDrawable caixa(int fundo, int borda, float raio, int larguraBorda, boolean tracejada) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(fundo);
        g.setCornerRadius(dp(raio));
        if (larguraBorda > 0) {
            if (tracejada) g.setStroke(dp(larguraBorda), borda, dp(6), dp(4));
            else g.setStroke(dp(larguraBorda), borda);
        }
        return g;
    }

    private Typeface display() { return Typeface.create("sans-serif-condensed", Typeface.BOLD); }

    private TextView texto(String s, float sp, int cor, boolean negrito) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(cor);
        if (negrito) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    private TextView titulo(String s, float sp) {
        TextView t = texto(s.toUpperCase(Locale.ROOT), sp, INK, false);
        t.setTypeface(display());
        return t;
    }

    private Button botao(String s, boolean primario) {
        Button bt = new Button(this);
        bt.setText(s);
        bt.setAllCaps(false);
        bt.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        bt.setTypeface(Typeface.DEFAULT_BOLD);
        bt.setMinHeight(dp(44));
        bt.setMinimumHeight(dp(44));
        bt.setPadding(dp(14), dp(8), dp(14), dp(8));
        if (Build.VERSION.SDK_INT >= 21) bt.setStateListAnimator(null);
        estilo(bt, primario);
        return bt;
    }

    private void estilo(Button bt, boolean primario) {
        bt.setTextColor(primario ? ACCENT_INK : INK);
        bt.setBackground(caixa(primario ? ACCENT : SURFACE, primario ? ACCENT : LINE, 8, 1, false));
    }

    private void estiloZap(Button bt) {
        bt.setTextColor(ZAP_INK);
        bt.setTextSize(TypedValue.COMPLEX_UNIT_SP, 17);
        bt.setBackground(caixa(ZAP, ZAP, 8, 1, false));
    }

    private void habilitar(View v, boolean on) {
        v.setEnabled(on);
        v.setAlpha(on ? 1f : 0.5f);
    }

    private LinearLayout vertical(int gapDp) {
        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.VERTICAL);
        l.setTag(gapDp);
        return l;
    }

    /** adiciona com espaço em cima (gap da coluna) */
    private void add(LinearLayout pai, View v) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        int gap = pai.getTag() instanceof Integer ? (Integer) pai.getTag() : 0;
        if (pai.getChildCount() > 0) lp.topMargin = dp(gap);
        pai.addView(v, lp);
    }

    private void addWrap(LinearLayout pai, View v) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        int gap = pai.getTag() instanceof Integer ? (Integer) pai.getTag() : 0;
        if (pai.getChildCount() > 0) lp.topMargin = dp(gap);
        pai.addView(v, lp);
    }

    /** linha de botões que quebra para baixo quando não cabe */
    private LinearLayout linhaBotoes(Button... bs) {
        LinearLayout col = vertical(8);
        for (Button b : bs) add(col, b);
        return col;
    }

    private LinearLayout card() {
        LinearLayout c = vertical(12);
        c.setPadding(dp(16), dp(16), dp(16), dp(16));
        c.setBackground(caixa(SURFACE, LINE, 10, 1, false));
        return c;
    }

    private LinearLayout destaque() {
        LinearLayout c = vertical(6);
        c.setPadding(dp(12), dp(12), dp(12), dp(12));
        c.setBackground(caixa(STRIPE, ACCENT, 10, 1, false));
        return c;
    }

    private TextView pequeno(String s) { return texto(s, 13, MUTED, false); }

    private CheckBox opcao(String s) {
        CheckBox c = new CheckBox(this);
        c.setText(s);
        c.setTextColor(INK);
        c.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        c.setChecked(true);
        if (Build.VERSION.SDK_INT >= 21) c.setButtonTintList(android.content.res.ColorStateList.valueOf(ACCENT));
        return c;
    }

    /* ================= tela ================= */
    private void montarTela() {
        scroll = new ScrollView(this);
        scroll.setBackgroundColor(BG);
        scroll.setFillViewport(true);
        LinearLayout raiz = vertical(16);
        raiz.setPadding(dp(16), dp(20), dp(16), dp(48));
        scroll.addView(raiz, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        LinearLayout header = vertical(6);
        TextView h1 = titulo("XLSM para PDF", 34);
        add(header, h1);
        add(header, texto("Escolha uma ou várias planilhas (.xlsm ou .xlsx). Cada uma vira um PDF do jeito que aparece no Excel: só as linhas e colunas visíveis, com cores, bordas e data em dd/mm/aaaa. Na aba Juntar PDFs, une vários PDFs em um só. Funciona sem internet.", 15, MUTED, false));
        add(raiz, header);

        // abas
        LinearLayout tabs = new LinearLayout(this);
        tabs.setOrientation(LinearLayout.HORIZONTAL);
        tabs.setPadding(dp(6), dp(6), dp(6), dp(6));
        tabs.setBackground(caixa(SURFACE, LINE, 10, 1, false));
        tabConv = new Button(this);
        tabJunta = new Button(this);
        for (Button t : new Button[]{tabConv, tabJunta}) {
            t.setAllCaps(false);
            t.setTypeface(display());
            t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
            if (Build.VERSION.SDK_INT >= 21) t.setStateListAnimator(null);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, dp(46), 1f);
            lp.leftMargin = dp(2); lp.rightMargin = dp(2);
            tabs.addView(t, lp);
        }
        tabConv.setText("PLANILHA → PDF");
        tabJunta.setText("JUNTAR PDFS");
        tabConv.setOnClickListener(v -> abrirAba(true));
        tabJunta.setOnClickListener(v -> abrirAba(false));
        add(raiz, tabs);

        /* ----- painel converter ----- */
        painelConv = vertical(16);
        LinearLayout c1 = card();
        add(c1, titulo("Arquivos", 20));
        LinearLayout drop = vertical(10);
        drop.setGravity(Gravity.CENTER_HORIZONTAL);
        drop.setPadding(dp(14), dp(22), dp(14), dp(22));
        drop.setBackground(caixa(SURFACE, LINE, 10, 2, true));
        Button escolher = botao("Escolher planilhas", true);
        escolher.setOnClickListener(v -> abrirSeletor(REQ_PLANILHAS, true, new String[]{
                "application/vnd.ms-excel.sheet.macroEnabled.12",
                "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
                "application/octet-stream", "application/zip"}));
        addWrap(drop, escolher);
        TextView ou = pequeno("toque para escolher uma ou várias de uma vez");
        ou.setGravity(Gravity.CENTER);
        add(drop, ou);
        add(c1, drop);

        optFit = opcao("Ajustar à largura da página");
        optAll = opcao("Todas as abas visíveis");
        LinearLayout opts = vertical(0);
        add(opts, optFit);
        add(opts, optAll);
        add(c1, opts);

        autoPasta = destaque();
        fazerTudo = botao("⚡ Fazer tudo", true);
        fazerTudo.setTextSize(TypedValue.COMPLEX_UNIT_SP, 17);
        fazerTudo.setOnClickListener(v -> fazerTudo());
        add(autoPasta, fazerTudo);
        pastaInfo = pequeno("Nenhuma pasta guardada ainda.");
        add(autoPasta, pastaInfo);
        definirPasta = botao("📁 Escolher / trocar a pasta", false);
        definirPasta.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        definirPasta.setOnClickListener(v -> abrirPasta(REQ_PASTA_GUARDAR));
        addWrap(autoPasta, definirPasta);
        add(c1, autoPasta);
        mostrarPasta();

        LinearLayout auto2 = destaque();
        Button soXlsm = botao("📄 Arquivos .xlsm → 1 PDF → WhatsApp", true);
        soXlsm.setOnClickListener(v -> abrirSeletor(REQ_XLSM_ZAP, true, new String[]{
                "application/vnd.ms-excel.sheet.macroEnabled.12", "application/octet-stream", "application/zip"}));
        add(auto2, soXlsm);
        add(auto2, pequeno("Vá até a pasta e marque os .xlsm: converte cada um em PDF, passa para a aba Juntar PDFs, junta tudo em um PDF só e abre o envio para o WhatsApp. Outros tipos de arquivo são ignorados."));
        Button pasta = botao("ou escolher a pasta (pega só os .xlsm dela)", false);
        pasta.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        pasta.setOnClickListener(v -> abrirPasta(REQ_PASTA_UMA));
        addWrap(auto2, pasta);
        add(c1, auto2);

        dlAll = botao("Juntar todos em um PDF", true);
        shareAll = botao("Compartilhar todos", false);
        clear = botao("Limpar lista", false);
        dlAll.setOnClickListener(v -> juntarTodosConvertidos());
        shareAll.setOnClickListener(v -> compartilharTodos());
        clear.setOnClickListener(v -> limparLista());
        add(c1, linhaBotoes(dlAll, shareAll, clear));
        status = texto("", 14, MUTED, false);
        add(c1, status);
        add(painelConv, c1);

        lista = vertical(10);
        add(painelConv, lista);
        add(raiz, painelConv);
        listaVazia();

        /* ----- painel juntar ----- */
        painelJunta = vertical(16);
        LinearLayout c2 = card();
        add(c2, titulo("Juntar PDFs em um só", 20));
        add(c2, pequeno("Escolha os PDFs, arrume a ordem com as setas e toque em Juntar em um PDF. As páginas ficam na ordem da lista."));
        LinearLayout drop2 = vertical(10);
        drop2.setGravity(Gravity.CENTER_HORIZONTAL);
        drop2.setPadding(dp(14), dp(22), dp(14), dp(22));
        drop2.setBackground(caixa(SURFACE, LINE, 10, 2, true));
        Button escolherPdfs = botao("Escolher PDFs", true);
        escolherPdfs.setOnClickListener(v -> abrirSeletor(REQ_PDFS, true, new String[]{"application/pdf"}));
        addWrap(drop2, escolherPdfs);
        usarConv = botao("Usar os PDFs convertidos na outra aba", false);
        usarConv.setOnClickListener(v -> {
            for (Item i : ITENS) if (i.pdf != null) JL.add(new Pdf(nomePdf(i.nome), i.pdf, null, i.pdf.length()));
            mudou();
        });
        addWrap(drop2, usarConv);
        add(c2, drop2);
        plist = vertical(6);
        add(c2, plist);
        LinearLayout nomeBox = vertical(4);
        add(nomeBox, pequeno("Nome do arquivo final"));
        nomeSaida = new EditText(this);
        nomeSaida.setText("PDFs_juntos");
        nomeSaida.setSingleLine(true);
        nomeSaida.setInputType(InputType.TYPE_CLASS_TEXT);
        nomeSaida.setTextColor(INK);
        nomeSaida.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        nomeSaida.setPadding(dp(10), dp(8), dp(10), dp(8));
        nomeSaida.setBackground(caixa(SURFACE, LINE, 6, 1, false));
        nomeSaida.addTextChangedListener(new android.text.TextWatcher() {
            public void beforeTextChanged(CharSequence s, int a, int b, int c) {}
            public void onTextChanged(CharSequence s, int a, int b, int c) {}
            public void afterTextChanged(android.text.Editable s) { if (!mudandoNome) nomeMexeu = true; }
        });
        add(nomeBox, nomeSaida);
        add(c2, nomeBox);
        juntar = botao("Juntar em um PDF", true);
        limpar2 = botao("Limpar lista", false);
        juntar.setOnClickListener(v -> botaoJuntar());
        limpar2.setOnClickListener(v -> { JL.clear(); mudou(); });
        add(c2, linhaBotoes(juntar, limpar2));
        status2 = texto("", 14, MUTED, false);
        add(c2, status2);
        dlJunto = botao("Baixar PDF", true);
        shareJunto = botao("📤 Enviar no WhatsApp", true);
        estiloZap(shareJunto);
        dlJunto.setOnClickListener(v -> { if (juntoFile != null) salvar(juntoFile, status2); });
        shareJunto.setOnClickListener(v -> { if (juntoFile != null) enviarWhatsApp(juntoFile); });
        saida = linhaBotoes(dlJunto, shareJunto);
        saida.setVisibility(View.GONE);
        add(c2, saida);
        add(painelJunta, c2);
        add(raiz, painelJunta);

        setContentView(scroll);
        abrirAba(true);
        atualizarBotoes();
        atualizarJunta();
    }

    private void abrirAba(boolean conv) {
        painelConv.setVisibility(conv ? View.VISIBLE : View.GONE);
        painelJunta.setVisibility(conv ? View.GONE : View.VISIBLE);
        for (Button t : new Button[]{tabConv, tabJunta}) {
            boolean sel = (t == tabConv) == conv;
            t.setTextColor(sel ? ACCENT_INK : MUTED);
            t.setBackground(caixa(sel ? ACCENT : SURFACE, sel ? ACCENT : SURFACE, 8, 0, false));
        }
        if (!conv) atualizarJunta();
    }

    private void setStatus(String t, boolean err) { status.setText(t); status.setTextColor(err ? ERR : MUTED); }
    private void setStatus2(String t, boolean err) { status2.setText(t); status2.setTextColor(err ? ERR : MUTED); }
    private void setStatusUi(final String t, final boolean err) { ui.post(() -> setStatus(t, err)); }
    private void setStatus2Ui(final String t, final boolean err) { ui.post(() -> setStatus2(t, err)); }

    private static String nomePdf(String n) { return n.replaceAll("(?i)\\.(xlsm|xlsx)$", "") + ".pdf"; }

    private void listaVazia() {
        lista.removeAllViews();
        TextView e = texto("Nenhuma planilha ainda.", 14, MUTED, false);
        e.setGravity(Gravity.CENTER);
        e.setPadding(dp(10), dp(10), dp(10), dp(10));
        add(lista, e);
    }

    private void atualizarBotoes() {
        int ok = 0;
        for (Item i : ITENS) if (i.pdf != null) ok++;
        habilitar(dlAll, ok > 0 && !OCUPADO);
        dlAll.setText(ok > 1 ? "Juntar os " + ok + " em um PDF" : "Juntar todos em um PDF");
        habilitar(clear, !ITENS.isEmpty());
        shareAll.setVisibility(ok > 0 ? View.VISIBLE : View.GONE);
    }

    /* ================= escolher arquivos ================= */
    private void abrirSeletor(int req, boolean varios, String[] tipos) {
        if (OCUPADO) { setStatus("Espere terminar o que está em andamento.", true); return; }
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType(tipos.length == 1 ? tipos[0] : "*/*");
        if (tipos.length > 1) i.putExtra(Intent.EXTRA_MIME_TYPES, tipos);
        i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, varios);
        try { startActivityForResult(i, req); }
        catch (ActivityNotFoundException e) { setStatus("Este celular não tem o seletor de arquivos.", true); }
    }

    private void abrirPasta(int req) {
        if (OCUPADO) { setStatus("Espere terminar o que está em andamento.", true); return; }
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try { startActivityForResult(i, req); }
        catch (ActivityNotFoundException e) { setStatus("Este celular não deixa escolher pastas.", true); }
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req == REQ_SALVAR) {
            byte[] b = pendenteSalvar;
            pendenteSalvar = null;
            if (res != RESULT_OK || data == null || data.getData() == null || b == null) { setStatus2("Cancelado.", false); return; }
            final Uri u = data.getData();
            fundo.execute(() -> {
                try (OutputStream o = getContentResolver().openOutputStream(u)) {
                    if (o == null) throw new IOException("sem acesso");
                    o.write(b);
                    setStatus2Ui("Baixado.", false);
                } catch (Exception e) { setStatus2Ui("Não deu para salvar: " + msg(e), true); }
            });
            return;
        }
        if (res != RESULT_OK || data == null) return;
        if (req == REQ_PASTA_UMA || req == REQ_PASTA_GUARDAR || req == REQ_PASTA_E_FAZER) {
            Uri arv = data.getData();
            if (arv == null) return;
            if (req == REQ_PASTA_UMA) {
                rodarComJanela(job -> {
                    List<Fonte> fs = listarPasta(arv);
                    processarXlsm(fs, nomeDaPasta(arv), job);
                }, "Fazendo tudo…", 4, false);
                return;
            }
            try { getContentResolver().takePersistableUriPermission(arv, Intent.FLAG_GRANT_READ_URI_PERMISSION); }
            catch (SecurityException e) { /* segue só nesta vez */ }
            prefs.edit().putString("pasta", arv.toString()).putString("pastaNome", nomeDaPasta(arv)).apply();
            mostrarPasta();
            if (req == REQ_PASTA_E_FAZER) fazerTudo();
            return;
        }
        List<Uri> uris = new ArrayList<>();
        if (data.getClipData() != null) {
            ClipData cd = data.getClipData();
            for (int i = 0; i < cd.getItemCount(); i++) uris.add(cd.getItemAt(i).getUri());
        } else if (data.getData() != null) uris.add(data.getData());
        if (uris.isEmpty()) return;
        final List<Fonte> fs = new ArrayList<>();
        for (Uri u : uris) fs.add(new Fonte(u, nomeDe(u)));
        if (req == REQ_PLANILHAS) {
            if (OCUPADO) return;
            travar(true);
            fundo.execute(() -> {
                try { receber(fs, null); } catch (Exception e) { /* nada */ }
                ui.post(() -> travar(false));
            });
        } else if (req == REQ_XLSM_ZAP) {
            rodarComJanela(job -> processarXlsm(fs, null, job), "Fazendo tudo…", 4, false);
        } else if (req == REQ_PDFS) {
            addPdfs(fs);
        }
    }

    private void receberIntent(Intent i) {
        if (i == null) return;
        String a = i.getAction();
        List<Uri> uris = new ArrayList<>();
        if (Intent.ACTION_SEND.equals(a)) {
            Uri u = i.getParcelableExtra(Intent.EXTRA_STREAM);
            if (u != null) uris.add(u);
        } else if (Intent.ACTION_SEND_MULTIPLE.equals(a)) {
            ArrayList<Uri> us = i.getParcelableArrayListExtra(Intent.EXTRA_STREAM);
            if (us != null) uris.addAll(us);
        } else if (Intent.ACTION_VIEW.equals(a) && i.getData() != null) {
            uris.add(i.getData());
        }
        if (uris.isEmpty()) return;
        setIntent(new Intent());
        final List<Fonte> planilhas = new ArrayList<>(), pdfs = new ArrayList<>();
        for (Uri u : uris) {
            String n = nomeDe(u);
            if (n.toLowerCase(Locale.ROOT).endsWith(".pdf")) pdfs.add(new Fonte(u, n));
            else planilhas.add(new Fonte(u, n));
        }
        if (!pdfs.isEmpty()) { addPdfs(pdfs); abrirAba(false); }
        if (!planilhas.isEmpty() && !OCUPADO) {
            abrirAba(true);
            travar(true);
            fundo.execute(() -> {
                try { receber(planilhas, null); } catch (Exception e) { /* nada */ }
                ui.post(() -> travar(false));
            });
        }
    }

    private String nomeDe(Uri u) {
        String n = null;
        try (Cursor c = getContentResolver().query(u, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) n = c.getString(0);
        } catch (Exception e) { /* usa o fim do caminho */ }
        if (n == null) { n = u.getLastPathSegment(); if (n == null) n = "arquivo"; int k = n.lastIndexOf('/'); if (k >= 0) n = n.substring(k + 1); }
        return n;
    }

    private String nomeDaPasta(Uri arv) {
        try {
            String id = DocumentsContract.getTreeDocumentId(arv);
            int k = Math.max(id.lastIndexOf('/'), id.lastIndexOf(':'));
            String n = k >= 0 ? id.substring(k + 1) : id;
            return n.isEmpty() ? "Pasta" : n;
        } catch (Exception e) { return "Pasta"; }
    }

    private List<Fonte> listarPasta(Uri arv) {
        List<Fonte> out = new ArrayList<>();
        Uri filhos = DocumentsContract.buildChildDocumentsUriUsingTree(arv, DocumentsContract.getTreeDocumentId(arv));
        try (Cursor c = getContentResolver().query(filhos, new String[]{
                DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE}, null, null, null)) {
            while (c != null && c.moveToNext()) {
                String id = c.getString(0), nome = c.getString(1), tipo = c.getString(2);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(tipo) || nome == null) continue;
                out.add(new Fonte(DocumentsContract.buildDocumentUriUsingTree(arv, id), nome));
            }
        }
        return out;
    }

    private byte[] lerTudo(Uri u) throws IOException {
        try (InputStream in = getContentResolver().openInputStream(u)) {
            if (in == null) throw new IOException("não deu para abrir o arquivo");
            return lerStream(in);
        }
    }

    private static byte[] lerStream(InputStream in) throws IOException {
        ByteArrayOutputStream bo = new ByteArrayOutputStream();
        byte[] buf = new byte[65536];
        int n;
        while ((n = in.read(buf)) > 0) bo.write(buf, 0, n);
        return bo.toByteArray();
    }

    private static byte[] lerArquivo(File f) throws IOException {
        try (FileInputStream in = new FileInputStream(f)) { return lerStream(in); }
    }

    private static void gravar(File f, byte[] b) throws IOException {
        File d = f.getParentFile();
        if (d != null && !d.exists() && !d.mkdirs()) throw new IOException("sem espaço no celular");
        try (FileOutputStream o = new FileOutputStream(f)) { o.write(b); }
    }

    private static String msg(Throwable e) {
        String m = e.getMessage();
        return m == null || m.isEmpty() ? e.getClass().getSimpleName() : m;
    }

    private static String limparNome(String s) { return s.replaceAll("[\\\\/:*?\"<>|]+", "_"); }

    private File novaPasta(String tipo) {
        File d = new File(getCacheDir(), tipo + "/" + System.nanoTime());
        //noinspection ResultOfMethodCallIgnored
        d.mkdirs();
        return d;
    }

    private void limparCacheAntigo() {
        fundo.execute(() -> { apagar(new File(getCacheDir(), "pdfs")); apagar(new File(getCacheDir(), "juntos")); });
    }

    private static void apagar(File f) {
        File[] fs = f.listFiles();
        if (fs != null) for (File x : fs) apagar(x);
        //noinspection ResultOfMethodCallIgnored
        f.delete();
    }

    /* ================= converter ================= */
    private void converter(Fonte fonte) {
        final Item it = new Item();
        it.nome = fonte.nome;
        aguardarUi(() -> {
            ITENS.add(it);
            if (lista.getChildCount() == 1 && lista.getChildAt(0) instanceof TextView) lista.removeAllViews();
            add(lista, itemUI(it));
        });
        try {
            if (!fonte.nome.toLowerCase(Locale.ROOT).matches(".*\\.(xlsm|xlsx)$")) throw new RuntimeException("não é .xlsm nem .xlsx");
            byte[] b = lerTudo(fonte.uri);
            int[] pg = new int[1];
            byte[] pdf = Motor.converter(b, fonte.nome.replaceAll("(?i)\\.(xlsm|xlsx)$", ""), optFit.isChecked(), optAll.isChecked(), pg);
            File f = new File(novaPasta("pdfs"), limparNome(nomePdf(fonte.nome)));
            gravar(f, pdf);
            it.pdf = f;
            it.paginas = pg[0];
            final Bitmap prev = previa(f, dp(96));
            ui.post(() -> {
                if (prev != null) it.img.setImageBitmap(prev);
                it.meta.setText(it.paginas + " página" + (it.paginas > 1 ? "s" : "") + " · " + (f.length() / 1024) + " KB");
                Button b1 = botao("Baixar PDF", true);
                b1.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
                b1.setOnClickListener(v -> salvar(f, it.meta));
                Button b2 = botao("Compartilhar", false);
                b2.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
                b2.setOnClickListener(v -> compartilhar(Collections.singletonList(f)));
                LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
                lp.rightMargin = dp(8);
                it.acts.addView(b1, lp);
                it.acts.addView(b2, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
                atualizarBotoes();
            });
        } catch (Throwable e) {
            it.erro = msg(e);
            ui.post(() -> {
                it.meta.setTextColor(ERR);
                it.meta.setText("Não deu para converter: " + it.erro + ".");
                atualizarBotoes();
            });
        }
    }

    private View itemUI(Item it) {
        LinearLayout li = new LinearLayout(this);
        li.setOrientation(LinearLayout.HORIZONTAL);
        li.setPadding(dp(10), dp(10), dp(10), dp(10));
        li.setBackground(caixa(SURFACE, LINE, 10, 1, false));
        ImageView img = new ImageView(this);
        img.setBackground(caixa(Color.WHITE, LINE, 4, 1, false));
        img.setAdjustViewBounds(true);
        img.setScaleType(ImageView.ScaleType.FIT_START);
        img.setContentDescription("Prévia da primeira página de " + it.nome);
        img.setImageDrawable(new ColorDrawable(0xFFEEEEEE));
        LinearLayout.LayoutParams ip = new LinearLayout.LayoutParams(dp(96), dp(136));
        ip.rightMargin = dp(12);
        li.addView(img, ip);
        LinearLayout info = vertical(2);
        TextView nm = texto(nomePdf(it.nome), 15, INK, true);
        add(info, nm);
        TextView meta = texto("Convertendo…", 13, MUTED, false);
        add(info, meta);
        LinearLayout acts = new LinearLayout(this);
        acts.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams ap = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        ap.topMargin = dp(6);
        info.addView(acts, ap);
        li.addView(info, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        it.linha = li; it.img = img; it.meta = meta; it.acts = acts;
        return li;
    }

    /** primeira página do PDF como imagem (usa o leitor de PDF do próprio Android) */
    private Bitmap previa(File f, int largura) {
        try (ParcelFileDescriptor fd = ParcelFileDescriptor.open(f, ParcelFileDescriptor.MODE_READ_ONLY);
             PdfRenderer r = new PdfRenderer(fd)) {
            if (r.getPageCount() == 0) return null;
            try (PdfRenderer.Page p = r.openPage(0)) {
                int w = Math.max(64, largura * 2);
                int h = Math.max(1, Math.round(w * (float) p.getHeight() / p.getWidth()));
                Bitmap bm = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
                bm.eraseColor(Color.WHITE);
                p.render(bm, null, null, PdfRenderer.Page.RENDER_MODE_FOR_DISPLAY);
                return bm;
            }
        } catch (Throwable e) { return null; }
    }

    /** roda no fundo: converte uma lista de planilhas */
    private void receber(List<Fonte> fs, Job job) throws Cancelado {
        if (fs.isEmpty()) return;
        int n = fs.size();
        setStatusUi("Convertendo " + n + " arquivo" + (n > 1 ? "s" : "") + "…", false);
        for (int k = 0; k < n; k++) {
            cancelado(job);
            if (job != null) {
                job.conta(k + 1, n, "Convertendo os arquivos em PDF");
                job.prog(8 + 62.0 * k / n, "Agora: " + fs.get(k).nome + ". Cada planilha .xlsm vira um PDF.", 2);
            }
            converter(fs.get(k));
        }
        cancelado(job);
        int ok = contarOk();
        if (job != null) {
            job.conta(ok, n, "Arquivos convertidos em PDF");
            job.prog(70, ok < n ? (n - ok) + " não deu para converter (veja na lista)." : "Todos convertidos. Agora vou juntar.", 2);
        }
        final int total = ITENS.size();
        setStatusUi(ok + " de " + total + " convertido" + (total > 1 ? "s" : "") + ".", false);
    }

    private int contarOk() {
        int ok = 0;
        for (Item i : ITENS) if (i.pdf != null) ok++;
        return ok;
    }

    private void limparLista() {
        ITENS.clear();
        listaVazia();
        setStatus("", false);
        atualizarBotoes();
    }

    private void juntarTodosConvertidos() {
        JL.clear();
        for (Item i : ITENS) if (i.pdf != null) JL.add(new Pdf(nomePdf(i.nome), i.pdf, null, i.pdf.length()));
        if (JL.isEmpty()) return;
        nomePadrao(null);
        abrirAba(false);
        mudou();
        if (JL.size() >= 2) botaoJuntar();
        else setStatus2("Só tem 1 PDF convertido: adicione outro PDF aqui para juntar, ou baixe ele direto na outra aba.", false);
    }

    private void compartilharTodos() {
        List<File> fs = new ArrayList<>();
        for (Item i : ITENS) if (i.pdf != null) fs.add(i.pdf);
        if (!fs.isEmpty()) compartilhar(fs);
    }

    /* ================= aba Juntar ================= */
    private void addPdfs(List<Fonte> fs) {
        for (Fonte f : fs) {
            if (!f.nome.toLowerCase(Locale.ROOT).endsWith(".pdf")) continue;
            long tam = -1;
            try (Cursor c = getContentResolver().query(f.uri, new String[]{OpenableColumns.SIZE}, null, null, null)) {
                if (c != null && c.moveToFirst() && !c.isNull(0)) tam = c.getLong(0);
            } catch (Exception e) { /* tamanho desconhecido */ }
            JL.add(new Pdf(f.nome, null, f.uri, tam));
        }
        mudou();
    }

    private void mudou() {
        juntoFile = null;
        saida.setVisibility(View.GONE);
        setStatus2(JL.size() == 1 ? "Adicione pelo menos mais um PDF." : "", false);
        atualizarJunta();
    }

    private void atualizarJunta() {
        plist.removeAllViews();
        for (int i = 0; i < JL.size(); i++) {
            final int idx = i;
            Pdf p = JL.get(i);
            LinearLayout li = new LinearLayout(this);
            li.setOrientation(LinearLayout.HORIZONTAL);
            li.setGravity(Gravity.CENTER_VERTICAL);
            li.setPadding(dp(10), dp(8), dp(8), dp(8));
            li.setBackground(caixa(SURFACE, LINE, 8, 1, false));
            TextView num = texto(String.valueOf(i + 1), 18, ACCENT, false);
            num.setTypeface(display());
            num.setGravity(Gravity.CENTER);
            li.addView(num, new LinearLayout.LayoutParams(dp(28), ViewGroup.LayoutParams.WRAP_CONTENT));
            LinearLayout info = vertical(0);
            add(info, texto(p.nome, 14, INK, true));
            add(info, texto(p.tam >= 0 ? (p.tam / 1024) + " KB" : "", 12, MUTED, false));
            LinearLayout.LayoutParams ilp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
            ilp.leftMargin = dp(8);
            li.addView(info, ilp);
            li.addView(setinha("↑", "Subir " + p.nome, idx > 0, v -> { Collections.swap(JL, idx, idx - 1); mudou(); }));
            li.addView(setinha("↓", "Descer " + p.nome, idx < JL.size() - 1, v -> { Collections.swap(JL, idx, idx + 1); mudou(); }));
            li.addView(setinha("✕", "Tirar " + p.nome, true, v -> { JL.remove(idx); mudou(); }));
            add(plist, li);
        }
        habilitar(juntar, JL.size() >= 2 && !OCUPADO);
        habilitar(limpar2, !JL.isEmpty());
        usarConv.setVisibility(contarOk() > 0 ? View.VISIBLE : View.GONE);
    }

    private Button setinha(String t, String desc, boolean on, View.OnClickListener l) {
        Button b = botao(t, false);
        b.setContentDescription(desc);
        b.setPadding(0, 0, 0, 0);
        b.setMinWidth(dp(40));
        b.setMinimumWidth(dp(40));
        b.setLayoutParams(new LinearLayout.LayoutParams(dp(40), dp(40)));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(40), dp(40));
        lp.leftMargin = dp(4);
        b.setLayoutParams(lp);
        habilitar(b, on);
        b.setOnClickListener(l);
        return b;
    }

    private String nomeFinal() {
        String s = nomeSaida.getText().toString().trim();
        if (s.isEmpty()) s = "PDFs_juntos";
        s = limparNome(s).replaceAll("(?i)\\.pdf$", "");
        if (s.isEmpty()) s = "PDFs_juntos";
        return s + ".pdf";
    }

    private void nomePadrao(String pasta) {
        if (nomeMexeu) return;
        Calendar c = Calendar.getInstance();
        String base = pasta == null || pasta.isEmpty() ? "PDFs_juntos" : limparNome(pasta);
        String v = String.format(Locale.ROOT, "%s_%02d-%02d-%d", base, c.get(Calendar.DAY_OF_MONTH), c.get(Calendar.MONTH) + 1, c.get(Calendar.YEAR));
        mudandoNome = true;
        nomeSaida.setText(v);
        mudandoNome = false;
    }

    private void botaoJuntar() {
        rodarComJanela(job -> {
            boolean ok = juntarCore(job, 5, 90, 2, 1);
            cancelado(job);
            if (!ok) return;
            job.prog(100, null, 3);
            mostrarEnvio(job);
        }, "Juntando PDFs…", 3, true);
    }

    /** roda no fundo: junta os PDFs da lista. Devolve true se deu certo. */
    private boolean juntarCore(final Job job, final double de, final double ate, final int etapa, final int etapaLer) throws Cancelado {
        final List<Pdf> lista = new ArrayList<>(JL);
        if (lista.size() < 2) return false;
        final String nome = aguardarUiValor(this::nomeFinal);
        ui.post(() -> { saida.setVisibility(View.GONE); setStatus2("Juntando…", false); });
        final int[] atual = {0};
        try {
            if (job != null) job.prog(de, "Preparando " + lista.size() + " PDFs para juntar…", etapaLer > 0 ? etapaLer : etapa);
            byte[][] bufs = new byte[lista.size()][];
            for (int k = 0; k < lista.size(); k++) {
                cancelado(job);
                atual[0] = k;
                if (job != null && etapaLer > 0) { job.conta(k + 1, lista.size(), "Lendo os arquivos PDF"); job.prog(-1, "Agora: " + lista.get(k).nome, 0); }
                Pdf p = lista.get(k);
                bufs[k] = p.arq != null ? lerArquivo(p.arq) : lerTudo(p.uri);
            }
            cancelado(job);
            if (job != null) { job.conta(1, lista.size(), "Juntando os arquivos em um PDF só"); job.prog(de + 2, "Agora: " + lista.get(0).nome + ". As páginas ficam na ordem da lista.", etapa); }
            int[] pg = new int[1];
            atual[0] = 0;
            byte[] r = Motor.juntar(bufs, pg, (k, n) -> {
                if (job != null && job.cancel) return false;
                atual[0] = Math.min(k, n - 1);
                setStatus2Ui("Juntando… " + k + " de " + n, false);
                if (job != null) {
                    if (k < n) { job.conta(k + 1, n, "Juntando os arquivos em um PDF só"); job.prog(de + 2 + (ate - de - 2) * k / n, "Agora: " + lista.get(k).nome + ". As páginas ficam na ordem da lista.", etapa); }
                    else { job.conta(n, n, "Arquivos juntados em um PDF só"); job.prog(ate, "Finalizando o PDF…", etapa); }
                }
                return true;
            });
            cancelado(job);
            File f = new File(novaPasta("juntos"), nome);
            gravar(f, r);
            final long kb = f.length() / 1024;
            final int paginas = pg[0];
            aguardarUi(() -> {
                juntoFile = f;
                setStatus2("Pronto: " + lista.size() + " PDFs, " + paginas + " página" + (paginas > 1 ? "s" : "") + " no total (" + kb + " KB).", false);
                saida.setVisibility(View.VISIBLE);
            });
            return true;
        } catch (Cancelado c) {
            throw c;
        } catch (Throwable e) {
            if (job != null && job.cancel) throw new Cancelado();
            String qual = atual[0] < lista.size() ? lista.get(atual[0]).nome : null;
            setStatus2Ui("Não deu para juntar" + (qual != null ? " (" + qual + ")" : "") + ": " + msg(e) + ".", true);
            return false;
        }
    }

    /* ================= botão automático ================= */
    private void fazerTudo() {
        String s = prefs.getString("pasta", null);
        if (s == null) { abrirPasta(REQ_PASTA_E_FAZER); return; }
        final Uri arv = Uri.parse(s);
        if (!temPermissao(arv)) {
            setStatus("Sem permissão para ler a pasta. Escolha a pasta de novo.", true);
            abrirPasta(REQ_PASTA_E_FAZER);
            return;
        }
        final String nome = prefs.getString("pastaNome", "Pasta");
        rodarComJanela(job -> {
            job.prog(2, "Abrindo a pasta…", 1);
            setStatusUi("Procurando .xlsm em “" + nome + "”…", false);
            job.prog(5, "Procurando .xlsm em “" + nome + "”…", 1);
            List<Fonte> fs;
            try { fs = listarPasta(arv); }
            catch (Exception e) { setStatusUi("Não deu para ler a pasta: " + msg(e) + ". Escolha a pasta de novo.", true); return; }
            cancelado(job);
            int nx = 0;
            for (Fonte f : fs) if (ehXlsm(f.nome)) nx++;
            if (nx == 0) { setStatusUi("Nenhum .xlsm na pasta “" + nome + "”.", true); return; }
            job.conta(nx, nx, "Arquivo" + (nx > 1 ? "s" : "") + " .xlsm encontrado" + (nx > 1 ? "s" : "") + " na pasta");
            job.prog(8, "Agora vou converter cada um em PDF.", 1);
            processarXlsm(fs, nome, job);
        }, "Fazendo tudo…", 4, false);
    }

    private boolean temPermissao(Uri arv) {
        for (UriPermission p : getContentResolver().getPersistedUriPermissions())
            if (p.getUri().equals(arv) && p.isReadPermission()) return true;
        return false;
    }

    private void mostrarPasta() {
        String n = prefs.getString("pastaNome", null);
        pastaInfo.setText(n != null ? "Pasta guardada: " + n + ". Toque em “Fazer tudo”." : "Nenhuma pasta guardada ainda. Toque em “Escolher / trocar a pasta”.");
    }

    private static boolean ehXlsm(String n) { return n.toLowerCase(Locale.ROOT).endsWith(".xlsm") && !n.startsWith("~$"); }

    /** roda no fundo: .xlsm -> PDFs -> junta -> janela com Enviar no WhatsApp */
    private void processarXlsm(List<Fonte> todos, String nomePasta, Job job) throws Cancelado {
        List<Fonte> xs = new ArrayList<>();
        for (Fonte f : todos) if (ehXlsm(f.nome)) xs.add(f);
        final Collator col = Collator.getInstance(new Locale("pt", "BR"));
        Collections.sort(xs, (a, b) -> compararNatural(a.nome, b.nome, col));
        if (xs.isEmpty()) { setStatusUi("Nenhum arquivo .xlsm escolhido.", true); return; }
        aguardarUi(this::limparLista);
        receber(xs, job);
        final List<Pdf> ok = new ArrayList<>();
        for (Item i : ITENS) if (i.pdf != null) ok.add(new Pdf(nomePdf(i.nome), i.pdf, null, i.pdf.length()));
        if (ok.isEmpty()) { setStatusUi("Nenhuma planilha foi convertida.", true); return; }
        aguardarUi(() -> {
            JL.clear();
            JL.addAll(ok);
            nomePadrao(nomePasta != null ? nomePasta : "PDFs_juntos");
            abrirAba(false);
            mudou();
        });
        if (JL.size() >= 2) {
            if (!juntarCore(job, 72, 92, 3, 0)) return;
            cancelado(job);
        } else {
            aguardarUi(() -> {
                juntoFile = JL.get(0).arq;
                saida.setVisibility(View.VISIBLE);
                setStatus2("Só 1 planilha: PDF pronto.", false);
            });
        }
        if (job != null) { job.prog(100, null, 4); mostrarEnvio(job); }
    }

    /** compara "arquivo 2" antes de "arquivo 10" */
    private static int compararNatural(String a, String b, Collator col) {
        int i = 0, j = 0;
        while (i < a.length() && j < b.length()) {
            char ca = a.charAt(i), cb = b.charAt(j);
            if (Character.isDigit(ca) && Character.isDigit(cb)) {
                int si = i, sj = j;
                while (i < a.length() && Character.isDigit(a.charAt(i))) i++;
                while (j < b.length() && Character.isDigit(b.charAt(j))) j++;
                String na = a.substring(si, i).replaceFirst("^0+(?=.)", ""), nb = b.substring(sj, j).replaceFirst("^0+(?=.)", "");
                if (na.length() != nb.length()) return na.length() - nb.length();
                int c = na.compareTo(nb);
                if (c != 0) return c;
            } else {
                int si = i, sj = j;
                while (i < a.length() && !Character.isDigit(a.charAt(i))) i++;
                while (j < b.length() && !Character.isDigit(b.charAt(j))) j++;
                int c = col.compare(a.substring(si, i), b.substring(sj, j));
                if (c != 0) return c;
            }
        }
        return (a.length() - i) - (b.length() - j);
    }

    /* ================= salvar e enviar ================= */
    private void salvar(final File f, final TextView alvo) {
        final String nome = f.getName();
        if (Build.VERSION.SDK_INT >= 29) {
            fundo.execute(() -> {
                try {
                    ContentResolver cr = getContentResolver();
                    ContentValues cv = new ContentValues();
                    cv.put(MediaStore.MediaColumns.DISPLAY_NAME, nome);
                    cv.put(MediaStore.MediaColumns.MIME_TYPE, "application/pdf");
                    cv.put(MediaStore.MediaColumns.RELATIVE_PATH, "Download/XLSM para PDF");
                    Uri u = cr.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, cv);
                    if (u == null) throw new IOException("sem acesso aos Downloads");
                    try (OutputStream o = cr.openOutputStream(u)) {
                        if (o == null) throw new IOException("sem acesso aos Downloads");
                        o.write(lerArquivo(f));
                    }
                    ui.post(() -> alvo.setText("Baixado em Downloads › XLSM para PDF."));
                } catch (Exception e) {
                    ui.post(() -> alvo.setText("Não deu para baixar aqui: " + msg(e)));
                }
            });
        } else {
            try { pendenteSalvar = lerArquivo(f); } catch (IOException e) { alvo.setText("Não deu para baixar: " + msg(e)); return; }
            Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("application/pdf");
            i.putExtra(Intent.EXTRA_TITLE, nome);
            try { startActivityForResult(i, REQ_SALVAR); }
            catch (ActivityNotFoundException e) { alvo.setText("Este celular não deixa escolher onde salvar."); }
        }
    }

    private Uri uriDe(File f) { return ArquivosProvider.uriPara(getCacheDir(), f); }

    private void compartilhar(List<File> fs) {
        Intent i;
        ArrayList<Uri> us = new ArrayList<>();
        for (File f : fs) us.add(uriDe(f));
        if (us.size() == 1) {
            i = new Intent(Intent.ACTION_SEND);
            i.putExtra(Intent.EXTRA_STREAM, us.get(0));
        } else {
            i = new Intent(Intent.ACTION_SEND_MULTIPLE);
            i.putParcelableArrayListExtra(Intent.EXTRA_STREAM, us);
        }
        i.setType("application/pdf");
        ClipData cd = ClipData.newRawUri(fs.get(0).getName(), us.get(0));
        for (int k = 1; k < us.size(); k++) cd.addItem(new ClipData.Item(us.get(k)));
        i.setClipData(cd);
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try { startActivity(Intent.createChooser(i, "Compartilhar PDF")); }
        catch (ActivityNotFoundException e) { setStatus("Nenhum app para compartilhar.", true); }
    }

    /** abre o WhatsApp direto; se não tiver, abre a lista de apps */
    private boolean enviarWhatsApp(File f) {
        Uri u = uriDe(f);
        for (String pkg : new String[]{"com.whatsapp", "com.whatsapp.w4b"}) {
            Intent i = new Intent(Intent.ACTION_SEND);
            i.setType("application/pdf");
            i.putExtra(Intent.EXTRA_STREAM, u);
            i.setClipData(ClipData.newRawUri(f.getName(), u));
            i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            i.setPackage(pkg);
            try { startActivity(i); return true; } catch (ActivityNotFoundException e) { /* tenta o próximo */ }
        }
        compartilhar(Collections.singletonList(f));
        return true;
    }

    /* ================= janela de progresso com Cancelar ================= */
    static final class Cancelado extends Exception { Cancelado() { super("cancelado"); } }

    interface Tarefa { void rodar(Job job) throws Exception; }

    final class Job {
        volatile boolean cancel = false;
        boolean statusNaJunta;

        void prog(final double p, final String t, final int etapa) {
            ui.post(() -> { if (JOB == this) jobProg(p, t, etapa); });
        }

        void conta(final int k, final int n, final String fase) {
            ui.post(() -> { if (JOB == this) jobConta(k, n, fase); });
        }
    }

    private void cancelado(Job job) throws Cancelado { if (job != null && job.cancel) throw new Cancelado(); }

    private void montarJanela() {
        modal = new Dialog(this);
        modal.requestWindowFeature(Window.FEATURE_NO_TITLE);
        modal.setCancelable(false);
        LinearLayout box = vertical(12);
        box.setPadding(dp(20), dp(20), dp(20), dp(20));
        box.setBackground(caixa(SURFACE, LINE, 12, 1, false));
        jobTit = titulo("Fazendo tudo…", 24);
        add(box, jobTit);
        LinearLayout conta = vertical(2);
        conta.setGravity(Gravity.CENTER_HORIZONTAL);
        conta.setPadding(dp(10), dp(10), dp(10), dp(10));
        conta.setBackground(caixa(STRIPE, STRIPE, 10, 0, false));
        jobNum = texto("0 de 0", 40, ACCENT, false);
        jobNum.setTypeface(display());
        jobNum.setGravity(Gravity.CENTER);
        jobFase = texto("", 15, INK, true);
        jobFase.setGravity(Gravity.CENTER);
        add(conta, jobNum);
        add(conta, jobFase);
        jobConta = conta;
        add(box, conta);
        jobMsg = texto("Começando…", 14, MUTED, false);
        jobMsg.setMinLines(2);
        add(box, jobMsg);
        jobBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        jobBar.setMax(100);
        jobBar.setIndeterminate(false);
        if (Build.VERSION.SDK_INT >= 21) {
            jobBar.setProgressTintList(android.content.res.ColorStateList.valueOf(ACCENT));
            jobBar.setProgressBackgroundTintList(android.content.res.ColorStateList.valueOf(LINE));
        }
        LinearLayout.LayoutParams bp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(14));
        bp.topMargin = dp(12);
        box.addView(jobBar, bp);
        LinearLayout pinfo = new LinearLayout(this);
        pinfo.setOrientation(LinearLayout.HORIZONTAL);
        jobEtapa = texto("Etapa 1 de 4", 13, MUTED, false);
        jobPct = texto("0%", 13, INK, true);
        jobPct.setGravity(Gravity.END);
        pinfo.addView(jobEtapa, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        pinfo.addView(jobPct, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        add(box, pinfo);
        jobEnviar = botao("📤 Enviar no WhatsApp", true);
        estiloZap(jobEnviar);
        jobEnviar.setVisibility(View.GONE);
        add(box, jobEnviar);
        jobCancel = botao("Cancelar", false);
        jobCancel.setOnClickListener(v -> cancelarJob());
        add(box, jobCancel);
        FrameLayout moldura = new FrameLayout(this);
        moldura.setPadding(dp(16), dp(16), dp(16), dp(16));
        moldura.addView(box, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        modal.setContentView(moldura);
        Window w = modal.getWindow();
        if (w != null) {
            w.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
            w.setLayout(Math.min(getResources().getDisplayMetrics().widthPixels, dp(412)), WindowManager.LayoutParams.WRAP_CONTENT);
            w.setDimAmount(0.55f);
        }
    }

    private void jobProg(double p, String t, int etapa) {
        if (p >= 0) {
            jobPctV = Math.max(jobPctV, Math.min(100, p));
            int v = (int) Math.round(jobPctV);
            jobBar.setProgress(v);
            jobPct.setText(v + "%");
        }
        if (t != null) jobMsg.setText(t);
        if (etapa > 0) jobEtapa.setText("Etapa " + etapa + " de " + jobTotal);
    }

    private void jobConta(int k, int n, String fase) {
        jobConta.setVisibility(View.VISIBLE);
        jobNum.setText(k + " de " + n);
        jobFase.setText(fase);
    }

    private void abrirJanela(String titulo, int total) {
        jobPctV = 0;
        jobTotal = total;
        jobTit.setText(titulo.toUpperCase(Locale.ROOT));
        jobCancel.setText("Cancelar");
        jobEnviar.setVisibility(View.GONE);
        jobConta.setVisibility(View.GONE);
        jobBar.setProgressTintList(android.content.res.ColorStateList.valueOf(ACCENT));
        jobBar.setProgress(0);
        jobPct.setText("0%");
        jobMsg.setText("Começando…");
        jobEtapa.setText("Etapa 1 de " + total);
        modal.show();
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    private void fecharJanela() {
        if (modal.isShowing()) modal.dismiss();
        getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    private void travar(boolean sim) {
        OCUPADO = sim;
        habilitar(fazerTudo, !sim);
        habilitar(definirPasta, !sim);
        habilitar(juntar, !sim && JL.size() >= 2);
        habilitar(dlAll, !sim && contarOk() > 0);
    }

    private void cancelarJob() {
        if (JOB == null) { fecharJanela(); return; }
        Job job = JOB;
        job.cancel = true;
        JOB = null;
        fecharJanela();
        if (job.statusNaJunta) setStatus2("Cancelado. Toque em “Juntar em um PDF” para começar de novo.", false);
        else setStatus("Cancelado. Toque em “Fazer tudo” para começar de novo.", false);
    }

    private void rodarComJanela(final Tarefa fn, String titulo, int total, boolean statusNaJunta) {
        if (OCUPADO) {
            if (statusNaJunta) setStatus2("Espere terminar o que está em andamento.", true);
            else setStatus("Espere terminar o que está em andamento.", true);
            return;
        }
        final Job job = new Job();
        job.statusNaJunta = statusNaJunta;
        JOB = job;
        travar(true);
        abrirJanela(titulo, total);
        fundo.execute(() -> {
            try {
                fn.rodar(job);
            } catch (Cancelado c) {
                // já avisado
            } catch (Throwable e) {
                final String m = "Não deu para terminar: " + msg(e) + ".";
                if (statusNaJunta) setStatus2Ui(m, true); else setStatusUi(m, true);
            } finally {
                ui.post(() -> {
                    if (JOB == job) { JOB = null; fecharJanela(); }
                    travar(false);
                    atualizarJunta();
                    atualizarBotoes();
                });
            }
        });
    }

    /** fim: a janela fica aberta com o botão de enviar */
    private void mostrarEnvio(final Job job) throws Cancelado {
        cancelado(job);
        aguardarUi(() -> {
            if (JOB != job) return;
            JOB = null; // daqui em diante o botão de baixo só fecha
            final File f = juntoFile;
            if (f == null) { fecharJanela(); return; }
            jobProg(100, null, jobTotal);
            jobConta(JL.size(), JL.size(), JL.size() > 1 ? "Arquivos juntados em 1 PDF" : "Arquivo pronto em PDF");
            jobBar.setProgressTintList(android.content.res.ColorStateList.valueOf(OK));
            jobTit.setText("TUDO PRONTO ✓");
            jobCancel.setText("Fechar");
            jobMsg.setText(f.getName() + " está pronto. Toque em Enviar no WhatsApp e escolha o contato.");
            jobEnviar.setVisibility(View.VISIBLE);
            jobEnviar.setText("📤 Enviar no WhatsApp");
            jobEnviar.setOnClickListener(v -> { fecharJanela(); enviarWhatsApp(f); setStatus2(status2.getText() + " Enviado para o WhatsApp.", false); });
            jobEnviar.requestFocus();
        });
    }

    /* ================= utilidades de thread ================= */
    /** roda na thread da tela e espera terminar */
    private void aguardarUi(final Runnable r) {
        if (Looper.myLooper() == Looper.getMainLooper()) { r.run(); return; }
        final Object trava = new Object();
        final boolean[] feito = {false};
        ui.post(() -> {
            try { r.run(); } finally { synchronized (trava) { feito[0] = true; trava.notifyAll(); } }
        });
        synchronized (trava) {
            while (!feito[0]) {
                try { trava.wait(); } catch (InterruptedException e) { Thread.currentThread().interrupt(); return; }
            }
        }
    }

    interface Valor<T> { T obter(); }

    private <T> T aguardarUiValor(final Valor<T> v) {
        final Object[] r = new Object[1];
        aguardarUi(() -> r[0] = v.obter());
        @SuppressWarnings("unchecked") T t = (T) r[0];
        return t;
    }
}
