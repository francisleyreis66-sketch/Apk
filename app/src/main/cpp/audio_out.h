// Som nativo do Android (AAudio), com pouca latência.
// O emulador gera 32 kHz; aqui o som é convertido para a taxa do aparelho com interpolação
// cúbica, e a velocidade é ajustada bem de leve (no máximo 0,5%) para o buffer nunca
// esvaziar nem encher — é isso que evita estalos.
#pragma once
#include <aaudio/AAudio.h>
#include <atomic>
#include <cstdint>
#include <mutex>

class AudioOut {
public:
  bool start();
  void stop();
  bool needsRestart() const { return restart.load(); }
  void push(int16_t l, int16_t r);   // chamado pelo emulador (uma amostra de 32 kHz)
  void setVolume(float v) { volume.store(v); }
  void clear() { clearReq.store(true); }

private:
  static aaudio_data_callback_result_t dataCb(AAudioStream* s, void* user, void* data, int32_t frames);
  static void errorCb(AAudioStream* s, void* user, aaudio_result_t err);
  void render(float* out, int32_t frames);

  static constexpr uint32_t RING = 16384, RM = RING - 1;  // amostras estéreo de 32 kHz (~0,5 s)
  int16_t ringL[RING] = {0}, ringR[RING] = {0};
  std::atomic<uint32_t> wr{0}, rd{0};
  std::atomic<float> volume{1.0f};
  std::atomic<bool> restart{false}, clearReq{false};
  std::mutex mtx;
  AAudioStream* stream = nullptr;
  double outRate = 48000.0;
  // estado do conversor de taxa (só usado pela linha do som)
  float hL[4] = {0, 0, 0, 0}, hR[4] = {0, 0, 0, 0};
  double frac = 0.0;
};
