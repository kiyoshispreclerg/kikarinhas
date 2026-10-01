# Kikarinhas — plano

Avatares do chat andando numa janela transparente para o OBS capturar, no
espírito do Stream Avatars e do Desktop Ponies, só que nativo para Linux/X11
e leve: C, Xlib, Cairo e Pango, com GTK2 apenas no configurador.

## Objetivos

- Janela ARGB que o OBS captura com transparência (Xcomposite).
- Um avatar por pessoa do chat, andando à toa, reagindo a mensagens e comandos.
- Chat: **YouTube primeiro**; depois Twitch e Odysee.
- Ler os avatares do Stream Avatars já instalados pelo usuário.
- Opcional: camadas HTML num processo à parte, para aposentar alguns
  obs-browser.

Fora do escopo por enquanto: Wayland, Windows, edição de avatares.

## Arquitetura

```
kikarinhas          (C11, Xlib + Cairo + Pango; laço único com poll())
 ├─ janela ARGB 32 bits ─ modo OBS: janela normal, capturada pelo Xcomposite com alfa
 │                      └─ modo desktop: sobreposição em tela cheia, clique atravessa (XShape)
 ├─ motor de sprites  (atlas, animação, máquina de estados, pulo/gravidade, chão)
 ├─ chat              (YouTube, depois Twitch e Odysee) → eventos internos
 ├─ socket unix       ◄── bridges externas (JSON por linha) e comandos do configurador
 └─ memória compart.  ◄── kikarinhas-web (processo à parte, opcional)
kikarinhas-config   (GTK2: edita o .ini e manda "reload" pelo socket)
```

### Princípios

- Uma thread só; tudo entra no mesmo `poll()`: fd do X, timerfd do quadro,
  sockets do chat (via libcurl multi), socket unix.
- Desenho numa `cairo_image_surface` ARGB32, enviada com `XShmPutImage`
  só nas regiões sujas. Pixel art usa `CAIRO_FILTER_NEAREST`
  (respeitando o `bilinearFilter` do SA).
- 30 fps por padrão; quando nada muda, nada é enviado ao servidor X.
- Plataforma que quebrar (APIs não oficiais) não derruba o resto: cada
  conector é isolado atrás da mesma interface de eventos, e o socket unix
  aceita bridges externas em qualquer linguagem.

### Dependências

| Biblioteca | Uso |
|---|---|
| libX11, libXext (MIT-SHM, Shape) | janela, envio de quadros, clique atravessando |
| cairo, pangocairo | desenho, nomes e balões de fala |
| libcurl (multi) | HTTPS do YouTube/Odysee, integrado ao `poll()` |
| OpenSSL | TLS do IRC da Twitch e do websocket da Odysee |
| cJSON (embutido) | JSON do SA e das APIs |
| miniz (embutido) | ler os zips do SA/workshop |
| GTK2 | só o `kikarinhas-config` |

Build com Makefile + `config.mk` + pkg-config, no mesmo esquema do kisnitch.

## Compatibilidade com o Stream Avatars

Formato observado (não descompilamos nem copiamos código do SA; só lemos os
dados do próprio usuário para interoperar, e não distribuímos as artes):

- **Pacote** (`premade/*.zip`, `StreamingAssets/*.zip`, workshop): `data.txt`
  (JSON) + `avatars/<nome>.png` + `gear/<conjunto>/<peça>.png` +
  `backgrounds/*.png`. Raiz do JSON: `avatar`, `gear`, `backgrounds`,
  `backgroundObj` (e `playerClass`/`boss` nos pacotes de batalha).
- **Instalação do usuário** (Proton):
  `steamapps/compatdata/665300/pfx/drive_c/users/steamuser/AppData/LocalLow/ClonzeWork/Streaming Avatars/data/`
  com `avatars/`, `gear/`, `sounds/`, `backgrounds/` e
  `streamavatars_json.txt` (UTF-8 **com BOM**), que agrega `avatarData`,
  `gear`, `backgroundLevel`, `userData`, `soundData` e configurações.
- **Spritesheet**: grade de células `width`×`height`. Cada **linha** é uma
  animação, na ordem do JSON (`idle`, `walk`, `sit`, `stand`, `jump`,
  `custom1..N`), contando só as que têm `frameData` não vazio; cada
  **coluna** é um quadro. Conferido em applejack (96×96, PNG 960×768 →
  10×8, 8 animações) e littlewalker (32×32, PNG 192×160 → 6×5).
  *Hipótese a validar em todos os avatares instalados (fase 1).*
- **Animação**: `framesPerSecond`, `animationLoops`, `loopCount`,
  `holdLastFrame`, `returnsToIdle`, `targetsUser`, `targetDistance`,
  `customName` (ex.: `custom1` = "dance").
- **Avatar**: `pixelsPerUnit` (escala), `colliderHeight`, `moveSpeed`,
  `bilinearFilter`, `CanUseGear`.
- **Paletas**: `mainPalette.colors` → `swappablePalettes.<nome>.colors`,
  troca de cor exata.
- **Gear**: `gearPivot`/`uniquePivot` por quadro, `zIndex`,
  `isUniqueZindex`, `flipsWithAvatarX`, `isAnimated`, `FPS`, `PPU`.
- **Usuários**: chaves de `userData` são o id da plataforma com prefixo;
  `Y` + id do canal (`YUC...`) para o YouTube, id numérico para a Twitch.
  Dá para importar quem usava qual avatar.

**Segurança:** `streamavatars_json.txt` tem `loginDetails` com tokens. O
importador ignora esse campo e nunca o copia nem o registra em log.

## Plataformas de chat

### YouTube (primeira)

