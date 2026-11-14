// Liga o núcleo do SNES ao Android: linha própria do emulador (tempo certo, voltar no tempo),
// som nativo, desenho por Vulkan e as funções chamadas pelo Java (JNI).
#include <jni.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "audio_out.h"
#include "renderer.h"
#include "rewind.h"
#include "snes_core.h"

namespace {

constexpr double FRAME_SEC = 1.0 / 60.0988;

struct Host {
  std::unique_ptr<SnesCore> core = std::make_unique<SnesCore>();
  std::mutex coreMtx;
  std::thread thread;
  std::atomic<bool> quit{false};

  // troca de janela (superfície) — a linha do emulador é a única que desenha
  std::mutex winMtx;
  std::condition_variable winCv;
  ANativeWindow* pendingWin = nullptr;
  bool winChange = false, winAck = false, sizeChanged = false;
  Renderer renderer;

  AudioOut audio;
  bool audioStarted = false;

  std::atomic<int> jsKeys1{0}, jsKeys2{0}, padKeys1{0}, padKeys2{0};
  std::atomic<bool> loaded{false}, run{false}, external{false}, rewindHeld{false}, rewindOn{true}, foreground{true}, audioEnabled{true};
  std::atomic<float> speed{1.0f};
  std::atomic<int> fps{0};
  std::atomic<bool> vkActive{false};

  std::mutex viewMtx;
  ViewParams view;
  std::atomic<bool> viewDirty{true};

  // última imagem pronta (protegida por coreMtx); drawBuf é só da linha do emulador
  std::vector<uint32_t> shown = std::vector<uint32_t>(SnesCore::W * SnesCore::H, 0xFF000000u);
  std::vector<uint32_t> drawBuf = std::vector<uint32_t>(SnesCore::W * SnesCore::H, 0xFF000000u);
  bool shownDirty = false;
  bool extNew = false;

  Rewinder rew;
  int rewCounter = 0;

  static void audioSink(void* user, int16_t l, int16_t r) { static_cast<Host*>(user)->audio.push(l, r); }

  void start() {
    core->setAudio(&Host::audioSink, this);
    thread = std::thread([this] { loop(); });
  }

