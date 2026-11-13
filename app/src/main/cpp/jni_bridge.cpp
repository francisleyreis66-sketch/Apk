// Ponte JNI: o Java só chama estas duas funções; todo o trabalho é feito em C++.
#include <jni.h>

#include "engine.h"

namespace {
void lancar(JNIEnv* env, const char* msg) {
  jclass c = env->FindClass("java/lang/RuntimeException");
  if (c) env->ThrowNew(c, msg);
}
xp::Bytes copiar(JNIEnv* env, jbyteArray a) {
  jsize n = env->GetArrayLength(a);
  xp::Bytes b((size_t)n);
  if (n) env->GetByteArrayRegion(a, 0, n, reinterpret_cast<jbyte*>(b.data()));
  return b;
}
jbyteArray devolver(JNIEnv* env, const xp::Bytes& b) {
  jbyteArray r = env->NewByteArray((jsize)b.size());
  if (r && !b.empty()) env->SetByteArrayRegion(r, 0, (jsize)b.size(), reinterpret_cast<const jbyte*>(b.data()));
  return r;
}
// avisa o Java do progresso e permite cancelar
struct ProgJava : xp::Progresso {
  JNIEnv* env;
  jobject cb;
  jmethodID m;
  ProgJava(JNIEnv* e, jobject c) : env(e), cb(c), m(nullptr) {
    if (cb) {
      jclass k = env->GetObjectClass(cb);
      m = env->GetMethodID(k, "passo", "(II)Z");
    }
  }
  void passo(int feitos, int total) override {
    if (!cb || !m) return;
    jboolean seguir = env->CallBooleanMethod(cb, m, feitos, total);
    if (env->ExceptionCheck()) { env->ExceptionClear(); throw xp::Erro("cancelado"); }
    if (!seguir) throw xp::Erro("cancelado");
  }
};
}  // namespace

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_xlsmpdf_app_Motor_converter(JNIEnv* env, jclass, jbyteArray dados, jstring titulo, jboolean fit,
                                     jboolean todas, jintArray paginas) {
  try {
    xp::Bytes b = copiar(env, dados);
    const char* t = env->GetStringUTFChars(titulo, nullptr);
    std::string tit = t ? t : "";
    if (t) env->ReleaseStringUTFChars(titulo, t);
    int n = 0;
    xp::Bytes pdf = xp::converterPlanilha(b.data(), b.size(), tit, fit, todas, &n);
    if (paginas && env->GetArrayLength(paginas) > 0) { jint v = n; env->SetIntArrayRegion(paginas, 0, 1, &v); }
    return devolver(env, pdf);
  } catch (const std::exception& e) {
    lancar(env, e.what());
  } catch (...) {
    lancar(env, "erro desconhecido");
  }
  return nullptr;
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_xlsmpdf_app_Motor_juntar(JNIEnv* env, jclass, jobjectArray pdfs, jintArray paginas, jobject progresso) {
  try {
    std::vector<xp::Bytes> bs;
    jsize n = env->GetArrayLength(pdfs);
    for (jsize i = 0; i < n; i++) {
      auto a = static_cast<jbyteArray>(env->GetObjectArrayElement(pdfs, i));
      bs.push_back(copiar(env, a));
      env->DeleteLocalRef(a);
    }
    ProgJava pj(env, progresso);
    int np = 0;
    xp::Bytes pdf = xp::juntarPdfs(bs, &np, progresso ? &pj : nullptr);
    if (paginas && env->GetArrayLength(paginas) > 0) { jint v = np; env->SetIntArrayRegion(paginas, 0, 1, &v); }
    return devolver(env, pdf);
  } catch (const std::exception& e) {
    lancar(env, e.what());
  } catch (...) {
    lancar(env, "erro desconhecido");
  }
  return nullptr;
}