Sem chave de API, pelo mesmo caminho do chat em pop-out (InnerTube), que é o
que pytchat/chat-downloader usam:

1. GET da página da live (`/watch?v=ID` ou `/channel/UC.../live`) e
   extração de `INNERTUBE_API_KEY`, versão do cliente e da continuação
   inicial do chat ao vivo em `ytInitialData`.
2. POST em `youtubei/v1/live_chat/get_live_chat` com a continuação;
   respeitar o `timeoutMs` devolvido (tipicamente 1–5 s).
3. Ações tratadas: `addChatItemAction` com
   - `liveChatTextMessageRenderer` (nome, `authorExternalChannelId`, texto
     e emojis em `runs`, selos de membro/moderador/dono);
   - `liveChatPaidMessageRenderer` (Super Chat);
   - `liveChatMembershipItemRenderer` (membros novos).

Alternativa oficial: YouTube Data API v3 (`liveChatMessages.list`) com chave
do usuário. A cota diária padrão não aguenta uma live longa com sondagem
frequente, então fica só como reserva.

Risco: InnerTube muda sem aviso. O conector fica isolado num arquivo e com
testes sobre respostas gravadas.

### Twitch (depois)

IRC em `irc.chat.twitch.tv:6697` (TLS), login anônimo `justinfanNNNN`, só
leitura, sem token. Tags IRCv3 (`user-id`, `display-name`, `badges`,
`emotes`). Enviar mensagens (respostas a comandos) exige OAuth: fica para
depois.

### Odysee (depois)

Chat das lives é baseado no Commentron: websocket
`wss://sockety.odysee.tv/ws/commentron?id=<claim_id>` para novos
comentários e `comments.odysee.tv/api/v2` (`comment.List`) para o histórico.
*A confirmar na fase em que for implementado.*

### Bridges externas

Protocolo no socket unix, uma linha JSON por evento:

```json
{"type":"message","platform":"youtube","user_id":"UC...","name":"Fulano",
 "text":"oi !jump","badges":["member"],"color":null}
```

Serve para plataformas que não valem ir para o núcleo e para testes
(`echo ... | socat - UNIX-CONNECT:...`).

## Comportamento dos avatares

- Entra na primeira mensagem; some após N segundos sem falar (SA usa 300);
  limite de avatares simultâneos (SA usa 30) com fila.
- Estados: idle → walk (direção e distância aleatórias) → idle/sit/stand…;
  vira o sprite conforme a direção.
- Mensagem: pulinho + balão de fala (Pango, quebra de linha, tempo
  proporcional ao tamanho do texto).
- Comandos: `!avatar <nome>`, `!jump`, `!dance`, `!hug @x`, `!attack @x`,
  `!sit`, com tempos de espera por pessoa e globais.
- Nome embaixo (Pango com contorno), cor por selo (dono/mod/membro).
- Persistência própria em `~/.local/share/kikarinhas/users.ini`
  (pessoa → avatar, paleta, gear), com importação do `userData` do SA.

## Camada HTML (opcional)

`kikarinhas-web`: processo separado com **WPE WebKit** (libwpe e backend
fdo/WPEPlatform), renderizando fora da tela com fundo transparente e
entregando quadros por memória compartilhada; o núcleo os compõe como
camadas com posição/tamanho no .ini.

Não é leve como o Cairo (≈80–150 MB por página), mas troca várias
instâncias de CEF dentro do OBS por um processo isolado e opcional.
Alternativas avaliadas: litehtml (sem JS), Servo (ainda imaturo para
embutir), CEF (é o próprio obs-browser), Ultralight/Sciter (proprietários).

## Configuração

`~/.config/kikarinhas/kikarinhas.ini`: tamanho/modo da janela, altura do
chão, escala, fps, fonte, plataformas (id da live/canal), caminho do SA,
avatar padrão, tempos de espera. O `kikarinhas-config` (GTK2) edita o
arquivo e manda `reload` pelo socket; o núcleo funciona sem ele.

## Estrutura de pastas

```
src/          núcleo (main, window, render, sprite, avatar, behavior, chat_*.c)
src/sa/       importador do Stream Avatars
config/       kikarinhas-config (GTK2)
web/          kikarinhas-web (WPE)
vendor/       cJSON, miniz
tools/        scripts (validação de spritesheets, gravação de respostas)
tests/        testes de unidade e respostas gravadas das APIs
data/         avatar padrão original, exemplo de .ini
```

## Fases

0. **Prova de conceito**: janela ARGB + Cairo desenhando um retângulo
   semitransparente animado. Validar no OBS ("Captura de janela
   (Xcomposite)" com transparência), inclusive com a janela coberta;
   validar o modo desktop com clique atravessando.
1. **Motor de sprites e importador do SA**: ler `streamavatars_json.txt` e
   zips; script em `tools/` que valida a regra linha=animação em todos os
   avatares instalados; um avatar andando com idle/walk/sit/jump.
2. **YouTube**: conector InnerTube; um avatar por pessoa, nome, balão,
   sumiço por inatividade, limite com fila. Testes com respostas gravadas.
3. **Interações**: comandos, tempos de espera, gear, paletas, persistência
   própria e importação do `userData` do SA. Super Chat/membro com reação
   especial.
4. **Socket unix e configurador GTK2**: protocolo de bridges, `reload`,
   `kikarinhas-config`.
5. **Twitch** (IRC anônimo) e **Odysee** (Commentron).
6. **kikarinhas-web** (WPE) como camadas.
7. **Extras**: fundos e sons do SA, emojis/emotes como imagem no balão,
   plugin de OBS lendo a memória compartilhada direto (sem Xcomposite),
   Wayland (layer-shell) no futuro.