  void loop() {
    using clk = std::chrono::steady_clock;
    auto last = clk::now(), fpsT = last;
    double acc = 0, rwAcc = 0;
    int fpsN = 0;
    while (!quit.load()) {
      {
        std::unique_lock<std::mutex> lk(winMtx);
        if (winChange) {
          renderer.shutdown();
          if (pendingWin) { renderer.init(pendingWin); ANativeWindow_release(pendingWin); pendingWin = nullptr; }
          winChange = false; winAck = true; sizeChanged = false;
          viewDirty = true;
          winCv.notify_all();
        }
        if (sizeChanged) { renderer.surfaceChanged(); sizeChanged = false; viewDirty = true; }
      }
      const bool wantAudio = foreground.load() && loaded.load();
      if (wantAudio && (!audioStarted || audio.needsRestart())) {
        if (audioStarted) audio.stop();
        audioStarted = audio.start();
      } else if (!wantAudio && audioStarted) { audio.stop(); audioStarted = false; }

      const auto now = clk::now();
      double dt = std::chrono::duration<double>(now - last).count();
      last = now;
      if (dt > 0.12) dt = FRAME_SEC;
      bool newFrame = false;
      int n = 0;
      {
        std::lock_guard<std::mutex> lk(coreMtx);
        if (loaded && run && foreground && !external) {
          if (rewindHeld && rewindOn) {
            // volta ~2x mais rápido que o jogo anda
            rwAcc += dt;
            acc = 0;
            while (rwAcc >= FRAME_SEC * 2) {
              rwAcc -= FRAME_SEC * 2;
              std::vector<uint8_t> st;
              if (!rew.pop(st)) { rwAcc = 0; break; }
              core->setState(st.data(), st.size());
              core->setAudioEnabled(false);
              core->setKeys(0); core->setKeys2(0);
              core->runFrame();
              newFrame = true;
              n++;
            }
          } else {
            rwAcc = 0;
            const float sp = speed.load();
            core->setAudioEnabled(audioEnabled.load() && sp == 1.0f);
            acc += dt * sp;
            const int maxN = sp > 1.0f ? (int)std::ceil(sp * 2) : 2;  // celular lento: desacelera em vez de travar
            while (acc >= FRAME_SEC && n < maxN) {
              core->setKeys(jsKeys1.load() | padKeys1.load());
              core->setKeys2(jsKeys2.load() | padKeys2.load());
              core->runFrame();
              acc -= FRAME_SEC;
              n++;
              if (rewindOn && ++rewCounter >= 4) { rewCounter = 0; rew.push(core->getState()); }
            }
            if (acc > FRAME_SEC * 2) acc = 0;
            if (n) newFrame = true;
          }
        } else {
          acc = 0; rwAcc = 0;
        }
        if (external && extNew) { extNew = false; newFrame = true; n = 1; }
        if (newFrame) std::memcpy(shown.data(), core->framePtr(), shown.size() * 4);
        if (shownDirty) { shownDirty = false; newFrame = true; }
        if (newFrame) std::memcpy(drawBuf.data(), shown.data(), drawBuf.size() * 4);
        fpsN += n;
      }
      if (std::chrono::duration<double>(now - fpsT).count() >= 1.0) {
        fps = (int)std::lround(fpsN / std::chrono::duration<double>(now - fpsT).count());
        fpsN = 0; fpsT = now;
      }
      if (renderer.ready() && (newFrame || viewDirty.exchange(false))) {
        ViewParams v;
        { std::lock_guard<std::mutex> lk(viewMtx); v = view; }
        renderer.draw(newFrame ? drawBuf.data() : nullptr, v);
        vkActive = renderer.usingVulkan();
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
    }
    renderer.shutdown();
    if (audioStarted) audio.stop();
  }

  void setWindow(ANativeWindow* w) {
    std::unique_lock<std::mutex> lk(winMtx);
    if (pendingWin) ANativeWindow_release(pendingWin);
    pendingWin = w;
    winChange = true; winAck = false;
    // o Android exige que o desenho pare antes de a superfície ser destruída
    winCv.wait_for(lk, std::chrono::seconds(3), [this] { return winAck; });
  }
};

Host* host = nullptr;
std::mutex hostMtx;

Host* H() {
  std::lock_guard<std::mutex> lk(hostMtx);
  if (!host) { host = new Host(); host->start(); }
  return host;
}

std::vector<uint8_t> bytesOf(JNIEnv* env, jbyteArray a) {
  std::vector<uint8_t> v;
  if (!a) return v;
  const jsize n = env->GetArrayLength(a);
  v.resize((size_t)n);
  if (n) env->GetByteArrayRegion(a, 0, n, reinterpret_cast<jbyte*>(v.data()));
  return v;
}
jbyteArray toArray(JNIEnv* env, const void* p, size_t n) {
  jbyteArray a = env->NewByteArray((jsize)n);
  if (a && n) env->SetByteArrayRegion(a, 0, (jsize)n, reinterpret_cast<const jbyte*>(p));
  return a;
}

}  // namespace

#define JF(ret, name) extern "C" JNIEXPORT ret JNICALL Java_com_retrogame_snes_NativeCore_##name

JF(void, init)(JNIEnv*, jclass) { H(); }

JF(void, setSurface)(JNIEnv* env, jclass, jobject surface) {
  ANativeWindow* w = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
  H()->setWindow(w);
}

JF(void, surfaceChanged)(JNIEnv*, jclass) {
  Host* h = H();
  std::lock_guard<std::mutex> lk(h->winMtx);
  h->sizeChanged = true;
}

JF(void, setForeground)(JNIEnv*, jclass, jboolean fg) { H()->foreground = fg; }

JF(jboolean, loadRom)(JNIEnv* env, jclass, jbyteArray rom) {
  Host* h = H();
  const std::vector<uint8_t> b = bytesOf(env, rom);
  std::lock_guard<std::mutex> lk(h->coreMtx);
  h->core->loadRom(b.data(), b.size());
  h->rew.clear(); h->rewCounter = 0;
  h->audio.clear();
  std::memcpy(h->shown.data(), h->core->framePtr(), h->shown.size() * 4);
  h->shownDirty = true;
  h->loaded = true;
  h->viewDirty = true;
  return JNI_TRUE;
}

JF(void, reset)(JNIEnv*, jclass) {
  Host* h = H();
  std::lock_guard<std::mutex> lk(h->coreMtx);
  h->core->reset();
  h->rew.clear();
  h->audio.clear();
}

JF(jbyteArray, getSave)(JNIEnv* env, jclass) {
  Host* h = H();
  std::lock_guard<std::mutex> lk(h->coreMtx);
  const std::vector<uint8_t> s = h->core->getSave();
  return toArray(env, s.data(), s.size());
}

JF(void, setSave)(JNIEnv* env, jclass, jbyteArray data) {
  Host* h = H();
  const std::vector<uint8_t> b = bytesOf(env, data);
  std::lock_guard<std::mutex> lk(h->coreMtx);
  h->core->setSave(b.data(), b.size());
}

JF(jint, saveType)(JNIEnv*, jclass) {
  Host* h = H();
  std::lock_guard<std::mutex> lk(h->coreMtx);
  return h->core->saveType();
}

JF(jstring, chipWarning)(JNIEnv* env, jclass) {
  Host* h = H();
  std::string w;
  { std::lock_guard<std::mutex> lk(h->coreMtx); w = h->core->chipWarning(); }
  return env->NewStringUTF(w.c_str());
}

JF(jbyteArray, getState)(JNIEnv* env, jclass) {
  Host* h = H();
  std::lock_guard<std::mutex> lk(h->coreMtx);
  const std::vector<uint8_t> s = h->core->getState();
  return toArray(env, s.data(), s.size());
}

JF(jboolean, setState)(JNIEnv* env, jclass, jbyteArray data) {
  Host* h = H();
  const std::vector<uint8_t> b = bytesOf(env, data);
  std::lock_guard<std::mutex> lk(h->coreMtx);
  const bool ok = h->core->setState(b.data(), b.size());
  if (ok) { h->rew.clear(); h->audio.clear(); }
  return ok ? JNI_TRUE : JNI_FALSE;
}

JF(jint, stateHash)(JNIEnv*, jclass) {
  Host* h = H();
  std::lock_guard<std::mutex> lk(h->coreMtx);
  return (jint)h->core->stateHash();
}

JF(jint, setCheats)(JNIEnv* env, jclass, jstring text) {
  Host* h = H();
  std::vector<std::string> lines;
  if (text) {
    const char* c = env->GetStringUTFChars(text, nullptr);
    std::string s(c ? c : "");
    if (c) env->ReleaseStringUTFChars(text, c);
    size_t p = 0;
    while (p <= s.size()) {
      const size_t q = s.find('\n', p);
      const std::string line = s.substr(p, q == std::string::npos ? std::string::npos : q - p);
      if (!line.empty()) lines.push_back(line);
      if (q == std::string::npos) break;
      p = q + 1;
    }
  }
  std::lock_guard<std::mutex> lk(h->coreMtx);
  return h->core->setCheats(lines);
}

JF(void, setKeys)(JNIEnv*, jclass, jint k1, jint k2) { Host* h = H(); h->jsKeys1 = k1; h->jsKeys2 = k2; }
JF(void, setPad)(JNIEnv*, jclass, jint player, jint mask) { Host* h = H(); (player ? h->padKeys2 : h->padKeys1) = mask; }

JF(void, setRun)(JNIEnv*, jclass, jboolean run, jfloat speed, jboolean audio) {
  Host* h = H();
  h->speed = speed > 0 ? speed : 1.0f;
  h->audioEnabled = audio;
  h->run = run;
}

JF(void, setExternal)(JNIEnv*, jclass, jboolean ext) {
  Host* h = H();
  std::lock_guard<std::mutex> lk(h->coreMtx);
  h->external = ext;
  h->extNew = false;
}

// modo 2 jogadores (o JavaScript decide quando cada quadro roda)
JF(void, stepFrame)(JNIEnv*, jclass) {
  Host* h = H();
  std::lock_guard<std::mutex> lk(h->coreMtx);
  h->core->setAudioEnabled(h->audioEnabled.load());
  h->core->setKeys(h->jsKeys1.load());
  h->core->setKeys2(h->jsKeys2.load());
  h->core->runFrame();
  h->extNew = true;
}

JF(void, setRewind)(JNIEnv*, jclass, jboolean held) { H()->rewindHeld = held; }
JF(void, setRewindEnabled)(JNIEnv*, jclass, jboolean on) {
  Host* h = H();
  std::lock_guard<std::mutex> lk(h->coreMtx);
  h->rewindOn = on;
  if (!on) h->rew.clear();
}
JF(jint, rewindCount)(JNIEnv*, jclass) {
  Host* h = H();
  std::lock_guard<std::mutex> lk(h->coreMtx);
  return (jint)h->rew.count();
}

JF(void, setView)(JNIEnv*, jclass, jfloat x, jfloat y, jfloat w, jfloat hh, jint rot, jint filter) {
  Host* h = H();
  { std::lock_guard<std::mutex> lk(h->viewMtx); h->view.x = x; h->view.y = y; h->view.w = w; h->view.h = hh; h->view.rot = rot; h->view.filter = filter; }
  h->viewDirty = true;
}

JF(void, setColorFix)(JNIEnv*, jclass, jboolean on) {
  Host* h = H();
  std::lock_guard<std::mutex> lk(h->coreMtx);
  h->core->setColorCorrection(on);
}

JF(void, setVolume)(JNIEnv*, jclass, jfloat v) { H()->audio.setVolume(v); }

JF(jboolean, takeSaveDirty)(JNIEnv*, jclass) {
  Host* h = H();
  std::lock_guard<std::mutex> lk(h->coreMtx);
  return h->core->takeSaveDirty() ? JNI_TRUE : JNI_FALSE;
}

JF(jint, fps)(JNIEnv*, jclass) { return H()->fps.load(); }

JF(jbyteArray, getFrame)(JNIEnv* env, jclass) {
  Host* h = H();
  std::lock_guard<std::mutex> lk(h->coreMtx);
  return toArray(env, h->shown.data(), h->shown.size() * 4);
}

JF(jboolean, vulkan)(JNIEnv*, jclass) { return H()->vkActive.load() ? JNI_TRUE : JNI_FALSE; }
