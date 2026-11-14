# Retro Game SNES — APK (C++ + Vulkan)

App Android híbrido:

- **Emulador em C++** (`app/src/main/cpp/snes_core.*`): tradução fiel do núcleo em JavaScript
  (65816, PPU, SPC700, DSP, DMA/HDMA). Testado quadro a quadro contra o núcleo JS: mesma imagem,
  mesmo som e mesma memória em todos os testes.
- **Imagem por Vulkan** (`renderer.cpp` + `shaders/`), com filtros: pixels puros, nítido e liso,
  suave, linhas de LCD e TV de tubo (CRT). Sem Vulkan no aparelho, desenha pela CPU.
- **Som nativo (AAudio)** com conversão de taxa cúbica e ajuste automático de velocidade (sem estalos).
- **Voltar no tempo**: segure ⏪ na tela, L2 no controle ou R no teclado (até ~30 s).
- **Controles USB/Bluetooth** lidos direto pelo Android (R2 avança, L2 volta no tempo).
- A interface (biblioteca, capas, menus, estados, trapaças, 2 jogadores) é a mesma página HTML,
  em `app/src/main/assets/index.html`. Estados salvos na versão antiga (navegador) continuam abrindo.

## Gerar o APK

Pelo **Assistente APK**: modo "Projeto Android Gradle", envie o ZIP deste projeto. Não precisa de `gradlew`:
o GitHub instala o Gradle 8.13. O APK sai em `app/build/outputs/apk/debug/`.

No computador: Android Studio (com NDK) → abrir a pasta → Build › Build APK.

Requisitos: Android 8.0 ou mais novo.
