<div align="center">

# NucleoOS

### O sistema operacional web‑nativo para o M5Stack Cardputer

**O sistema roda no aparelho. O seu navegador é o console de operação.**
Um ESP32‑S3, sem PSRAM, ~512 KB de RAM, transformado num sistema operacional moderno de verdade, com vários apps,
uma assistente de IA que usa o melhor “cérebro” disponível (offline no aparelho, os modelos do seu próprio PC ou a
nuvem), um shell web com cara de desktop, um console de jogos de bolso, um laboratório de segurança, transcrição de
voz e geração de imagens.

`ESP32‑S3 / M5StampS3` · `firmware ESP‑IDF` · `shell web PWA` · `código em inglês`

[![Release oficial](https://img.shields.io/github/v/release/indecenti/NucleoOs?label=release%20oficial)](https://github.com/indecenti/NucleoOs/releases/latest)
![Licença: PolyForm Noncommercial 1.0.0](https://img.shields.io/badge/licen%C3%A7a-PolyForm%20Noncommercial%201.0.0-blue)
![Plataforma: ESP32-S3](https://img.shields.io/badge/plataforma-ESP32--S3-informational)
![Framework: ESP-IDF](https://img.shields.io/badge/firmware-ESP--IDF-red)

**[🇧🇷 Este fork](#este-fork)** · **[⬇️ Instalar](#instalar)** · **[📖 Guia do usuário (inglês)](docs/user-guide.md)** · **[English README](README.md)**

<sub>Livre para uso não comercial · **uso comercial exige licença paga** do autor original → niki070585@gmail.com</sub>

<sub>⚠️ Inclui ferramentas de teste de segurança: **somente testes autorizados, pesquisa e educação**. [Leia as regras ↓](#uso-legal)</sub>

<img src="docs/screenshots/webos.gif" width="860" alt="Desktop web do NucleoOS: um shell no estilo Windows (arquivos, planilha, Paint, jogos, monitor) servido direto do Cardputer" />

</div>

---

<a id="este-fork"></a>
## 🇧🇷 Este fork: NucleoOS em português do Brasil

Este é um fork do [indecenti/NucleoOs](https://github.com/indecenti/NucleoOs). O projeto, o código e a licença são do
autor original; este fork acrescenta o **português do Brasil** em todo o sistema e alguns apps. As mudanças:

- **Interface em pt-BR.** O português é um idioma completo da interface nativa (launcher, Central de Controle,
  Configurações, apps), do shell web e dos apps web. Escolha “Português” na primeira inicialização ou em
  Configurações.
- **ANIMA em português, offline.** A assistente entende e responde em português sem internet:
  - comandos do aparelho: hora, data, bateria, volume, brilho, abrir apps, timers, alarmes, lembretes, notas, agenda;
  - contas, porcentagens, desconto, IVA, conversões de unidade (“quantos km são 13 milhas”), geometria, física,
    números primos, romanos, binário, signos, fuso horário de cidades;
  - todas as sugestões que o app oferece e conversa comum (“tudo bem?”, “oi, tudo bem?”, “tchau”, “valeu”);
  - perguntas de conhecimento por um pacote em português de ~1.500 respostas revisadas (capitais, ciência,
    tecnologia, pessoas conhecidas), lido do SD sem ocupar RAM.
- **Voz offline em português.** A ANIMA fala português com a voz *Francisca* (Microsoft Edge neural TTS, gerada uma
  vez no PC): ~9.700 trechos pré‑gravados, números e horas compostos (“São 9 e 30”, “duzentos e trinta e quatro”),
  respostas de conhecimento lidas frase por frase. O pacote (~300 MB no SD) não fica no git: gere com
  `tools/nucleo-tts/` (veja [docs/tts.md](docs/tts.md)).
- **Tradutor offline pt / en / ja.** Dicionários de português↔inglês, inglês↔japonês e português↔japonês
  (via inglês, indicado na resposta), além do italiano↔inglês original. O japonês aparece em **romaji**, porque a
  tela do Cardputer só tem fonte ASCII: “traduz cachorro para japonês” → *inu (via dog)*.
- **Ditado em português no navegador.** O modelo Vosk de português, local, no ANIMA e no app de Ditado.
- **RemoCon.** Controles remotos infravermelhos definidos em JSON, desenhados como o controle de verdade, com
  carrossel de aparelhos e teclado numérico na segunda tela. Veja [docs/remocon.md](docs/remocon.md).
- **Teclado.** O Cardputer vira teclado do computador por USB‑C e Bluetooth ao mesmo tempo, com layout ABNT2.
- **Build no macOS.** Os gates de teste do host e o build do firmware funcionam no Mac (ESP‑IDF 5.4.4).

Detalhes técnicos e limites: [docs/anima-pt.md](docs/anima-pt.md) (em inglês, como toda a documentação do projeto).

### Como ter os recursos deste fork

O fork tem **releases e instalador próprios**, assinados com as chaves do fork:

- **Instalador web:** abra **[sedir.github.io/NucleoOs](https://sedir.github.io/NucleoOs/)** no Chrome ou Edge,
  conecte o Cardputer e clique em instalar.
- **Arquivos:** [releases do fork](https://github.com/sedir/NucleoOs/releases/latest), com o firmware completo
  (`nucleoos-latest.bin`), a imagem OTA e o conteúdo do SD (`nucleoos-latest-sd.zip`).
- **Atualizações:** depois de instalado, o aparelho busca novas versões **neste fork** (Configurações ▸ Atualizações),
  nunca no original.

**Sem M5Launcher:** o firmware do fork (3,3 MB) não cabe no espaço de app que o M5Launcher reserva (3,06 MB), então ele
só se instala sozinho no Cardputer, pelo instalador web ou pelo esptool.

Vindo do NucleoOS original, a primeira instalação precisa ser pelo **cabo USB** (instalador web ou esptool): o
firmware original só aceita releases assinadas pelo autor original. Daí em diante, as atualizações chegam pelo Wi‑Fi.

A voz em português (~300 MB) não vai na release: gere com `tools/nucleo-tts/` (veja [docs/tts.md](docs/tts.md)) e copie
para `/data/tts/pt/` no cartão.

Para compilar você mesmo (ESP‑IDF 5.4.4, a versão que o projeto fixa):

```bash
export IDF_PATH=~/esp/esp-idf-v5.4.4
. $IDF_PATH/export.sh
cd firmware && idf.py build
# grava pelo cabo USB-C (troque a porta pela sua; no Windows, COM3 etc.)
cd build && python -m esptool --chip esp32s3 -p /dev/cu.usbmodem101 -b 460800 write_flash @flash_args
```

---

<a id="instalar"></a>
## 🚀 Instalar o NucleoOS original: três jeitos

**Você precisa de:** um Cardputer (original ou ADV, o mesmo firmware), um cabo USB‑C de dados e um cartão microSD
formatado em **FAT32**.
> ⚠️ **Só FAT32, não exFAT.** Cartões acima de 32 GB vêm de fábrica em exFAT, e o Windows não oferece FAT32 para
> eles: reformate com uma ferramenta como guiformat ou Rufus. Num cartão exFAT o firmware inicia, mas o sistema web
> não é lido e o navegador mostra a página de *Rescue* (“The desktop shell is unavailable on this device”).

### 1. Instalador web: o mais fácil, sem instalar nada
1. Abra **[indecenti.github.io/NucleoOs](https://indecenti.github.io/NucleoOs/)** no **Chrome ou Edge**, num computador.
2. Conecte o Cardputer e clique em **Connect & install NucleoOS**.
3. Quando terminar, aperte **reset**.

### 2. M5Launcher: mantenha seus outros firmwares
Já usa o [M5Launcher](https://github.com/bmorcelli/Launcher)? O NucleoOS se instala como convidado ao lado deles.
1. Baixe **[`nucleoos-latest.bin`](https://github.com/indecenti/NucleoOs/releases/latest/download/nucleoos-latest.bin)** e copie para o cartão SD.
2. No Launcher, escolha o arquivo no **SD installer**. As atualizações também passam pelo Launcher.
3. O `.bin` é só o firmware. O sistema web fica no cartão: extraia
   [`nucleoos-latest-sd.zip`](https://github.com/indecenti/NucleoOs/releases/latest/download/nucleoos-latest-sd.zip)
   na raiz do cartão ou deixe o assistente da primeira inicialização baixar pelo Wi‑Fi (veja abaixo).
4. A loja de apps nativos em C precisa do NucleoOS 0.8.4 ou mais novo instalado pelo Launcher: ele cria uma partição
   de flash de 448 KB (`nucleo_nap`) de onde os apps nativos rodam. Numa instalação antiga a loja fica cinza:
   instale o novo `.bin` pelo Launcher de novo.

### 3. esptool: linha de comando
Baixe **[`nucleoos-latest.bin`](https://github.com/indecenti/NucleoOs/releases/latest/download/nucleoos-latest.bin)** e, com o [esptool](https://github.com/espressif/esptool) (`pip install esptool`):
```bash
esptool --chip esp32s3 --baud 921600 write_flash 0x0 nucleoos-latest.bin
```

### Depois: o cartão SD e a primeira inicialização
- **Conteúdo do SD.** O NucleoOS serve os apps a partir do cartão. Já com Wi‑Fi, o assistente da primeira
  inicialização baixa tudo sozinho: verificado, retomável, pulável. Ou extraia
  **[`nucleoos-latest-sd.zip`](https://github.com/indecenti/NucleoOs/releases/latest/download/nucleoos-latest-sd.zip)**
  na raiz do cartão, para que `/apps`, `/www`, `/system` e `/data` fiquem no topo. O zip é conteúdo do cartão, nunca
  um firmware.
- **Primeira inicialização.** Um assistente curto, no teclado do Cardputer, pergunta o idioma, o Wi‑Fi (ou liga o
  ponto de acesso do próprio aparelho) e um nome. A tela então mostra um endereço: abra num navegador da mesma rede e
  pareie com o PIN de 6 dígitos (Central de Controle ▸ Web, ou Configurações ▸ Aparelho).
- **Atualizações** chegam sozinhas: Configurações ▸ Atualizações oferece uma atualização de um clique, com
  verificação de checksum, slots A/B e volta automática em caso de falha, e sincroniza o conteúdo do SD (só os
  arquivos alterados).

Todos os arquivos: **[release mais recente](https://github.com/indecenti/NucleoOs/releases/latest)** · passo a passo
completo: **[Guia do usuário](docs/user-guide.md)** (inglês).

---

## Por que o NucleoOS é o melhor sistema para o Cardputer

A maioria dos firmwares do Cardputer é um menu de ferramentas. **O NucleoOS é um sistema operacional de verdade**:
um registro de apps com permissões declaradas, um barramento de eventos, pareamento e autenticação, OTA com volta
automática, um motor de idiomas, notificações, um gerenciador de energia e **duas interfaces completas** que
compartilham o mesmo contrato:

| | **Firmware nativo** (na tela de 240×135) | **NucleoOS Web** (qualquer navegador da sua rede) |
|---|---|---|
| Roda | No próprio ESP32‑S3 | Servido *pelo* aparelho, desenhado no seu navegador |
| Ponto forte | Sempre disponível, sem computador, UX de smartwatch | Janelas de desktop, mouse + teclado, tela grande |
| Exemplos | Jogos, ferramentas de segurança, sensores, mídia, chat da ANIMA | Gerenciador de arquivos, planilha, Paint+IA, Game Center, IDE |
| Regra de design | Fontes grandes, todo pixel usado, autocompletar/histórico/atalho 1‑9 | Janelas de verdade, apps em tela cheia, arrastar e soltar |

A divisão é proposital: **trabalho pesado vai para o navegador, nunca para os 18 KB de heap do aparelho.** É essa
ideia que permite a um microcontrolador sem PSRAM oferecer uma experiência desse tamanho.

<div align="center">
<img src="docs/screenshots/cardputer.gif" width="520" alt="Interface nativa do NucleoOS no Cardputer: launcher, música e vídeo, na tela de 240×135" />
<br><sub>A interface nativa no aparelho (240×135): launcher → música → vídeo, capturada ao vivo pelo Wi‑Fi do endpoint
<code>GET /api/screen</code> do próprio aparelho, que transmite o painel físico como BMP, sem canvas, ~0 de heap.</sub>
</div>

## 🔌 Hardware: um firmware, as duas placas

**O NucleoOS roda tanto no M5Stack Cardputer original quanto no Cardputer ADV, mais novo.** Um único binário
universal detecta na inicialização qual você tem (mapa do teclado + HAL do codec de áudio): compile uma vez e grave
em qualquer um.

<div align="center">
<img src="docs/screenshots/hardware.png" width="820" alt="M5Stack Cardputer (original, à esquerda) e Cardputer ADV (à direita): o NucleoOS roda nos dois" />
<br><sub>Esquerda: Cardputer original (M5StampS3). Direita: Cardputer ADV (Stamp‑S3A, bateria maior, IMU, saída de 3,5 mm).
Fotos de produto © M5Stack.</sub>
</div>

## ✨ Novidades da v0.8.0 (versão original)

- **Uma segunda tela.** Ligue um display touch SPI de 2,8" (320×240) no conector EXT do Cardputer ADV e o launcher,
  os jogos e os apps ganham **um layout próprio para ela**, com toque direto: toque num bloco, arraste uma lista,
  aperte o próprio conteúdo. A tela do Cardputer vira companheira (o que você digita, o placar, as dicas). O painel
  tem brilho próprio, calibração de toque em 4 pontos e desliga junto com o Cardputer no modo de espera.
  [Fiação e esquema ↓](#segunda-tela)
- **A Loja tem apps nativos.** Dois canais, escolhidos primeiro: apps **C nativos** que rodam no próprio Cardputer
  (módulos assinados instalados no cartão SD, carregados quando você abre) e **apps web** servidos ao navegador.
  Todo catálogo e pacote é assinado offline e verificado no aparelho antes de instalar.
  [Como funciona ↓](#loja)
- **Os jogos vêm da Loja**, não de dentro do firmware: cerca de 480 KB de flash liberados para o sistema. Depois de
  atualizar, abra **Loja ▸ C nativo** e instale os que você joga; saves, fixados e recentes continuam.
- **Um sistema web para todo aparelho NucleoOS.** O desktop web e seus apps agora vêm do build compartilhado
  `nucleo-webos` (também usado pelo Handheld e pelo P4), com a mesma Loja no navegador.

Release anterior (v0.7.5): atualização automática opcional com contagem regressiva cancelável, toda a interface
nativa no idioma do sistema, uma Lanterna com luz vermelha/laranja e SOS.

Notas completas: [CHANGELOG.md](CHANGELOG.md). Aparelhos instalados recebem a oferta de atualização em
**Configurações ▸ Atualizações**.

<a id="segunda-tela"></a>
## 📺 Segunda tela: Cardputer ADV

Um TFT SPI opcional de 2,8" (a placa vermelha comum **“2.8\" TFT SPI 240x320” V1.1 / V1.2**: controlador ILI9341 ou
ST7789V, touch resistivo XPT2046) no conector EXT 2×7 do ADV. Com a fiação abaixo não é preciso configurar nada: o
firmware encontra o módulo na inicialização (lendo o chip de toque) e, sem nada ligado, nada muda. O Cardputer
original não aceita: G5/G6 fazem a varredura do teclado dele.

<div align="center">
<img src="docs/external-display-schematic.svg" width="900" alt="Esquema elétrico: conector EXT do Cardputer ADV ligado ao módulo TFT de 2,8 polegadas" />
<br><sub>O esquema elétrico: alimentação, o barramento SPI compartilhado com o microSD, o LCD, o controlador de toque e
o que acontece no modo de espera.</sub>
<br><br>
<img src="docs/external-display-wiring.svg" width="900" alt="Mapa fio a fio do conector de 14 pinos do display até o conector EXT 2x7 do Cardputer ADV" />
<br><sub>Fio a fio, uma cor por fio, com o conector EXT visto de cima.</sub>
</div>

| Pino do módulo | Pino EXT do ADV | GPIO | |
|---|---|---|---|
| VCC | 6 (5VOUT) | — | o regulador do módulo gera 3,3 V |
| GND | 4 | — | |
| CS | 13 | G5 | seleção do LCD |
| RESET | 12 | G13 | |
| DC | 14 | G15 | |
| SDI (MOSI) | 9 | G14 | compartilhado com o microSD |
| SCK | 7 | G40 | compartilhado com o microSD |
| LED | 1 | G3 | luz de fundo, ligada e dimerizada pelo firmware |
| SDO (MISO) | — | — | **deixe livre** |
| T_CLK | 7 | G40 | mesmo pino do SCK |
| T_DIN | 9 | G14 | mesmo pino do SDI |
| T_DO | 11 | G39 | o único fio no MISO |
| T_CS | 5 | G6 | **obrigatório**: é assim que o módulo é detectado |
| T_IRQ | 3 | G4 | opcional (lido por polling quando não ligado) |
| SD_* | — | — | não ligue: o cartão do próprio Cardputer continua sendo o do sistema |

**Antes de ligar:** faça a fiação com o Cardputer **desligado**, deixe o jumper **J1** do módulo **aberto** (fechado,
os 5 V vão direto ao painel e o chip de toque joga 5 V no G39) e localize primeiro o pino 1 do EXT (G3): um conector
invertido troca 5 V e GND.

**Toque.** Toques, arrastos e toques longos agem sobre o que está embaixo do dedo: toda tela tem um Voltar do
tamanho de um dedo, listas rolam arrastando, os jogos são jogados no painel. Calibre uma vez em **Configurações ▸
Segunda tela ▸ Calibrar toque** (quatro alvos).

**Economia de energia.** Quando o Cardputer entra em espera (tempo ocioso, Central de Controle ou `display off` pela
web), o G3 apaga a luz de fundo do painel, as duas telas recebem o comando de dormir e o controlador de toque fica em
baixo consumo com a interrupção armada: um toque no painel acorda tudo. Com o fio do LED no 5VOUT em vez do G3 a luz
não apaga; o painel só é pintado de preto.

**Configurações ▸ Segunda tela** (só ADV): modo (as duas telas / só o Cardputer / só o painel), home grande, apps
grandes, velocidade (10–40 MHz), orientação, brilho, toque, calibração. Referência completa, com outras pinagens e
solução de problemas: [docs/external-display.md](docs/external-display.md).

<a id="loja"></a>
## 🛒 Loja: apps C nativos e apps web

A **Loja** é o segundo item da Home, logo depois da ANIMA. Primeiro ela pergunta qual canal abrir:

| Canal | O que você recebe | Roda em | Instala em |
|---|---|---|---|
| **C nativo** | Jogos e ferramentas escritos em C/C++, com o mesmo visual e as mesmas teclas dos apps embutidos, e layout nativo para a segunda tela | no próprio Cardputer | o cartão SD (`/sd/apps/<id>/`), carregado na flash quando você abre |
| **Apps web** | Apps para o **NucleoOS Web**, o desktop de navegador que o Cardputer serve | no navegador de quem abre (0 de RAM do aparelho) | o cartão SD, servido ao sistema web |

- **Seguro por construção.** Só o mantenedor publica, com uma chave offline. O aparelho só instala o que confere com
  as chaves compiladas no firmware, e um módulo nativo é verificado de novo a cada abertura: um cartão editado nunca
  roda código que a Loja não assinou. Todo app nativo é recompilado byte a byte a partir do código público do NucleoOS.
- **Firmware menor, mais apps.** Os apps nativos ficam no cartão, não no firmware: só os jogos liberaram cerca de
  480 KB, e apps novos saem sem uma nova versão do firmware.
- **Atualização em um toque**, pelo Cardputer ou pelo sistema web: a mesma loja, as mesmas assinaturas, conferidas
  também pelo navegador.

Repositório e catálogo da Loja: [indecenti/nucleoos-cardputer-store](https://github.com/indecenti/nucleoos-cardputer-store)
· formato e regras: o `docs/` de lá · como funcionam os apps nativos: [docs/native-apps.md](docs/native-apps.md).

---

## ⭐ Recursos principais

### 🧠 ANIMA: uma assistente que usa o melhor cérebro que você tiver

A ANIMA responde a partir de uma escada de motores e mostra qual respondeu:

- **Fatos e comandos primeiro, com exatidão.** Ações do aparelho, bateria, Wi‑Fi, armazenamento, hora, datas,
  conversões de unidade e contas são respondidos com exatidão. Um modelo nunca inventa esses valores.
- **O modelo do seu próprio PC.** Ollama, LM Studio, llama.cpp ou Jan no computador de onde você navega: gratuito,
  offline e sem chave. Configure em **Configurações ▸ IA / Modelos ▸ Modelos no seu PC**.
- **Um modelo na nuvem, se você adicionar uma chave:** Claude, Groq, Grok (xAI) ou Gemini.
- **No navegador.** Um LLM completo em WebGPU (Qwen3‑1.7B) ou o próprio cérebro do Cardputer compilado para
  **WebAssembly**, offline e privado.
- **No Cardputer, sempre.** Um motor de linguagem natural offline de verdade, em C: uma cascata de recuperação, um
  grafo de conhecimento, matemática e unidades. Ele é ancorado por construção: nunca inventa um fato que não tem.

Há dois modos. **Auto** usa o melhor motor disponível. **Privado** remove todo passo pela internet, então nada sai da
sua máquina.

<div align="center">
<img src="docs/screenshots/settings-pc-models.png" width="640" alt="Configurações ▸ IA / Modelos ▸ Modelos no seu PC" />
<br><sub>Configurações ▸ IA / Modelos ▸ <b>Modelos no seu PC</b>: o que roda neste computador, um modelo por tarefa, quem responde primeiro.</sub>
</div>

**O agente da ANIMA funciona como um pequeno OpenCode nos seus arquivos.** Ele recebe uma pasta de trabalho no SD e
ferramentas para ler, buscar, editar (com diff e a sua aprovação), criar arquivos e rodar JavaScript. Também tem um
shell parecido com o do Linux (`ls`, `cat`, `grep`, `sed`, `node ARQUIVO.js`). Ele roda no modelo do seu PC ou numa
chave de nuvem. O Cardputer só guarda os arquivos e nunca faz inferência.

**🔊 Voz offline.** A ANIMA *fala*: um motor de TTS concatenativo (italiano e inglês no original, e português neste
fork) que pré‑grava no PC um vocabulário finito e ancorado e cola os trechos certos na hora: voz natural, ~zero de
RAM, totalmente offline. Ela diz a hora exata ao minuto, confirma aberturas (“Abrindo a música”), informa o estado
do aparelho e avisa educadamente *“leia na tela”* para o que não deve ser lido em voz alta. Veja
[`docs/tts.md`](docs/tts.md).

### 🤝 IA de verdade, integrada em todo o sistema

Quando você *quer* um modelo de ponta, o NucleoOS o integra de forma limpa:

- **Copiloto em todo o sistema**: chame a IA de qualquer lugar com `Ctrl/⌘ + Espaço`.
- **Provedores plugáveis**: Claude, Groq, Grok (xAI), Gemini. Configure um (ou vários, com troca automática). As
  chaves ficam **no SD do aparelho, nunca em log**, e o navegador fala direto com o provedor: o Cardputer não fica no
  meio.
- O **Chat de IA** fala direto com o modelo que você escolher; a **ANIMA** escolhe o melhor motor a cada turno.

- **ANIMA Code**: um runtime JavaScript isolado no navegador com uma API `os.*` (`os.fs`, `os.http`, `os.anima`,
  `os.notify`): escreva um script, clique em Run e controle o sistema.
- **ANIMA Chat / Agentes**: chat de vários turnos e um runtime multiagente no navegador para trabalho online.

### 🎙️ Voz, transcrição e gravação remota

- **Transcrição no aparelho**: fale no microfone do Cardputer e veja o texto aparecer, por um modelo Vosk em WASM.
  Italiano e inglês (e português neste fork), resumo, tela desligada enquanto escuta para poupar bateria.
- **Grave e transcreva em tempo real, a distância**: comece uma gravação no Cardputer e transmita ao vivo para o
  NucleoOS Web (`/api/rec/stream`), com transcrição em tempo real na tela grande.
- **Espectro do microfone**: um analisador FFT ao vivo na tela nativa.

### 🎨 Geração e edição de imagens offline (WebGPU)

O **Paint** é um editor de imagens completo (camadas, ferramentas, ajustes, transformações) com um **lado de IA
(Atelier)** que gera e manipula imagens **localmente via WebGPU**, sem servidor: *“crie um ícone de gato”*, *“remova
o fundo”*, *“brilho +20”*, *“gire para a direita”*. Os pixels pesados ficam na GPU do navegador; o aparelho só guarda
o resultado.

### 🎮 Jogos: um console de bolso

**Costellazioni** é o carro‑chefe: um jogo espacial com combate em 6 graus de liberdade em sistemas estelares
procedurais, uma galáxia para atravessar (mapa, saltos, atracação, faróis para reacender) e mundos onde pousar, com
descida contínua da órbita, oito biomas e descobertas. Tem 66 pinturas e uma trilha original de quatro faixas. Roda
no navegador; uma versão nativa roda no próprio Cardputer.

| | | |
|---|---|---|
| ![Costellazioni: combate num sistema estelar procedural](docs/screenshots/costellazioni-combat.jpg) | ![Costellazioni: pouso ao lado de uma descoberta num mundo desértico](docs/screenshots/costellazioni-world.jpg) | ![Costellazioni: o mapa da galáxia, planejando um salto](docs/screenshots/costellazioni-galaxy.jpg) |

- **Console nativo** no Cardputer, todos com o mesmo visual de console, em vários idiomas, com pacotes de som:
  - multijogador por **ESP‑NOW**, sem roteador: Pong, Snake Duel e Tank Duel;
  - Brawl (beat‑'em‑up), Hordes, Cardler (mini‑RPG), Pinball, Reactor e Tanks (artilharia);
  - Poker, Slots, Yahtzee e Dados;
  - o novo **Sand Garden**, um jogo de combinar.

  Cada jogo reinicia num heap novo para ter o máximo de RAM.
- **Emulação nativa de Game Boy e Game Gear**: núcleos próprios (sem truques de PSRAM, fiéis ao cartucho, saves com
  bateria), rodando suas próprias ROMs direto do SD, sem navegador.
- **Arcade (web)**: um segundo emulador, no navegador (EmulatorJS: NES, SNES, Game Boy, GBA, Mega Drive, PC Engine),
  para quando você prefere jogar na tela grande.
- **Game Center (web)**: ponto a ponto por WebRTC com o Cardputer como sinalização: Jogo da Velha, Lig 4, Pong, para
  2 jogadores **ou contra a ANIMA** (cérebro offline, ou um LLM quando online).

### 🎬 Mídia: player nativo, MP3 e rádio

- **Player de filmes (nativo)**: toca `.nfv` v4 (quadros JPEG inteiros + som IMA ADPCM num arquivo só, ~15 fps)
  direto do SD, também no painel externo de 320x240, com busca e retomada.
- **Player de MP3 (nativo)**: atento aos dois microfones, navega por pastas, lê a duração pelos cabeçalhos.
- **Rádio (nativo)**: rádio pela internet com várias estações e um app web gêmeo.

### 🛡️ Laboratório de segurança

Um kit ofensivo/defensivo escrito do zero (apps nativos do firmware):

- **WiFi Sniffer**: captura promíscua → `.pcap` no SD, detalhamento ao vivo, radiotap, visão de radar.
- **Evil Portal**: teste de portal cativo.
- **WiFi Attacks**: deauth/beacon/probe.
- **BLE Suite**: varredura / spam / iBeacon.
- **Ethernet (W5500)**: ataques L2/L3 em MACRAW pelo barramento do SD.
- **Payloads / BadUSB**: executor de DuckyScript com modo de simulação.
- **USB Keyboard PRO**: injeção HID, macros, layouts IT/US.

### 🧰 Instrumentos de bolso (sensores nativos)

Transforme o Cardputer num canivete suíço: **Lanterna**, **Nível de bolha**, **Transferidor** (goniômetro),
**Alarme** (sirene armada por microfone + acelerômetro + IMU), **Pedômetro**, **Espectro do microfone**, além de
Relógio, Calendário, Clima, Controle IR, QR e PixelFix.

### 💻 SSH: um terminal evoluído, dos dois lados

Um terminal SSH de verdade que chega aos seus servidores por uma pequena **ponte** auto‑hospedada (sem terceiros),
disponível como **app nativo** no Cardputer *e* como **terminal web** completo, com hosts salvos.

### 🗂️ Uma estação de trabalho de desktop no navegador

File Commander, Planilha (SUMIFS/VLOOKUP/IF com copiloto da ANIMA), Monitor do sistema ao vivo, Central de Controle
nas Configurações, Bloco de notas, Calculadora, DOSBox e mais: janelas de verdade, tela cheia, arrastar e soltar,
menu Iniciar e busca.

---

<details>
<summary><b>📸 Mais capturas de tela</b>: apps do shell web</summary>

| | |
|---|---|
| ![ANIMA: assistente offline no navegador](docs/screenshots/anima.png) | ![Chat de IA: Claude/Groq/Grok/Gemini](docs/screenshots/groq-chat.png) |
| ![Transcrição: microfone do aparelho](docs/screenshots/dictation.png) | ![SSH: terminal pela ponte auto‑hospedada](docs/screenshots/ssh.png) |
| ![Planilha com copiloto da ANIMA](docs/screenshots/spreadsheet.png) | ![Configurações: Central de Controle](docs/screenshots/settings.png) |

</details>

---

## 🔬 Inovações por baixo do capô

Os apps são a superfície. Estas são as apostas de engenharia que os tornam possíveis num microcontrolador sem PSRAM,
e várias não têm equivalente em outros firmwares do Cardputer.

### Uma assistente, três runtimes, comprovadamente idênticos

O cérebro offline da ANIMA é escrito uma vez em C e roda em **três** lugares, certificado a dar **a mesma resposta**
em cada um:

1. **No ESP32‑S3** (o firmware do aparelho).
2. **No seu navegador**: o mesmo C **compilado para `wasm32`** (`anima-local.wasm`), então a assistente do shell web é
   offline e privada, não uma reimplementação.
3. **No harness de host do PC** (`anima.exe`), para desenvolvimento e testes.

Um **gate de paridade** passa cada pergunta pelo módulo WASM *e* pelo build nativo e exige respostas idênticas byte a
byte, e recusa um WASM que não foi recompilado a partir dos fontes atuais do firmware; um **gate de contrato web**
fixa cada campo JSON que a interface do chat lê. A cascata em WASM não é um port: é literalmente o C do aparelho
(`firmware/components/nucleo_anima`), recompilado.

### A cascata de recuperação: o barato primeiro, honesta sempre

```
pergunta → L0  intenção/FAQ por texto                 (<1 ms, sem SD, sem modelo)   70–90% do uso
           L1  busca binária de Hamming (popcount)    + reordenação int8, com limiar de confiança
           L2  costura de trechos MOSAICO: literal, com fonte (nunca inventado)
           L3  LLM na nuvem opcional; senão, um "não sei" honesto
```

- **Amigável a quem não tem FPU:** embeddings **binários** Matryoshka (~64 B), distância = `popcount(a XOR b)`:
  matemática inteira pura, ideal para um MCU. A reordenação int8 só para os poucos sobreviventes (~dezenas de KB por
  pergunta).
- **Pré‑busca especulativa Matryoshka:** o prefixo de 16 dimensões do embedding pré‑carrega os prováveis grupos no SD
  no núcleo 1 enquanto o núcleo 0 termina a codificação completa: o cálculo se sobrepõe ao armazenamento lento.
- **Abstenção por evidência (estilo CRAG):** confiança alta → resposta congelada, média → costura, baixa → recusa.
  Ela **nunca alucina um fato que não consegue sustentar.** Dados ao vivo (hora/bateria/arquivos) sempre ignoram o
  cache.
- **Núcleo neuro‑simbólico:** raciocínio hiperdimensional HDC/VSA (associação por XOR/popcount) + um motor dedutivo
  de KGE por permutação (inverso/transitivo/vários saltos) + um solucionador de matemática/física/vetores, com um modo
  que carrega a prova (NSPCG) no Forge.

### O navegador faz o trabalho pesado (WebGPU + WASM)

O aparelho nunca transmite pesos de modelo. Tudo o que é pesado vai para o navegador: **WebLLM (Qwen3‑1.7B, WebGPU)**
e **wllama** para LLM de verdade no navegador (Forge), **o Ollama / LM Studio do seu PC** para o agente e o chat
(acessados pelo navegador, nunca pelo Cardputer), **ONNX Runtime Web** para difusão offline (Paint/Atelier),
**ffmpeg.wasm** para o Video Studio.

### Motor de ataque por Ethernet cabeada (módulo W5500)

Um motor **MACRAW** de camada 2/3 escrito do zero (`nucleo_eth`) sobre um driver W5500 próprio: não é lwIP nem código
do Bruce. Inclui um **cliente DHCP feito à mão** que se configura sozinho *sem pilha TCP/IP*, identificação de
fabricante por OUI e operações que a maioria dos firmwares não tem:

| Operação | O que faz |
|---|---|
| Scan | varredura ARP da sub‑rede → lista de hosts + fabricante por OUI |
| MITM | ARP spoof bidirecional vítima↔gateway, **reversível** (cura os caches que envenenou ao sair) |
| Poison / Starve / Flood | caos ARP · esgotamento do pool DHCP · inundação da tabela CAM do switch |
| Rogue DHCP | responde aos DISCOVERs como servidor falso → redireciona roteador/DNS para nós |
| PCAP | captura passiva num `.pcap` no SD |

Disciplina rígida: **exatamente uma operação por vez** (iniciar = parar, curar e liberar a anterior), todo ataque
ativo **exige consentimento**, e o app declara `NX_NET_APP` para o sistema recuperar ~70 KB enquanto ele roda.

### Sobrevivendo com 512 KB e sem PSRAM (a mágica de verdade)

Todo recurso exigente respeita um conjunto apertado de truques de RAM, validados no hardware:

- **Modo exclusivo**: apps recuperam ~47 KB desligando httpd + ANIMA L1 + mDNS durante seu uso.
- **Heap na entrada**: buffers grandes são alocados ao entrar, nunca em `.bss` (um vetor em `.bss` já travou o ADV).
- **Boot solo**: uma marca no RTC pula httpd/mDNS para ter heap contíguo; cada jogo reinicia num heap novo.
- **OTA sem canvas**: o framebuffer se libera sozinho em repouso para que a atualização encontre heap contíguo
  suficiente.
- **Árbitro de trabalho pesado**: uma sessão TLS por vez (503 se ocupado); ANIMA online liberada só com
  `bloco ≥ 18 KB E livre ≥ 40 KB`. Além de FATFS com setor de WL de 512 (+33 KB), tela desligada com foco na web
  (+32 KB) e trabalho de tamanho com `-Os` e Lua enxuta (≈37 % de flash livre).

### Encanamento da plataforma

- **Binário universal**: uma imagem detecta em tempo de execução o **Cardputer original ou o ADV** (mapa do teclado
  TCA8418 + HAL do codec ES8311): compile uma vez, envie para os dois.
- **Barramento de eventos**: deltas de estado só de acréscimo → observabilidade, replay, desfazer; estende‑se a pares
  **ESP‑NOW** (o modelo swarm/CHORUS) para área de transferência compartilhada, multijogador e malha de sensores.
- **Apps com vários runtimes**: `web` (zero RAM do aparelho) · `vm` (bytecode isolado no aparelho) · `service` (C
  nativo) · `elf` (exige PSRAM). Instale e rode sem regravar o firmware.
- **Serviços do sistema**: manifestos com permissões, OTA A/B com volta automática, pareamento por PIN + cookie,
  sincronização incremental do SD (nunca `/MIR`), idiomas ao vivo em todo o sistema (IT/EN/DE/ES/FR no original,
  **pt-BR** neste fork, e **chinês simplificado (简体中文) no sistema web**), notificações, um **controle de microfone**
  e um **controle de downloads** em todo o sistema (Web Locks) e um controle de atualização do service worker.
- **Gates de robustez** rodam antes de qualquer build sair:
  - uma suíte de regressão da ANIMA com 129 gates que compila o C real do firmware no PC: 0 invenções nas baterias
    de estresse de alucinação, adversariais e metamórficas;
  - cerca de 850 testes de unidade;
  - cerca de 300 testes ponta a ponta no Chrome real, na CI a cada push;
  - um teste rápido de cada app web num Cardputer de verdade.
- **Conhecimento**: um gate de deduplicação e qualidade do corpus e um grafo de conhecimento que evolui sozinho
  (Wikipédia + LLM professor, codificador AUG‑192).

---

## 📇 Catálogo completo

### Apps nativos do firmware (rodam na tela do Cardputer)

| Categoria | Apps |
|---|---|
| **Assistente e voz** | Chat da ANIMA · Gravador (MP3, dois microfones + transcrição ao vivo) · Espectro do microfone (FFT) · Treinador de voz / palavra‑chave + aperte‑para‑falar (AVCEB MFCC·CMN·DTW) |
| **Segurança: Wi‑Fi/BLE** | WiFi Sniffer (→ `.pcap`) · Evil Portal · WiFi Attacks (deauth/beacon/probe) · BLE Suite (scan/spam/iBeacon) · Beacon |
| **Segurança: cabo/USB** | Motor L2‑L3 Ethernet/W5500 · Payloads / BadUSB (DuckyScript) · USB Keyboard PRO (HID, macros, IT/US) · **Teclado** (USB‑C + Bluetooth, ABNT2; deste fork) |
| **Mídia** | Player de filmes (`.nfv` v4 MJPEG+ADPCM, busca) · Player de MP3 · Rádio (várias estações) · Fotos |
| **Instrumentos e sensores** | Lanterna · Nível de bolha · Transferidor · Alarme (microfone + acelerômetro + IMU) · Pedômetro · Clima · Controle IR · **RemoCon** (deste fork) · QR · PixelFix · Protetor de tela |
| **Sistema** | Gerenciador de Wi‑Fi · Configurações/Tema · Relógio · Calendário · Bloco de notas · Calculadora · Monitor do sistema · Notificações · Info · E‑mail (SMTP) · Terminal SSH |
| **Jogos** (da Loja ▸ C nativo) | Constellations (espaço 3D) · Pong · Snake Duel · Tank Duel · Tanks (artilharia) · Brawl · Pinball · Reactor · Hordes · Cardler · Poker · Slots · Dados · Yahtzee · Sand Garden (combinar), pelo carrossel **GameFront** |
| **Emulação** | Game Boy (núcleo próprio, saves com bateria) · Game Gear (máquina VDP/PSG/mapper própria + um núcleo Z80 de licença permissiva), ambos nativos, fiéis ao cartucho, sem navegador |

### Apps web (NucleoOS Web: servidos pelo aparelho, rodam no seu navegador)

| Categoria | Apps |
|---|---|
| **IA** | ANIMA (WASM offline, modelos do seu PC ou a nuvem) · Chat de IA (Claude/Groq/Grok/Gemini) · ANIMA Code · Agentes · Copiloto em todo o sistema |
| **Criatividade e mídia** | Paint + Atelier (geração de imagens em WebGPU) · Video Studio · Player de vídeo · Media Player · Rádio · QR |
| **Produtividade** | File Commander · Planilha (+ copiloto da ANIMA) · Bloco de notas · Calculadora · Calendário · Tarefas · Gerenciador de arquivos compactados · E‑mail |
| **Desenvolvimento e sistema** | Code Runner · Terminal · SSH · Monitor do sistema · Configurações · Visualizador de logs · Atualizações · Lixeira · Ajuda · Navegador |
| **Ferramentas** | Clima (Open‑Meteo, direto do navegador: previsão de 6 dias, geolocalização, cache offline) · Autenticador (2FA TOTP/HOTP, JS puro, funciona em http na rede local e offline) · Conversor de unidades · Passkeys · Controle IR |
| **Pessoal** | Contatos (agenda: e‑mail/telefone/empresa, favoritos, exportação vCard, Compor no E‑mail com um clique) · Tarefas · Calendário · Miei Fatti |
| **Voz** | Transcrição (Vosk WASM, no aparelho + remota em tempo real) · Gravador · Gerenciador de voz · Ditado |
| **Jogar e conectar** | Costellazioni (jogo espacial 3D) · Game Center (WebRTC P2P) · Jogos · Arcade (EmulatorJS: NES/SNES/Game Boy/GBA/Mega Drive/PC Engine, suas próprias ROMs, no navegador) · DJ · DOSBox (+ DOS Importer) · Nearby · Swarm |

> O **shell de desktop** é uma estação de trabalho de verdade: encaixe de janelas (arrastar até a borda + Snap
> Layouts), Alt‑Tab, uma busca global que também pergunta à ANIMA, área de transferência com histórico, Central de
> Notificações (Não Perturbe + horário de silêncio), **widgets** na barra de tarefas (clima + pulso do aparelho +
> recentes, tudo no navegador), **luz noturna**, **tela de bloqueio** (Ctrl+Alt+L) e uma folha de atalhos de teclado,
> tudo em vários idiomas, custando ~0 de RAM ao Cardputer.

> Os apps têm permissões declaradas (cada um declara as suas) e a maioria custa **~0 de RAM do aparelho**: são
> arquivos no SD, desenhados pelo seu navegador. O registro é a única fonte de verdade.

---

## Restrições rígidas: projetar para o pior caso

O NucleoOS existe *porque* o hardware é brutal. Todo recurso acima respeita isto:

- **RAM:** ~512 KB de SRAM, **sem PSRAM**. Heap em execução de ~18 KB; o inimigo real é a *fragmentação* (httpd +
  ANIMA L1 + workers), não a CPU. Dados pesados nunca carregam no aparelho: vão para o navegador. Apps nativos alocam
  ao entrar e rodam em **modo exclusivo**. Veja [`docs/memory-budget.md`](docs/memory-budget.md).
- **Bateria:** ~120 mAh: energia é um recurso de primeira classe; todo serviço declara um orçamento.
- **Armazenamento:** microSD (sistema na flash, conteúdo no SD).
- **Recuperação primeiro:** atualizações OTA falham sem travar o aparelho (partições A/B + volta automática).

---

## Organização do repositório

```
docs/              Especificações de engenharia, um arquivo focado por tema (a base de conhecimento)
schemas/           JSON Schemas: contrato compartilhado entre firmware / PC / Android
registry/          Registro vivo do sistema: apps instalados, configurações, associações de arquivo, apelidos de apps
apps/<id>/www/     Uma pasta por app web + manifest.json (.gz gerado ao lado)
firmware/          Firmware ESP‑IDF (boot, SD, registro, HTTP/WS, núcleo C da ANIMA, apps nativos)
  components/nucleo_app/      Apps nativos do firmware (jogos, segurança, sensores, mídia, ANIMA…)
  components/nucleo_anima/    Motor de linguagem natural offline (cascata, grafo, raciocínio)
  components/nucleo_tts/      Voz offline concatenativa
  components/nucleo_*/        HAL + serviços: codec, httpd, ir, ble, smtp, …
web/shell/         PWA do shell de desktop (janelas, barra de tarefas, Iniciar, copiloto, idiomas)
tools/             Ferramentas de desenvolvimento: validadores, harness de host, simulador do aparelho, deploy/flash
tools/anima-host/  Gates de host: compilam o C REAL do firmware e o rodam no PC
tools/webos-pt/    Camada pt-BR do sistema web (catálogos, patches revisados e seus hashes; deste fork)
```

Comece qualquer orientação pelo arquivo relevante em `docs/*.md` (em inglês): ele é a fonte da verdade, uma
especificação por tema; prefira lê‑lo a deduzir de novo. Primeira vez aqui? Leia o **[Guia do usuário](docs/user-guide.md)**
ou o **[índice da documentação](docs/README.md)**.

---

## Desenvolver sem gravar: host primeiro, flash por último

Não itere gravando o firmware. O harness de host compila o C **real** da ANIMA e o roda no PC:

```bash
npm run anima -- --pt "que horas são"  # uma pergunta       npm run anima       # REPL interativo
npm run anima:pt                        # gate do português (comandos, respostas, sugestões, tradução)
npm run anima:gate                      # o gate de regressão da ANIMA (verde antes de qualquer flash)
npm run validate                        # schema de registro + manifestos + referências cruzadas
```

Para trabalho no sistema web, rode o **simulador do aparelho**, sem dependências: ele imita a API do aparelho e serve
todos os apps, então você verifica no navegador sem tocar no hardware:

```bash
node tools/serve-shell.mjs             # http://localhost:5599
```

**Matriz de verificação:** `npm run validate` · `i18n:gate` · `gz:check` · `icons:gate` · `gen:api:check` ·
`anima:gate` · `test:all` · `node tools/web-e2e/run.mjs` (E2E no navegador). Veja [`docs/debugging.md`](docs/debugging.md).

> Todas as capturas de tela acima vêm desse simulador rodando o shell e os apps **reais**.

---

## Build e release: OTA + API web, não serial

```bash
tools/release.ps1                      # gate → build → monta deploy/sd → sincroniza o SD → OTA
tools/release.ps1 -FirmwareOnly        # só o firmware, por OTA
tools/flash.ps1                        # gate → build → grava por USB (só para confirmar)
```

Os scripts `.ps1` são do Windows. No macOS e no Linux, use `idf.py build` e o `esptool`, como em
[Como ter os recursos deste fork](#este-fork).

É **um binário universal**: a mesma imagem detecta a placa (Cardputer original ou ADV) em tempo de execução, então
você compila uma vez e distribui. O gate da ANIMA precisa estar verde antes de qualquer flash
(`release.ps1`/`flash.ps1` exigem isso). Veja [`docs/releasing.md`](docs/releasing.md).

Aparelhos instalados descobrem novas versões sozinhos: o shell consulta o GitHub **deste fork** uma vez por dia (pelo
**navegador**; o aparelho nunca acessa a internet) e oferece uma atualização verificada de um clique em
**Configurações ▸ Atualizações**. Para lançar uma versão do fork: suba `firmware/version/VERSION` (e package.json,
CITATION.cff, CHANGELOG.md), crie a tag `vX.Y.Z`, e depois que o CI publicar, assine neste Mac com
`python3 tools/fw_sign.py release vX.Y.Z` (a chave privada fica em `~/.nucleo`).
Design: [`docs/update-check.md`](docs/update-check.md).

---

<a id="uso-legal"></a>
## Uso legal e responsável

O NucleoOS traz ferramentas que sondam e transmitem em redes de rádio e cabeadas: Wi‑Fi (varredura, deauth, beacon,
Evil Portal, captura de PMKID / handshake), Bluetooth LE, Ethernet L2/L3, infravermelho, USB‑HID (BadUSB /
DuckyScript) e a pilha de chave de segurança FIDO. **Elas existem só para testes de segurança autorizados, pesquisa e
educação.**

- Use‑as **somente** em redes, aparelhos e sistemas que você **possui** ou que tem **permissão explícita por
  escrito** para testar.
- **Só você é responsável** por cumprir todas as leis e normas aplicáveis na sua jurisdição, incluindo as de uso
  indevido de computadores, acesso não autorizado, interceptação, privacidade e uso do espectro de rádio. Transmissão
  ativa (quadros de deauth, inundação de beacon / BLE, infravermelho, interferência) é restrita ou ilegal em muitos
  países, inclusive no Brasil.
- **Não** use o NucleoOS contra nenhuma rede, aparelho ou conta sem autorização. Acesso, interrupção ou interceptação
  não autorizados podem ser crime.
- O NucleoOS é fornecido **“como está”, sem garantia de nenhum tipo**. Os autores e colaboradores **não se
  responsabilizam** por danos, perdas ou consequências legais do seu uso ou mau uso.

Ao compilar, gravar ou rodar este software, você aceita estes termos e concorda em usá‑lo de forma legal e ética. Se
não concordar, não o use.

## Contribuições

Contribuições para o projeto vão para o repositório original: veja [`CONTRIBUTING.md`](CONTRIBUTING.md) (inclui um
acordo curto de contribuidor para o projeto poder manter licença dupla). Componentes de terceiros e suas licenças
estão em [`THIRD_PARTY.md`](THIRD_PARTY.md).

## Licença

**[PolyForm Noncommercial License 1.0.0](LICENSE)**: livre para qualquer uso **não comercial** (pessoal, estudo,
pesquisa, hobby, sem fins lucrativos, educação, governo). A licença em vigor é o texto em inglês do arquivo `LICENSE`.

**Uso comercial ou em produção exige uma licença comercial paga do autor original**: veja
[`COMMERCIAL.md`](COMMERCIAL.md). Contato: **niki070585@gmail.com**.

© 2026 indecenti. Todos os direitos reservados. O NucleoOS inclui componentes de terceiros sob as licenças deles
(algumas copyleft): veja [`THIRD_PARTY.md`](THIRD_PARTY.md). Os dados em português deste fork (dicionários, lista de
palavras) seguem as licenças das fontes, listadas no mesmo arquivo.

---

<div align="center">

**NucleoOS**: um microcontrolador que se acha uma estação de trabalho.
Feito para o M5Stack Cardputer · projetado para o pior caso.

</div>
