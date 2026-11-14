package com.projetocamaleao

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject

data class Macro(var name: String, var pkg: String, var steps: List<String>)
data class AppItem(var pkg: String, var name: String, val macros: MutableList<Macro>)

object Store {
    private const val K = "apps"
    fun load(c: Context): MutableList<AppItem> {
        val out = mutableListOf<AppItem>()
        try {
            val a = JSONArray(c.getSharedPreferences("camaleao", 0).getString(K, "[]"))
            for (i in 0 until a.length()) {
                val o = a.getJSONObject(i)
                val pkg = o.getString("pkg")
                val ms = mutableListOf<Macro>()
                val ma = o.getJSONArray("macros")
                for (j in 0 until ma.length()) {
                    val m = ma.getJSONObject(j)
                    val s = m.getJSONArray("steps")
                    ms.add(Macro(m.getString("name"), pkg, List(s.length()) { s.getString(it) }))
                }
                out.add(AppItem(pkg, o.getString("name"), ms))
            }
        } catch (_: Exception) {}
        return out
    }
    fun save(c: Context, l: List<AppItem>) {
        val a = JSONArray()
        l.forEach { ap ->
            val ms = JSONArray()
            ap.macros.forEach { ms.put(JSONObject().put("name", it.name).put("steps", JSONArray(it.steps))) }
            a.put(JSONObject().put("pkg", ap.pkg).put("name", ap.name).put("macros", ms))
        }
        c.getSharedPreferences("camaleao", 0).edit().putString(K, a.toString()).apply()
    }
}
