# XLSM para PDF (Android, motor em C++)

App Android que faz o mesmo que a versão em HTML:

- **Planilha → PDF**: converte .xlsm/.xlsx em PDF do jeito que aparece no Excel (só linhas e colunas visíveis, cores, bordas, mesclagens, datas em dd/mm/aaaa, área de impressão, várias abas).
- **Juntar PDFs**: une vários PDFs em um só, na ordem da lista (↑ ↓ ✕).
- **⚡ Fazer tudo**: guarda uma pasta; num toque pega todos os .xlsm dela, converte, junta em um PDF e abre o envio no WhatsApp.
- **📄 .xlsm → 1 PDF → WhatsApp**: o mesmo, escolhendo os arquivos na hora.
- Funciona sem internet.

## Onde está cada coisa

| Parte | Arquivo | Linguagem |
|---|---|---|
| Ler o ZIP e o XML da planilha | `app/src/main/cpp/util.hpp` | C++ |
| Estilos, formatos de número/data, layout em páginas A4, gerar o PDF | `app/src/main/cpp/xlsx2pdf.cpp` | C++ |
| Juntar PDFs (lê xref, object streams, PDFs quebrados) | `app/src/main/cpp/pdfmerge.cpp` | C++ |
| Ponte Java ↔ C++ | `app/src/main/cpp/jni_bridge.cpp`, `Motor.java` | C++ / Java |
| Tela, escolher arquivos/pasta, WhatsApp, Downloads | `MainActivity.java` | Java (só interface) |

O Android exige Java/Kotlin para abrir o seletor de arquivos e o compartilhar; todo o processamento é C++ (só usa a zlib, que já vem no Android).

## Gerar o APK

O GitHub gera o APK sozinho (`.github/workflows/apk.yml`):

1. Crie um repositório no GitHub e envie estes arquivos.
2. Abra a aba **Actions** e espere o "Gerar APK" ficar verde (uns 5–8 min).
3. Abra **Releases** e toque em **XLSM-para-PDF.apk** para baixar.
4. No celular, permita "instalar apps desconhecidos" para o navegador/arquivos e instale.

No computador com Android Studio: abra a pasta e use *Build › Build APK(s)*.
