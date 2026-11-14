#include "audio_out.h"
#include <cmath>
#include <algorithm>

// alvo: ~45 ms de som guardado (1440 amostras de 32 kHz)
static constexpr double TARGET_FILL = 1440.0;

bool AudioOut::start() {
  std::lock_guard<std::mutex> lk(mtx);
  if (stream) return true;
  restart.store(false);
  AAudioStreamBuilder* b = nullptr;
  if (AAudio_createStreamBuilder(&b) != AAUDIO_OK || !b) return false;
  AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
  AAudioStreamBuilder_setChannelCount(b, 2);
  AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
  AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
  AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
  AAudioStreamBuilder_setDataCallback(b, &AudioOut::dataCb, this);
  AAudioStreamBuilder_setErrorCallback(b, &AudioOut::errorCb, this);
  AAudioStream* s = nullptr;
  const aaudio_result_t r = AAudioStreamBuilder_openStream(b, &s);
  AAudioStreamBuilder_delete(b);
  if (r != AAUDIO_OK || !s) return false;
  const int32_t rate = AAudioStream_getSampleRate(s);
  outRate = rate > 0 ? rate : 48000.0;
  const int32_t burst = AAudioStream_getFramesPerBurst(s);
  if (burst > 0) AAudioStream_setBufferSizeInFrames(s, burst * 2);
  frac = 0.0;
  for (int i = 0; i < 4; i++) { hL[i] = 0; hR[i] = 0; }
  if (AAudioStream_requestStart(s) != AAUDIO_OK) { AAudioStream_close(s); return false; }
  stream = s;
  return true;
}

void AudioOut::stop() {
  std::lock_guard<std::mutex> lk(mtx);
  if (!stream) return;
  AAudioStream_requestStop(stream);
  AAudioStream_close(stream);
  stream = nullptr;
}

void AudioOut::push(int16_t l, int16_t r) {
  const uint32_t w = wr.load(std::memory_order_relaxed), d = rd.load(std::memory_order_acquire);
  if (w - d >= RING - 1) return;  // cheio: descarta (só acontece se o som parou)
  ringL[w & RM] = l; ringR[w & RM] = r;
  wr.store(w + 1, std::memory_order_release);
}

aaudio_data_callback_result_t AudioOut::dataCb(AAudioStream*, void* user, void* data, int32_t frames) {
  static_cast<AudioOut*>(user)->render(static_cast<float*>(data), frames);
  return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void AudioOut::errorCb(AAudioStream*, void* user, aaudio_result_t) {
  // fone conectado/desconectado etc.: o fluxo precisa ser reaberto (fora desta linha)
  static_cast<AudioOut*>(user)->restart.store(true);
}

static inline float hermite(const float* h, float t) {
  // h[0]=mais antigo ... h[3]=mais novo; interpola entre h[1] e h[2]
  const float c0 = h[1];
  const float c1 = 0.5f * (h[2] - h[0]);
  const float c2 = h[0] - 2.5f * h[1] + 2.0f * h[2] - 0.5f * h[3];
  const float c3 = 0.5f * (h[3] - h[0]) + 1.5f * (h[1] - h[2]);
  return ((c3 * t + c2) * t + c1) * t + c0;
}

void AudioOut::render(float* out, int32_t frames) {
  if (clearReq.exchange(false)) rd.store(wr.load(std::memory_order_acquire), std::memory_order_release);
  uint32_t d = rd.load(std::memory_order_relaxed);
  const uint32_t w = wr.load(std::memory_order_acquire);
  const double fill = (double)(w - d);
  // controle de taxa: buffer acima do alvo -> consome um pouco mais rápido, e vice-versa
  double adj = (fill - TARGET_FILL) / TARGET_FILL;
  adj = std::max(-1.0, std::min(1.0, adj));
  const double step = (32000.0 / outRate) * (1.0 + 0.005 * adj);
  const float vol = volume.load();
  uint32_t avail = w - d;
  for (int32_t i = 0; i < frames; i++) {
    frac += step;
    while (frac >= 1.0) {
      frac -= 1.0;
      float nl, nr;
      if (avail) {
        nl = ringL[d & RM] * (1.0f / 32768.0f); nr = ringR[d & RM] * (1.0f / 32768.0f);
        d++; avail--;
      } else {
        // faltou som: repete o último bem devagar até o silêncio (sem estalo)
        nl = hL[3] * 0.995f; nr = hR[3] * 0.995f;
      }
      hL[0] = hL[1]; hL[1] = hL[2]; hL[2] = hL[3]; hL[3] = nl;
      hR[0] = hR[1]; hR[1] = hR[2]; hR[2] = hR[3]; hR[3] = nr;
    }
    const float t = (float)frac;
    float l = hermite(hL, t) * vol, r = hermite(hR, t) * vol;
    l = std::max(-1.0f, std::min(1.0f, l));
    r = std::max(-1.0f, std::min(1.0f, r));
    out[i * 2] = l; out[i * 2 + 1] = r;
  }
  rd.store(d, std::memory_order_release);
}
