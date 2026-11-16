# Imprimir USB

App Android que imprime **direto pelo cabo USB (OTG)** em impressoras que não funcionam no celular, como a **HP LaserJet P1005**. Ele usa o driver livre `foo2xqx` do projeto [foo2zjs](https://github.com/koenkooi/foo2zjs) (licença GPL).

## Como gerar o APK no GitHub
1. Crie um repositório novo no GitHub e envie **todos** os arquivos desta pasta, inclusive a pasta `.github`.
2. Abra a aba **Actions**. O workflow **Gerar APK** roda sozinho a cada envio. Se não rodar, toque em **Run workflow**.
3. Quando ficar verde, abra a execução e baixe o artefato **Imprimir-USB-APK**. Ele vem em .zip, com o APK dentro.
4. Instale o APK no celular. O Android vai pedir para permitir a instalação de fontes desconhecidas.

Se ficar vermelho, abra o passo que falhou e copie o texto do erro.

## Interface
O app usa a mesma tela do "Imprimir" (arquivo `app/src/main/assets/index.html`). Dentro do app, o botão **Imprimir** manda direto para a impressora USB e **Salvar PDF** grava em Downloads. A seção **Impressora USB** autoriza a impressora, carrega o firmware e ativa o app no menu Imprimir de outros apps.

## Como usar
1. Ligue a impressora no celular com o cabo e o adaptador OTG. O Android pergunta se quer abrir o **Imprimir USB**. Marque "usar sempre" e toque em OK.
2. No app: **Escolher PDF ou foto**, escolha o papel e toque em **Imprimir pelo cabo USB**.
3. Para imprimir de qualquer app: **Abrir configurações de impressão**, ative **Imprimir USB**. A impressora passa a aparecer no menu *Imprimir* do Chrome, da galeria etc.
4. Do app web "Imprimir": toque em **PDF / Enviar** e escolha **Imprimir USB**.

## Firmware da P1005
A P1005 pode precisar receber um firmware toda vez que é ligada. O app detecta isso e envia o arquivo, mas ele precisa ser carregado uma vez em **Carregar arquivo de firmware**. O arquivo é o `sihpP1005.dl` do foo2zjs (script `getweb P1005` num PC com Linux).

## Opções do driver
O artefato **driver-ajuda** traz as opções aceitas pelo `foo2xqx`. Se precisar, coloque opções extras no campo *Avançado* do app, sem precisar compilar de novo.
