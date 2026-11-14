package com.projetocamaleao

import android.accessibilityservice.AccessibilityService
import android.content.Intent
import android.os.Handler
import android.os.Looper
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.Toast

class CamaleaoService : AccessibilityService() {
    companion object { var instance: CamaleaoService? = null }

    private val handler = Handler(Looper.getMainLooper())
    private var runId = 0

    override fun onServiceConnected() { instance = this }
    override fun onUnbind(intent: Intent?): Boolean { instance = null; return super.onUnbind(intent) }
    override fun onAccessibilityEvent(e: AccessibilityEvent?) {}
    override fun onInterrupt() {}

    fun run(m: Macro) {
        val launch = packageManager.getLaunchIntentForPackage(m.pkg)
        if (launch == null) { toast("App não encontrado: ${m.pkg}"); return }
        launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        startActivity(launch)
        runId++
        step(m, 0, 0, runId)
    }

    private fun step(m: Macro, i: Int, tries: Int, id: Int) {
        if (id != runId) return
        if (i >= m.steps.size) { toast("Pronto!"); return }
        val delay = if (i == 0 && tries == 0) 2500L else 700L
        handler.postDelayed({
            if (id != runId) return@postDelayed
            val root = rootInActiveWindow
            val ok = root != null && root.packageName?.toString() == m.pkg &&
                findAndClick(root, m.steps[i])
            when {
                ok -> step(m, i + 1, 0, id)
                tries < 20 -> step(m, i, tries + 1, id)
                else -> toast("Não achei na tela: ${m.steps[i]}")
            }
        }, delay)
    }

    private fun findAndClick(root: AccessibilityNodeInfo, label: String): Boolean {
        val nodes = root.findAccessibilityNodeInfosByText(label.trim())
        for (n in nodes) {
            if (!n.isVisibleToUser) continue
            var cur: AccessibilityNodeInfo? = n
            while (cur != null) {
                if (cur.isClickable && cur.performAction(AccessibilityNodeInfo.ACTION_CLICK)) return true
                cur = cur.parent
            }
        }
        return false
    }

    private fun toast(t: String) = Toast.makeText(this, t, Toast.LENGTH_LONG).show()
}
