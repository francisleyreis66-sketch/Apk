package com.projetocamaleao

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.content.pm.ResolveInfo
import android.graphics.drawable.Drawable
import android.os.Bundle
import android.provider.Settings
import android.text.TextUtils
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.*

class MainActivity : Activity() {
    private lateinit var box: LinearLayout
    private var current: String? = null

    override fun onCreate(b: Bundle?) {
        super.onCreate(b)
        box = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(24), dp(16), dp(16))
        }
        setContentView(ScrollView(this).apply { addView(box) })
    }

    override fun onResume() { super.onResume(); render() }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        if (current != null) { current = null; render() } else super.onBackPressed()
    }

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

    private fun enabled(): Boolean {
        val s = Settings.Secure.getString(contentResolver, Settings.Secure.ENABLED_ACCESSIBILITY_SERVICES) ?: ""
        return s.contains("$packageName/")
    }

    private fun btn(t: String, f: () -> Unit) = Button(this).apply { text = t; setOnClickListener { f() } }

    private fun icon(pkg: String): Drawable? =
        try { packageManager.getApplicationIcon(pkg) } catch (e: Exception) { null }

    private fun render() {
        box.removeAllViews()
        val apps = Store.load(this)
        val cur = apps.find { it.pkg == current }
        if (cur == null) { current = null; home(apps) } else appScreen(cur)
    }

    // ---------- Tela inicial: ícones dos apps ----------
    private fun home(apps: MutableList<AppItem>) {
        box.addView(ImageView(this).apply {
            setImageResource(R.drawable.logo); adjustViewBounds = true; maxHeight = dp(120)
        }, LinearLayout.LayoutParams(-1, -2))
        box.addView(TextView(this).apply {
            text = "Projeto Camaleão"; textSize = 24f; gravity = Gravity.CENTER
        }, LinearLayout.LayoutParams(-1, -2))
        if (!enabled()) {
            box.addView(TextView(this).apply {
                text = "Falta ativar o serviço de acessibilidade."; setPadding(0, dp(12), 0, 0)
            })
            box.addView(btn("Ativar acessibilidade") {
                startActivity(Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS))
            })
        }
        if (apps.isEmpty()) {
            box.addView(TextView(this).apply {
                text = "Toque em “+ Adicionar app” para escolher os apps que você usa."
                setPadding(0, dp(20), 0, dp(8))
            })
        }
        apps.chunked(3).forEach { rowApps ->
            val row = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL; setPadding(0, dp(16), 0, 0)
            }
            rowApps.forEach { row.addView(tile(it), LinearLayout.LayoutParams(0, -2, 1f)) }
            repeat(3 - rowApps.size) { row.addView(Space(this), LinearLayout.LayoutParams(0, 0, 1f)) }
            box.addView(row)
        }
        box.addView(Space(this), LinearLayout.LayoutParams(-1, dp(24)))
        box.addView(btn("+ Adicionar app") {
            pickApp { pkg, label ->
                val l = Store.load(this)
                if (l.none { it.pkg == pkg }) l.add(AppItem(pkg, label, mutableListOf()))
                Store.save(this, l); current = pkg; render()
            }
        })
    }

    private fun tile(a: AppItem): View = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER_HORIZONTAL
        setPadding(dp(4), dp(8), dp(4), dp(8))
        addView(ImageView(context).apply { setImageDrawable(icon(a.pkg)) }, LinearLayout.LayoutParams(dp(64), dp(64)))
        addView(TextView(context).apply {
            text = a.name; textSize = 13f; gravity = Gravity.CENTER
            maxLines = 1; ellipsize = TextUtils.TruncateAt.END
        }, LinearLayout.LayoutParams(-1, -2))
        setOnClickListener { current = a.pkg; render() }
        setOnLongClickListener {
            AlertDialog.Builder(context).setTitle("Remover ${a.name}?")
                .setMessage("As funções salvas desse app também serão apagadas.")
                .setPositiveButton("Remover") { _, _ ->
                    val l = Store.load(this@MainActivity); l.removeAll { it.pkg == a.pkg }
                    Store.save(this@MainActivity, l); render()
                }.setNegativeButton("Cancelar", null).show()
            true
        }
    }

    // ---------- Tela do app: botões das funções ----------
    private fun appScreen(a: AppItem) {
        val head = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        head.addView(ImageView(this).apply { setImageDrawable(icon(a.pkg)) }, LinearLayout.LayoutParams(dp(56), dp(56)))
        head.addView(TextView(this).apply { text = a.name; textSize = 22f; setPadding(dp(16), 0, 0, 0) })
        box.addView(head)
        box.addView(btn("Abrir app") {
            packageManager.getLaunchIntentForPackage(a.pkg)?.let { startActivity(it) }
        })
        a.macros.forEachIndexed { i, m ->
            val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
            row.addView(btn(m.name) { run(m) }, LinearLayout.LayoutParams(0, dp(64), 1f))
            row.addView(btn("✎") { edit(a.pkg, i) }, LinearLayout.LayoutParams(dp(64), dp(64)))
            box.addView(row)
        }
        box.addView(btn("+ Nova função") { edit(a.pkg, null) })
        box.addView(btn("← Voltar") { current = null; render() })
    }

    private fun run(m: Macro) {
        val s = CamaleaoService.instance
        if (s == null) {
            Toast.makeText(this, "Ative o serviço de acessibilidade primeiro", Toast.LENGTH_LONG).show()
            startActivity(Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS))
            return
        }
        s.run(m)
    }

    private fun pickApp(cb: (String, String) -> Unit) {
        val i = Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER)
        val apps = packageManager.queryIntentActivities(i, 0)
            .filter { it.activityInfo.packageName != packageName }
            .sortedBy { it.loadLabel(packageManager).toString().lowercase() }
        val adapter = object : ArrayAdapter<ResolveInfo>(this, 0, apps) {
            override fun getView(p: Int, v: View?, parent: ViewGroup): View {
                val ri = getItem(p)!!
                return LinearLayout(context).apply {
                    orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
                    setPadding(dp(16), dp(8), dp(16), dp(8))
                    addView(ImageView(context).apply { setImageDrawable(ri.loadIcon(packageManager)) },
                        LinearLayout.LayoutParams(dp(40), dp(40)))
                    addView(TextView(context).apply {
                        text = ri.loadLabel(packageManager); textSize = 16f; setPadding(dp(16), 0, 0, 0)
                    })
                }
            }
        }
        AlertDialog.Builder(this).setTitle("Escolha o app").setAdapter(adapter) { _, w ->
            val ri = apps[w]
            cb(ri.activityInfo.packageName, ri.loadLabel(packageManager).toString())
        }.show()
    }

    private fun edit(pkg: String, idx: Int?) {
        val all = Store.load(this)
        val ap = all.first { it.pkg == pkg }
        val m = if (idx != null) ap.macros[idx] else Macro("", pkg, listOf())
        val l = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(20), dp(8), dp(20), 0)
        }
        val name = EditText(this).apply { hint = "Nome do botão"; setText(m.name) }
        val steps = EditText(this).apply {
            hint = "Passos, um por linha (texto do botão na tela)\nex.:\nMenu\nConfigurações"
            setText(m.steps.joinToString("\n")); minLines = 4; gravity = Gravity.TOP
        }
        l.addView(name); l.addView(steps)
        val d = AlertDialog.Builder(this)
            .setTitle(if (idx == null) "Nova função" else "Editar função")
            .setView(ScrollView(this).apply { addView(l) })
            .setPositiveButton("Salvar") { _, _ ->
                val nm = Macro(
                    name.text.toString().ifBlank { "Função" }, pkg,
                    steps.text.toString().lines().map { it.trim() }.filter { it.isNotEmpty() }
                )
                if (idx == null) ap.macros.add(nm) else ap.macros[idx] = nm
                Store.save(this, all); render()
            }
            .setNegativeButton("Cancelar", null)
        if (idx != null) d.setNeutralButton("Apagar") { _, _ ->
            ap.macros.removeAt(idx); Store.save(this, all); render()
        }
        d.show()
    }
}
