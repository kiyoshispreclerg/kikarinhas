# Kikarinhas — plano

Avatares do chat andando numa janela transparente para o OBS capturar, no
espírito do Stream Avatars e do Desktop Ponies, só que nativo para Linux/X11
e leve: C, Xlib, Cairo e Pango, com GTK2 apenas no configurador.

## Objetivos

- Janela ARGB que o OBS captura com transparência (Xcomposite).
- Um avatar por pessoa do chat, andando à toa, reagindo a mensagens e comandos.
- Chat: YouTube, Twitch, Kick e Odysee.
- Ler os avatares do Stream Avatars já instalados pelo usuário.
- Opcional: camadas HTML num processo à parte, para aposentar alguns
  obs-browser.

Fora do escopo: Wayland nativo (roda pelo XWayland), Windows, edição de avatares.

## Arquitetura

```
kikarinhas          (C11, Xlib + Cairo + Pango; laço único com poll())
 ├─ janela ARGB 32 bits ─ modo OBS: janela normal, capturada pelo Xcomposite com alfa
 │                      └─ modo desktop: sobreposição em tela cheia, clique atravessa (XShape)
 ├─ motor de sprites  (atlas, animação, máquina de estados, pulo/gravidade, chão)
 ├─ chat              (YouTube, Twitch, Kick, Odysee) → eventos internos
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
| libcurl (multi) | HTTPS (YouTube, APIs do Kick/Odysee, listas de emotes), integrado ao `poll()` |
| OpenSSL | TLS do IRC da Twitch e dos websockets do Kick e do Odysee; SHA-1/base64 do handshake |
| cJSON (embutido) | JSON do SA e das APIs |
| miniz (embutido, ainda não incluído) | ler os zips do SA/workshop |
| libdbus-1 | players MPRIS (a música tocando, `!song`) |
| GTK2 | só o `kikarinhas-config` |
| fonte de emoji colorido (Noto Color Emoji) | só ao rodar: emojis do emote wall e dos balões |

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
- **Spritesheet**: grade de células `width`×`height`. Cada **linha** é a
  posição fixa da animação (`idle`=0, `walk`=1, `sit`=2, `stand`=3,
  `jump`=4, `customN`=4+N); slots vazios mantêm a linha deles. Cada
  **coluna** é um quadro (`frameData` dá quantos). Pode haver pixels ou
  linhas sobrando. Validado nos 194 avatares instalados (`--check`).
  Todos olham para a direita.
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

### Twitch (feita, ver fase 5)

IRC em `irc.chat.twitch.tv:6697` (TLS), login anônimo `justinfanNNNN`, só
leitura, sem token. Tags IRCv3 (`user-id`, `display-name`, `badges`,
`emotes`). Enviar mensagens (respostas a comandos) exige OAuth: fica para
depois. Emotes: a tag `emotes=25:0-4,12-16/1902:6-10` dá id e posições
(em code points, converter para bytes) de todos os nativos (globais, de
inscrito, de seguidor); a imagem é
`https://static-cdn.jtvnw.net/emoticons/v2/{id}/static/dark/2.0` (PNG; com
`default` no lugar de `static` vem GIF se for animado), sem chave. Viram
`kk_emote` na mensagem. BTTV/FFZ/7TV não vêm marcados: baixar a lista do
canal pelo `room-id` (tag do ROOMSTATE) e procurar as palavras no texto;
7TV serve WebP/AVIF por padrão, pedir PNG/GIF.

### Kick (feito, ver fase 5)

O site lê o chat pelo Pusher: websocket
`wss://ws-us2.pusher.com/app/32cbd69e4b950bf97679?protocol=7&client=js&version=8.4.0`
(chave pública do app do Kick), `pusher:subscribe` em `chatrooms.<id>.v2`
(mensagens, subs, banimentos) e `channel.<id>` (Kicks). Os ids vêm de
`https://kick.com/api/v2/channels/<slug>` (`chatroom.id`, `id`, `user_id`),
que responde a clientes comuns sem login. Cada evento chega como
`{"event","channel","data"}` com `data` sendo JSON dentro de uma string.

### Odysee (feito, ver fase 5)

O chat da live é a seção de comentários do claim da live, empurrada pelo
Commentron: websocket
`wss://sockety.odysee.tv/ws/commentron?id=<claim_id>&category=<@canal>&sub_category=viewer`,
eventos `delta` (comentário novo), `viewers`, `removed`, `pinned`. Link →
claim: `resolve` no proxy do SDK (`api.na-backend.odysee.com/api/v1/proxy`,
JSON-RPC); canal → live: `api.odysee.live/livestream/is_live?channel_claim_id=`
(`Live`, `ActiveClaim.ClaimID`). O histórico (`comment.List` em
`comments.odysee.tv/api/v2`) não é usado: como na Twitch, só o que chega
depois de conectar.

### Bridges externas

Protocolo no socket unix, uma linha JSON por evento:

```json
{"type":"message","platform":"youtube","user_id":"UC...","name":"Fulano",
 "text":"oi !jump","badges":["member"],"color":null}
```

Serve para plataformas que não valem ir para o núcleo e para testes
(`echo ... | socat - UNIX-CONNECT:...`). Emotes de imagem vão em
`"emotes":[{"id","name","url","start","len"}]` e reações em
`{"type":"reaction","emoji":"❤","count":3}` (ver `src/control.h`).

### Ligar uma plataforma nova

Todo conector recebe um `kk_chat_sink` (`src/chat.h`) e só entrega eventos
neutros: `kk_chat_msg` (com emojis Unicode no próprio texto e os emotes de
imagem da plataforma em `emotes`, com URL https de um PNG (ou GIF, do qual
vale o 1º quadro) e a posição no texto) e, se houver, `kk_reaction`
(contagens agregadas, com `delay` para espalhar um lote no tempo). Dali
para a frente nada sabe de onde veio: o `emoji.c` acha os emojis no texto,
o `emotes.c` baixa e guarda as imagens (cache em disco por plataforma e id)
e o `emotewall.c` decide o que sobe.

Protocolos sobre socket próprio usam o `kk_tls` (TLS cru, como o IRC) ou o
`kk_ws` (websocket); HTTP comum vai pelo `kk_http`. No programa, uma
plataforma é uma entrada do `platform` do `main.c` (lista do `[chat]`,
validação do alvo, criação, `pollfd` e `tick`), uma chave no `config.c`
e uma linha na aba Chat do configurador (`config/sources.c`).

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

0. **Prova de conceito** *(feita)*: janela ARGB + Cairo desenhando um
   cartão semitransparente animado, MIT-SHM com dano parcial, modos obs e
   desktop (clique atravessa via Shape). Medido: ~0,8% de CPU a 30 fps
   em 1920x1080, ~25 MB de RSS. Lição: conteúdo estático (texto Pango com
   contorno, gradientes) vai para uma superfície de cache e só é copiado
   a cada quadro; redesenhar tudo custava ~10x mais.
   *Falta validar no OBS pelo usuário*, inclusive com a janela coberta.
1. **Motor de sprites e importador do SA** *(feita, exceto zips)*: leitura
   de `streamavatars_json.txt` com busca da pasta nas bibliotecas do Steam;
   `--check` valida todas as folhas (194/194 ok); avatares com idle, walk,
   sit/stand, pulo com gravidade e animações custom como emote; nome
   embaixo; dano parcial por retângulos. Medido com 30 avatares em
   1920x1080 a 30 fps: ~3,3% de CPU e ~45 MB de RSS. Lições:
   - a linha da folha é a posição fixa do slot, não a ordem dos slots
     preenchidos (gastly, muk, grimer e haru urara só batem assim);
   - todos os avatares olham para a direita;
   - pixel art fica no tamanho original e é ampliada com nearest ao
     desenhar (4x menos memória que pré-escalar; +0,5% de CPU); folhas
     com `bilinearFilter` são pré-escaladas;
   - `malloc_trim` depois do parse devolve ~17 MB do JSON.
   Pendente: pacotes .zip (premade/workshop) e plaquinhas de nome que se
   sobrepõem quando avatares ficam juntos.
2. **YouTube** *(feita)*: conector InnerTube integrado ao laço via
   `curl_multi_wait`; um avatar por pessoa (sorteado por hash do id, sempre
   o mesmo), pulo e balão a cada mensagem, Super Chat dourado, membro
   verde, nome colorido por selo, sumiço após 300 s em silêncio, limite com
   saída de quem está calado há mais tempo, nomes em duas linhas,
   `--demo-chat`. Testes com fixtures sintéticas no formato real. Testado
   numa live japonesa com ~2 msg/s: 30 avatares, ~3,6% de CPU, ~97 MB de
   RSS; reconectou sozinho após queda de DNS. Lições:
   - a primeira resposta do "Live chat" repete o histórico: é descartada;
   - tokens `invalidationContinuationData` pedem `timeoutMs` de 10 s
     porque o navegador recebe push; sem push, sondamos a cada 2,5 s;
   - mensagens em moderação chegam como placeholder e depois em
     `replaceChatItemAction`;
   - fontes CJK são mais altas: a margem do chão mede "Ág日本語".
   Não verificado ao vivo: selos (nenhum apareceu nas amostras), Super
   Chat e membros (só nas fixtures). Emojis de canal viram `:atalho:` no
   texto dos balões; no emote wall já são imagem (fase 7).
   **Reações** do YouTube (confirmado numa live em 2026-10-02): vêm em
   toda resposta do `get_live_chat` (e no `ytInitialData`), em
   `frameworkUpdates.entityBatchUpdate.mutations[].payload.emojiFountainDataEntity`:
   `reactionBuckets[]` com `totalReactions`, `duration.seconds` (sempre 1),
   `intensityScore` e `reactionsData[]` de `{unicodeEmojiId, reactionCount}`,
   mais `updateTimeUsec`. Um balde por segundo desde a leitura anterior,
   sem repetir entre leituras (6 s entre leituras = 6 baldes): dá para
   tocar um balde por segundo, atrasado de um intervalo. Agregado por
   emoji, nunca por pessoa. Vistos: ❤ (sem FE0F), 🎉 💯 😄 😳.
   Emojis de canal (`isCustomEmoji`) trazem `image.thumbnails[]` em
   yt3.ggpht.com com sufixo `=w48-h48-c-k-nd`; trocar por `=w96-h96...`
   dá outro tamanho, sempre PNG RGBA (mesmo pedindo WebP).
3. **Interações** *(feita)*: registro de comandos genérico
   (`src/commands.c`: nome, aliases, papel mínimo, espera por pessoa e
   global, dado opcional; `!` e `！`; atalho `!nome` para avatar, peça ou
   paleta) e comandos padrão em `src/actions.c` (avatar, color, gear, jump,
   sit, dance, emote, hug, attack, sound → gancho para a fase 7).
   Acessórios desenhados (fixos, animados e alinhados à folha do avatar),
   paletas por troca exata de cor, `users.tsv` com gravação atômica a cada
   30 s e na saída, importação automática do `userData` do SA na primeira
   vez (947 pessoas importadas aqui). 21 testes de unidade. Lições sobre
   os acessórios (deduzidas e conferidas visualmente; ver `sa.h`):
   - referência = centro da base da célula do avatar (os pés); a peça
     pende do centro da própria base; pivôs em pixels do avatar com y para
     cima; o `uniquePivot` da peça substitui o `gearPivot` do conjunto;
   - conjunto com pivô y=-1000 está escondido naquele quadro (cadeiras só
     aparecem sentado);
   - z = `zIndex` da peça se `isUniqueZindex`, senão `globalZIndex` do
     conjunto; negativo fica atrás do corpo;
   - `isAnimatedAligned`: a folha da peça tem a mesma grade da do avatar.
   Pendente: `paletteSwap` das peças (b_heads deveriam herdar a paleta) e
   Super Chat/membro com reação além do balão.
4. **Configuração, socket unix e configurador GTK2** *(feita)*:
   `src/ini.c` lê e edita .ini preservando comentários e ordem;
   `src/config.c` monta a configuração em camadas (padrões → `.ini` →
   linha de comando, refeitas a cada `reload`) e guarda a tabela dos
   comandos padrão; seções `[command.NOME]` mudam apelidos, esperas, papel
   e `enabled`, ou criam comandos novos com `action` + `data` (o `data`
   vira o argumento fixo: `!buzina` = `sound buzina`). `src/control.c`:
   socket em `$XDG_RUNTIME_DIR/kikarinhas.sock` (0600), JSON por linha
   (`message`, `reload`, `ping`, `quit`, ou a palavra solta), até 16
   clientes, respostas sem bloquear; recusa tomar o socket de outra
   instância e troca um socket velho de um travamento. `reload` também por
   SIGHUP e por `kikarinhas --reload`; muda ao vivo comandos, avatar
   padrão, limites, fps, demo e alvo do YouTube (`kk_http_cancel` solta as
   requisições do conector antigo); o resto avisa que precisa reiniciar.
   `kikarinhas-config` (GTK2, opcional no build) edita tudo, com a lista
   de comandos editável, e manda `reload`. 13 testes novos (34 no total).
   Testado de ponta a ponta com ASan: bridge, comandos do .ini, `reload`
   pelo socket, por SIGHUP e pelo configurador. Lições:
   - comentário só em linha própria: um valor pode ter `#` (links);
   - o caminho de um socket unix tem no máximo 108 bytes: o padrão fica em
     `$XDG_RUNTIME_DIR`, e caminho longo dá erro claro em vez de truncar;
   - o GTK põe o `LC_NUMERIC` do sistema (vírgula decimal): o configurador
     volta para "C" para gravar `1.5`.
   A edição da lista de comandos pela interface foi conferida de ponta a
   ponta (2026-10-12, num Xvfb com `xdotool`, sem gerenciador de janelas):
   adicionar um comando de texto, editar apelidos/espera/custo de um padrão
   (só o que mudou vai para o `.ini`), "Restaurar padrão", recusa de remover
   padrão, nome repetido e imagem faltando (mensagem em vermelho no
   diálogo), remover um novo, salvar e reabrir (o diálogo de imagem mostra
   a prévia na posição certa). Lições: os diálogos agora rolam e respeitam
   a altura da tela (`dialog_body`); a roda do mouse sobre um combo dentro
   do diálogo troca o valor do combo em vez de rolar (comportamento do GTK2).
5. **Twitch** *(feita)*, **Kick** e **Odysee** *(feitos, ver abaixo)*. `src/twitch.c`: IRC sobre
   TLS em `irc.chat.twitch.tv:6697` como `justinfanNNNNN`, com
   `twitch.tv/tags` e `twitch.tv/commands`. Nada bloqueia: o
   `kk_http_connect` pede ao libcurl só DNS + TCP (`CONNECT_ONLY`) e
   devolve uma cópia do socket (`CURLINFO_ACTIVESOCKET` + `dup`); o OpenSSL
   roda em BIOs de memória e nós fazemos `recv`/`send` com `MSG_NOSIGNAL`
   (socket caído vira erro, não SIGPIPE), com o fd no `poll()` do laço.
   Mensagens: PRIVMSG (`bits` e Hype Chat viram pagas), USERNOTICE de
   sub/resub/presentes viram de membro (com a mensagem, ou o `system-msg`
   se não houver), raid e announcement viram comuns. Selos: broadcaster =
   dono, moderator = mod, subscriber/founder = membro, partner =
   verificado. `user-id` é a chave (`twitch:ID`, o mesmo do `userData` do
   SA). PING/PONG, `RECONNECT`, PING nosso após 4 min calado e queda após
   5 min; reconexão com espera crescente até 60 s. Emotes: a tag `emotes`
   (posições em code points, convertidas para bytes) e, com
   `[chat] third_party_emotes` (padrão sim), as listas do BTTV, FFZ e 7TV
   (globais e do canal, pelo `room-id` do ROOMSTATE) procuradas palavra a
   palavra; em conflito vale canal > global e 7TV > BTTV > FFZ. 8 testes
   com linhas sintéticas. Testado na live da ironmouse (~6,8 mil
   espectadores), também com ASan e `reload` desligando e religando.
   Lições:
   - imagens estáticas em PNG: Twitch `.../static/dark/2.0`; BTTV
     `/emote/ID/2x.png` (mesmo animado, vem o 1º quadro); FFZ `urls["2"]`;
     7TV `2x.png` nos estáticos e `2x_static.png` nos animados (cada um dá
     404 no outro caso), apesar de a lista só citar WebP/AVIF/GIF;
   - BTTV responde 404 para canal sem cadastro: lista vazia, não erro;
     7TV dá `"emote_set": null`;
   - a lista do 7TV de canal grande chega a 1000 emotes: busca binária na
     tabela ordenada.
   Não verificado ao vivo: bits, Hype Chat, subs e raids (só nos testes).
   **Várias lives ao mesmo tempo**: `[chat] youtube` e `twitch` são listas
   (vírgula ou espaço; `kk_config_next_target`), `-y`/`-t` repetíveis (o
   primeiro troca a lista do arquivo, os outros somam). O `main.c` guarda
   até 16 fontes (`source`: alvo + conector); no `reload` os alvos são
   conferidos antes (um inválido recusa tudo e mantém o que roda), as
   fontes que continuam na lista ficam conectadas e só as novas/removidas
   abrem/fecham. Cada mensagem leva `source` (`#canal`, `@handle` ou id do
   vídeo), visto no `-v`; os logs do YouTube também. A chave das pessoas
   continua `plataforma:id`, então quem aparece em duas lives da mesma
   plataforma tem um avatar só. Testado ao vivo com ironmouse e xqc na
   Twitch e Lofi Girl no YouTube juntos (ASan), e com `reload` tirando uma
   e com um alvo inválido. Cada conector da Twitch baixa as listas globais
   de emotes por conta própria (3 pedidos a mais por canal).
   Fora: espectadores calados (JOIN/PART só dão o login, sem `user-id`) e
   responder no chat (precisa de OAuth).
   **Kick e Odysee** *(feitos)*. O TLS sem bloquear saiu do `twitch.c`
   para `src/tls.c` (o mesmo esquema: `kk_http_connect`, BIOs de memória,
   `recv`/`send` com `MSG_NOSIGNAL`, fd no `poll()`), e por cima dele
   `src/ws.c`, um cliente websocket mínimo (RFC 6455: handshake com
   `Sec-WebSocket-Accept` conferido, quadros mascarados, fragmentos
   juntados, ping/pong, close; ping nosso após 40 s calado e queda após
   80 s). A libcurl daqui (7.81) não fala websocket. `src/kick.c`: alvo é
   o nome ou link `kick.com/canal`; pede os ids do canal, conecta no Pusher
   e responde `pusher:ping`. Mensagens (`message` e `reply`): chave
   `kick:<sender.id>`, selos broadcaster/moderator/subscriber+founder/
   verified; `[emote:ID:NOME]` vira `NOME` no texto e emote de imagem
   (`files.kick.com/emotes/ID/fullsize`); com `third_party_emotes`, 7TV
   global e do canal (`7tv.io/v3/users/kick/<user_id>`, mesma tabela da
   Twitch). `KicksGifted` vira mensagem paga ("500 Kicks"); `SubscriptionEvent`
   e `GiftedSubscriptionsEvent` só trazem o nome, então viram mensagem de
   membro se a pessoa falou no chat há pouco (anel de 512 nomes → id).
   `src/odysee.c`: alvo é um claim id (40 hex), link da live
   (`odysee.com/@canal:x/live:y` ou `lbry://`) ou canal (`@canal`); canal
   espera entrar ao vivo (pergunta a cada 60 s) e, lendo, confere a cada
   2 min se começou outra live, e troca. Chave `odysee:<channel_id>` de
   quem comenta (todo comentário é de um canal), nome `@canal`;
   `is_creator` = dono, `is_moderator`/`is_global_mod` = mod;
   `support_amount` > 0 vira paga (LBC, ou `USD 5.00` se `is_fiat`).
   Emotes `:nome:` do Odysee viram imagem (48 px), `:nome:` de twemoji
   viram o emoji Unicode, `<stkr>:NOME:<stkr>` vira sticker (imagem);
   tabelas geradas do código do site por `tools/gen_odysee_emotes.py`
   (`src/odysee_emotes.h`); menção `@nome:c` vira `@nome`. Emotes que só
   existem animados (GIF do Kick) entram pelo 1º quadro
   (`kk_image_decode_still`). `-k`/`-o` e `[chat] kick`/`odysee`, com
   `reload` e lista na aba Chat do configurador. 16 testes novos (ws, Kick,
   Odysee). Testado ao vivo, juntos e com ASan: Kick na live do gabepeixe
   (~5 mil espectadores), Odysee em duas lives pequenas (por canal e por
   claim id), Twitch junto; `reload` tirando e pondo o Kick e com alvo
   inválido. Lições:
   - Kick: a API `kick.com/api/v2` respondeu sem bloqueio do Cloudflare,
     inclusive sem User-Agent; a API oficial (`api.kick.com/public`) pede
     OAuth;
   - Odysee: `comments.odysee.tv` recusa (403) o User-Agent do Python, não
     o do curl; o `resolve` às vezes diz NOT_FOUND para o que existe
     (tentar de novo);
   - o chat do Odysee é pequeno: a live mais ativa entre ~130 tinha 13
     mensagens em 15 min;
   - SIGPIPE: a libcurl 7.81 deixa o OpenSSL escrever direto nos sockets
     HTTPS e, com `CURLOPT_NOSIGNAL`, não ignora mais o SIGPIPE; um teste
     com várias lives morreu assim uma vez (não reproduzido depois). Agora
     o processo ignora SIGPIPE (e volta ao padrão antes do `exec` do
     configurador).
   Não verificado ao vivo: Kicks, subs e presentes do Kick (só nos testes:
   em 7 min de 4 canais grandes veio 1 sub e nenhum Kick; o formato dos
   Kicks vem da KickLib) e gorjetas/stickers do Odysee.
6. **kikarinhas-web** (WPE) como camadas. *Adiada* (opcional).
7. **Extras**. Feitos:
   - **Mesa de som**: `src/sample.c` decodifica wav/ogg/mp3 inteiros na
     memória (dr_wav, dr_mp3 e stb_vorbis embutidos em `vendor/`, atrás de
     `src/decode.h`), já em 16 bits estéreo a 48 kHz (reamostragem linear),
     e mede pico e intensidade (RMS dos blocos de 50 ms acima de −50 dBFS);
     `src/audio.c` mistura até N vozes (ganho em ponto fixo, satura em vez
     de dar a volta) e escreve no ALSA sem bloquear, com os descritores do
     ALSA no `poll()` do laço; abre o dispositivo no primeiro som e fecha
     depois de 3 s parado. `src/soundboard.c` liga as seções
     `[sound.NOME]` (arquivo, apelidos, volume 0–400%) ao mixer, com cache
     de cada arquivo decodificado na primeira vez. Cada som vira também um
     comando `!NOME` com a espera do `!som` compartilhada
     (`kk_commands_set_group`). Socket: `play` e `stop`.
   - **Espectadores**: o arquivo de pessoas guarda nome, primeira e última
     vez (hora Unix); `--import-sa-users` completa com `displayName`,
     `firstTimeSpawned` e `lastTimeUsed` do SA sem mudar escolhas, e traz
     quem só tinha nome. Socket: `save` e `set_avatar`.
   - **Configurador**: abas Sons (adicionar, tocar com o mesmo mixer,
     nivelar, importar a mesa do SA com os volumes de lá) e Espectadores
     (busca, ordenação, troca de avatar pelo socket ou, com o programa
     fechado, direto no arquivo). Separado em `config/main.c`, `sounds.c` e
     `audience.c`.
   - **Nome e balão opcionais**: `[avatars] show_names`, `name_position`
     (`below`/`above`) e `show_bubbles` no `.ini`, e os mesmos três campos
     na aba Avatares do configurador (a caixa de posição só habilita com
     "Mostrar nomes" marcado). Viram campos de `kk_view` (`avatar.h`), lidos
     a cada quadro, então aplicam na hora por `--reload`/SIGHUP/"Salvar e
     aplicar", sem respawnar ninguém; `name_position = above` também poupa
     o espaço de `ground_margin` que só existe para caber o nome embaixo.
     `show_bubbles` só evita criar balão (`render_bubble` devolve NULL): um
     balão já aberto quando a opção é desligada termina sozinho, não some
     na hora.
   - **Fontes**: `[avatars] name_font`, `name_size`, `bubble_font` e
     `bubble_size` (família/estilo do Pango + pontos; padrão `Sans Bold` 11
     e `Sans` 10), também na aba Avatares. O balão aplica no reload a partir
     do próximo balão (`kk_stage_set_bubble_font`). O nome só muda
     reiniciando: a etiqueta é renderizada uma vez por avatar e o `ground`
     automático é medido com a fonte dela; o reload avisa e mantém a fonte
     em uso (mesmo esquema de `scale` e `ground`).
   - **Ajuda no balão**: `!help` (`!ajuda`, `!comandos`, `!commands`).
     Cada comando registrado ganha uma linha de uso (`kk_commands_set_help`:
     `!avatar NOME`, `!hug [@nome]`; comando com `data` fixo não tem
     argumento); os sons entram um a um (só o nome, não os apelidos), e o
     `!sound` genérico só aparece quando os sons não são comandos. O
     sorteio é por amostragem de reservatório entre os comandos que o
     papel de quem pediu permite (`kk_commands_each_help`), então cada
     pedido dá outra lista. Balão azul à parte (`kk_stage_help_bubble`,
     `render_text_bubble`), que ignora `show_bubbles`; `[commands]
     help_bubbles` desliga e `help_count` (1 a 20) define a quantidade.
     Duração: `[avatars] bubble_seconds` (mensagens) e `[commands]
     help_seconds` (ajuda), `auto` ou 0,5 a 600 s; `auto` é a fórmula
     antiga. Aplicam no reload, a partir do próximo balão.
   - **Idiomas do configurador**: gettext com msgids em inglês
     (`config/i18n.h`: `_()`, `N_()`, `ngettext()`), traduções em `po/`
     (`pt_BR` e `en`, listadas em `po/LINGUAS`; `make pot` atualiza). Os
     `.mo` saem em `build/locale`, achado ao lado do executável, ou em
     `$(LOCALEDIR)` depois do `make install`. O `kikarinhas` em si segue em
     português, sem tradução. Rótulos de papéis e títulos de colunas
     ficam em tabelas `N_()` e são traduzidos onde aparecem. A string
     de data da aba Espectadores também é traduzida (`%Y-%m-%d` em inglês).
     Clique direito na janela abre o configurador (`open_config` em
     `main.c`).
   - **Versão**: `config.mk` é o único lugar com o número (`VERSION`, hoje
     0.2.0); `make pot` não grava mais em `--package-version`, então os
     `.po` não duplicam. `-V`/`--version` também no
     `kikarinhas-config` (antes só o `kikarinhas` tinha). O rodapé do
     configurador ganhou um rótulo fixo (separado da linha de status, que
     muda a cada ação) com a versão do kikarinhas do outro lado do socket,
     atualizado a cada `ping`: no abrir da janela, depois de "Salvar e
     aplicar" e sempre que a aba Espectadores atualiza (que já fala com o
     socket). A barra de título leva a própria versão do configurador.
   - **Emote wall** (`src/emoji.c`, `src/emotes.c`, `src/emotewall.c`,
     aba em `config/wall.c`): emojis do chat, emotes de membros e reações
     do YouTube voando sobre o palco, como uma camada (`src/layer.h`) que
     o palco pinta por cima dos avatares sem saber o que é. Emoji Unicode
     é achado no texto por uma tabela gerada do `emoji-data.txt`
     (`tools/gen_emoji_table.py` → `src/emoji_table.h`: pictográficos
     sozinhos, os de texto só com FE0F, ZWJ, bandeiras, keycaps, tom de
     pele) e desenhado uma vez pelo Pango com a fonte de emoji. Emotes de
     imagem são PNG baixados pelo `kk_http` (6 de cada vez), guardados em
     `~/.cache/kikarinhas/emotes/<plataforma>/<hash do id>.png`. Regras:
     mínimo por mensagem ou combo (o mesmo emoji em N mensagens em X s;
     enquanto dura, cada mensagem com ele também sobe), blacklist (❤ = ❤️;
     `:_oi:` = `:oi:` = `oi`), teto por mensagem e na tela. Efeitos: `rise`
     (ondas de baixo para cima), `bounce` (diagonal quicando, o logo do
     DVD) e `fly` (de borda a borda). Reações: um balde por segundo,
     tocados um segundo depois do outro, misturadas com o resto. Medido
     com 150 emojis na tela, 30 avatares e balões o tempo todo, 1280x720 a
     30 fps: ~9% de CPU (sem o wall, ~4%).
   - **Níveis**: `anyone` < `member` < `trusted` < `mod` < `owner`
     (`kk_role`). Os selos de cada mensagem dão membro, mod e dono, e o
     arquivo de pessoas grava o da mensagem mais recente (`role=`; comum
     não grava nada). `trusted=yes` é marcado à mão (coluna Confiável da
     aba Espectadores, pelo pedido `set_user` do socket, que também troca
     avatar; `set_avatar` continua valendo). O confiável fica acima do
     membro, como os "regulars" do Nightbot: quem paga não ganha o que o
     streamer reservou para quem ele conhece. `kk_commands_set_trust` liga
     o registro de comandos ao arquivo; a confiança nunca baixa o nível dos
     selos. O chat de mentira não grava nível.
   - **Lista de lives no configurador** (`config/sources.c`): a aba Chat
     mostra uma lista (plataforma, live) com Adicionar/Remover no lugar dos
     dois campos; o arquivo continua com `[chat] youtube` e `twitch`. O
     diálogo adivinha a plataforma pelo link (twitch.tv, youtube.com,
     `@handle`) e confere o alvo com os mesmos `kk_yt_parse_target`/
     `kk_tw_parse_target` do programa, porque um alvo inválido faz o
     `reload` recusar tudo.
   - **Várias pastas de avatares** (`src/library.c`): a própria
     (`$XDG_DATA_HOME/kikarinhas/avatars`, se existir), as de `[avatars]
     paths` e o SA (`sa_dir = off` desliga), nessa ordem; chave repetida
     fica com a primeira. `kk_sa_load_dir` aceita pasta de dados
     (`streamavatars_json.txt`), pacote (`data.txt`) ou uma pasta delas
     (um nível, em ordem de nome). `kk_sa_merge` junta bibliotecas
     deslocando os índices de conjunto (`gear_sets`, `pivots[].set`) e os
     ids de peça; cada avatar guarda a pasta de onde veio (`dir`). Sem SA o
     programa roda com o resto; sem avatar nenhum, sai com a dica das
     pastas. O configurador carrega a biblioteca uma vez para as abas que
     listam avatares (`editor_library`). As páginas de formulário rolam.
   - **Aba Avatares instalados** (`config/avatars.c`): todos os avatares
     da biblioteca juntada, com miniatura (1º quadro do idle, sem a borda
     transparente, ampliado por passos inteiros com nearest se for pixel
     art), célula, animações (base; custom pelo nome), paletas, conjuntos
     de gear e pasta, com busca. As miniaturas carregam aos poucos num
     `g_idle_add` (6 por vez) para a janela não travar; "Ler de novo" relê
     as pastas como a aba Avatares diz.
   - **Comando `text`**: `data` é um texto fixo mostrado no balão azul da
     ajuda sobre o avatar de quem pediu (`!discord`, `!regras`); `\n`
     quebra a linha e `{name}` vira o nome de quem pediu. Cada comando
     ganhou `kk_cmd_params` (`seconds`, e `x`/`y`/`scale` para imagens),
     no `.ini` e em `kk_cmd_call.params`.
   - **Comando `image`**: `data` é um arquivo (PNG, JPEG, GIF animado ou
     BMP, pelo `stb_image` embutido em `vendor/images.c`), mostrado com o
     centro em `x`%/`y`% da janela, em `scale`% do tamanho, por `seconds`
     (padrão 5), com 0,3 s de entrada e saída. `src/image.c` converte para
     ARGB pré-multiplicado, um quadro por superfície (limite de 64 Mpx no
     total); `src/overlay.c` é uma camada sob o emote wall, com um lugar
     por comando (pedir de novo recomeça o tempo) e dano só quando muda
     (entrada, saída, quadro do GIF). Arquivos lidos na primeira vez e
     guardados (inclusive os que falharam, para o chat não forçar releitura);
     o `reload` esquece tudo. Ampliação inteira de 2x ou mais usa nearest.
     Lição: o cache guarda ponteiros, porque as imagens na tela apontam para
     ele enquanto ele cresce (o ASan pegou o `realloc`).
   - **Música tocando** (`src/mpris.c`, comando padrão `!song`, ação
     `nowplaying`): conexão privada à sessão D-Bus com os descritores no
     `poll()` (watches da libdbus; timeouts ignorados porque nenhuma
     chamada espera resposta com prazo). `ListNames` assíncrono e `GetAll`
     por player (a resposta traz o nome único, que é de onde vêm os
     sinais); `NameOwnerChanged` e `PropertiesChanged` mantêm a lista, então
     o comando responde na hora. Vale o player tocando que começou por
     último, senão um pausado. `data` filtra pelo nome do player (o que o
     espectador digita é ignorado: uma aba do navegador não aparece por
     acaso). Socket: `nowplaying`. Testado com o Audacious e com um player
     falso em Python abrindo, trocando de faixa e fechando.
   - **Diálogo de comandos** (`config/commands.c`, saiu do `main.c` do
     configurador): a lista virou resumo (ação com nome legível, dado
     curto, esperas, nível) e cada comando abre um diálogo que mostra só o
     que a ação usa: texto multilinha (`\n` no arquivo), imagem com
     seletor de arquivo, `x`/`y`/`scale` e uma prévia desenhada na
     proporção da janela da aba Janela, som da aba Sons, avatar da
     biblioteca, filtro de player, segundos. Confere nome, texto, imagem e
     som antes de aceitar. Caminho de imagem fica como escrito no arquivo
     (relativo ou `~/`). Lição: eventos de teclado sintéticos (xdotool sem
     XTEST) não trocam o foco com Tab neste X; o teste da interface foi
     pelo diálogo de comando novo e pelo salvar sem mudar nada.
   - **Pontos** (`src/points.c`, aba Pontos em `config/points.c`): saldo
     no arquivo de pessoas (`points=`). Ganha por mensagem (com espera),
     por presença (a cada `watch_minutes` com o avatar na tela; o laço
     chama `kk_points_watch` uma vez por segundo com as chaves dos avatares
     do chat, e uma parada conta no máximo 5 s), chegada (uma vez por
     execução), Super Chat/bits e membro novo; `start` para quem nunca
     apareceu. Gasta pelo `cost` de cada comando e de cada som (o do som
     vale também para `!som NOME`, pelo custo por argumento do registro de
     comandos) e `shortcut_cost`. `kk_commands_set_wallet`: o saldo é
     conferido antes (sem pontos, `KK_CMD_POOR` e o balão "Custa N...") e
     cobrado só se o comando fez algo; o dono não paga. Atalho sem pontos
     vira mensagem comum (não dá para saber antes se era atalho). Comandos
     `points`, `give`, `award` (mod) e `top`; a ajuda mostra o preço. O
     configurador mostra os custos sobre os mesmos dados das abas Comandos e
     Sons, e a aba Espectadores edita o saldo (`set_user` com `points`).
   - **Pôneis com cara de GIF** (`src/sprite.c`): as folhas de applejack,
     fluttershy, rainbowdash e rarity vieram de GIFs com pontilhado, e a
     escala 2 com `pixelsPerUnit` 1,43 dava 1,4× com nearest (pixels de 1
     e 2 de largura, xadrez virando chuvisco). Agora: folha com ≥ 2% de
     pixels em xadrez (lados diferentes, ≥ 3 cantos iguais) **e** mais de
     16 cores passa por duas passadas de mistura com os vizinhos de cor
     próxima (soma |ΔRGB| ≤ 200; contornos e olhos ficam) e é escalada
     suave; pixel art em escala fracionária é ampliada por fator inteiro
     com nearest e reduzida suave. `--check` diz quais foram misturadas.
     Lição: o critério de cores separa o pontilhado intencional da pixel
     art (Pokémon de 10 a 14 cores, também com xadrez nas asas) do
     pontilhado de conversão (27 a 529 cores).
   - **Escolher avatar pela imagem** (aba Espectadores): clique duplo numa
     pessoa abre `editor_pick_avatar` (`config/avatars.c`), uma grade
     (`GtkIconView`) com as miniaturas e busca por nome ou chave; Enter pega
     o primeiro resultado. "Sorteado" manda `avatar: ""` no `set_user`, que
     apaga a escolha e, se a pessoa está na tela, troca para
     `kk_stage_default_avatar` (o padrão do `.ini` ou o do hash da chave).
     As miniaturas ficam num cache por avatar, dividido com a aba Avatares
     instalados.
   - **Timers** (`src/timers.c`, aba em `config/timers.c`): `[timer.NOME]`
     com `command`, `minutes`, `messages`, `viewers` e `enabled`; vence
     quando todas as condições preenchidas valem, e precisa de minutos ou
     mensagens (só pessoas na tela dispararia todo segundo). O laço confere
     uma vez por segundo, junto da presença dos pontos, e roda o comando com
     uma mensagem sintética do dono (`kk_commands_handle` direto, sem
     `on_chat`: não cria avatar nem dá pontos); o balão vai para
     `kk_stage_random_avatar`. No `reload`, um timer que continua com o
     mesmo nome guarda o relógio e a contagem.
   - **Interações do Stream Avatars** (comandos e números tirados do
     `commandList` do SA instalado): `!left`/`!right`/`!move`/`!stand`
     (`kk_avatar_walk_to`, `kk_avatar_stand`); `!freeze` e `!scale` (mods,
     como no SA: alvo `@nome` opcional; valem só na sessão). O avatar ganhou
     estado de voo (`KK_ST_FLY`: velocidade nos dois eixos, quica nas
     paredes, no teto e no chão até ficar lento, rodopia com `angle`/`spin`,
     e o retângulo de dano vira o quadrado do círculo que o contém) e
     tamanho próprio (`size`, desenhado com `kk_sheet_draw_zoom`; folhas
     já em escala de tela usam filtro GOOD fora de 1x). `!sling ângulo
     força`: força 100 chega ao topo da janela (a altura cresce com o
     quadrado da velocidade). `src/props.c` é uma camada nova do palco
     (entre as imagens e o emote wall), toda desenhada em Cairo: `!bomb`
     (custa 10, como no SA) voa em arco até o alvo, o pavio queima 2 s e a
     explosão (clarão, faíscas, fumaça) arremessa quem está a até 75 px x
     escala, mais forte perto; `!spawn` (o "jump catch game" do SA, mods)
     solta estrelas (12% de gemas, valem 3x) na altura da cabeça mediana
     mais 45% a 70% do pulo mediano: parado embaixo não alcança, pulando
     sim (testado); quem encosta pega, `+N` sobe e os pontos de `[points]
     catch` vão para a pessoa. Os objetos nunca guardam ponteiro de
     avatar (o palco realoca e troca de lugar): procuram a cada quadro.
     `src/games.c`: `!roll` (1-100, 1-N, XdY), `!bola8` (20 respostas),
     `!slots` com a tabela do SA (46% ganham; cereja/laranja/7/moeda/saco
     com pesos 31/25/24/14/6 e 1x/0,5x/2x/4x/10x: ~95% de retorno, conferido
     em 200 mil giros) e roleta (`!bet N TIPO`: rodada de
     `roulette_seconds`, número/verde 36x, cores/par/metades 2x, dúzias 3x,
     o zero perde as apostas de fora). Apostas entre `min_bet` e `max_bet`,
     tiradas do saldo na hora e devolvidas se o programa fechar com a
     rodada aberta (`kk_actions_refund`); a roleta gira em
     `kk_actions_tick`, chamado a cada volta do laço. O configurador passou
     a listar todas as ações no diálogo de comandos: antes `points`,
     `give`, `award` e `top` caíam em "Trocar avatar" ao editar. 9 testes
     novos (física, bomba, pegar estrela pulando, jogos). Testado num X
     virtual (Xvfb) com seis pessoas inventadas pelo socket: bomba
     arremessando três (que pegaram estrelas voando), estilingue,
     `!scale 250`, `!freeze`, dado, bola 8, caça-níquel e roleta.
     Não verificado: a interface nova do configurador (só compila e a
     tradução está completa).
   Lições:
   - desenhar os emotes 1:1 (posição inteira, sem escala) é o caminho
     rápido do pixman: com escala aleatória por emote o mesmo wall custava
     o dobro; escala só no "pop" de entrada (0,2 s);
   - rajadas nascem no mesmo instante e à mesma velocidade viram
     fileiras: espalhar a saída (0,05 a 0,2 s) e variar a duração (±20%);
   - com muitos retângulos de dano espalhados o limite de 16 junta tudo
     num só, mas subir o limite (64, 128) não ajudou: o custo era o
     desenho, não a área;
   - nivelar pela intensidade com porta de silêncio, e não pelo pico, é o
     que iguala sons curtos e longos; o teto é o pico (nunca estoura);
     meta −18 dBFS;
   - o arquivo de pessoas é do kikarinhas enquanto ele roda (grava a cada
     30 s): o configurador pede `save` antes de ler e troca avatar pelo
     socket, senão a troca seria desfeita;
   - o GTK2 sem tradução instalada mostra os botões prontos em inglês:
     rótulos próprios com o ícone pronto.
   Não verificado: o som no OBS (só conferido que o fluxo aparece no
   PipeWire e some parado) e mp3 com taxa variável longos.
   - **Emotes animados e emotes no balão**: `kk_emotes_get_anim` devolve um
     `kk_emote_anim` (quadros já encaixados no quadrado do emote, atrasos,
     contagem de referências); GIFs com mais de ~1 M pixels no total ficam
     só com o 1º quadro, e o cache em disco agora é `.img` (guarda o
     arquivo como veio, PNG ou GIF). Fontes animadas: Twitch `default/dark/2.0`
     (GIF se animado), BTTV `2x.gif` (pelo `imageType`), 7TV `2x.gif` (pelo
     `animated`), Kick (GIF de sempre); FFZ animado só vem em WebP, que não
     decodificamos, e Odysee não tem. Na wall a partícula escolhe o quadro
     pela idade. No balão o texto leva U+FFFC no lugar do emote (atributo
     `shape` do Pango deixa um quadrado da altura da linha) e o `avatar`
     guarda os `kk_bubble_emote` (posição + animação), pintados por cima do
     balão a cada quadro; o balão repinta quando o quadro de algum muda
     (`emote_sig`). Emote ainda baixando aparece como vazio e entra quando
     chega (o `stage` resolve no `update`); se falhar fica o vazio. Só
     emotes de imagem com `start`/`len` entram no balão; o emoji Unicode
     segue como texto, pela fonte.
   - **Mouse na janela** (`src/pointer.c`), só no modo OBS (o desktop
     continua com o clique atravessando): `window.c` passa a pedir
     `ButtonRelease` e `Button1Motion` (movimento só com o botão esquerdo
     apertado, nada de tráfego parado) e entrega pressionar/mover/soltar
     em `kk_window_events.pointer`, com movimentos seguidos fundidos e o
     horário do servidor X. `kk_pointer` vira isso em gestos para
     *alvos* registrados em ordem (o de cima primeiro): `hit` diz o que
     está no ponto, soltar parado é `click`, passar de 4 px vira `grab`
     (com o ponto onde apertou, para manter o deslocamento) + `move` e, ao
     soltar, `drop` com a velocidade dos últimos 100 ms (zero se parou
     antes de soltar): a base para arremessar avatares. `kk_pointer_cancel`
     larga o que está seguro (avatar que saiu). O clique direito continua
     abrindo o configurador e não passa por aqui. Conferido no Xephyr com
     `xdotool`. Próximos alvos: a camada de imagens e os blocos HTML.
   - **Pegar e arremessar avatares** (`kk_stage_pointer_target`): o
     acerto é no pixel do sprite (alfa ≥ 64 na folha, com 3 px de folga,
     desfazendo a rotação de quem está girando), o de cima primeiro.
     Clique faz pular. Pegar leva o avatar para o fim da lista (desenhado
     por cima) e entra em `KK_ST_HELD`: segue o ponteiro mantendo o ponto
     onde foi pego, preso dentro da janela, e se inclina contra o
     movimento (até 0,7 rad, suavizado) como algo pendurado. A animação é
     uma custom chamada hang/held/grab/carry/fall, se o avatar tiver, ou o
     pulo. O SA não tem linha própria para isso. Segurado, nada mais o move:
     pulo, emote, sentar, andar, aproximação e bomba são ignorados, e ele
     não sai por silêncio nem para dar lugar a outro. Ao soltar vira o voo
     de sempre (`KK_ST_FLY`, o do estilingue) com a velocidade do ponteiro,
     limitada a 3000 px/s, e giro proporcional a vx. Cada avatar tem um
     `id` do palco, que o ponteiro usa no lugar de índice (o vetor muda).
   - **Danmaku** (`src/danmaku.c`, aba em `config/danmaku.c`, seção
     `[danmaku]`): com ele ligado, `main.c` desliga os balões de chat
     (`chat_bubbles`) e manda cada mensagem que não é comando para a
     camada, com as áreas dos avatares (`kk_stage_avatar_areas`). Ajuda e
     comandos de texto seguem em balão. A tela vira faixas de uma linha
     (medida com texto CJK e emoji, para os fallbacks caberem); toda
     mensagem atravessa em `seconds`, então as compridas andam mais
     rápido. Uma faixa está livre se a última já entrou inteira (mais
     24 px) e a nova, mais rápida, não a alcança antes de ela sair. Entre
     as livres, as de menor custo (altura × largura de avatar cruzada),
     sorteando entre as empatadas; sem faixa livre, a que libera primeiro.
     Cada mensagem é desenhada uma vez (Pango: contorno com
     `pango_cairo_layout_path` + traço redondo, depois o texto; valor em
     negrito nas pagas; emotes de imagem em U+FFFC com atributo `shape`,
     como no balão, reaproveitando `kk_bubble_emote`) e copiada a cada
     quadro. O dano é um retângulo por mensagem (posição anterior ∪ atual).
     Opacidade < 100% usa `push_group`. Passando de `max_on_screen`, as
     novas são descartadas. Cores `#rrggbb` (`kk_parse_color`), botões de
     cor do GTK no configurador, opções acinzentadas enquanto desligado.
     `KK_MAX_LAYERS` foi para 6. Medido no Xephyr com o chat de mentira:
     ~3% de CPU, igual com ele desligado (quem pesa é a emote wall).
   - **Cenário** (`[scenery.NOME]`, aba em `config/scenery.c`): imagens
     fixas na janela (chão, fundo, moldura, logo) com `file`, `x`/`y`
     (centro, em % da janela), `scale` (% do arquivo) e `layer =
     behind|front`. Não vêm do SA: o usuário põe os arquivos. É o
     `src/overlay.c` com itens *fixados* (`kk_overlay_pin`: sem fim, sem
     entrada/saída, GIF anima, nunca perdem o lugar para a imagem de um
     comando). `main.c` tem duas instâncias: `overlay` (frente, junto das
     imagens dos comandos) e `backdrop`, ligada com
     `kk_stage_add_back_layer`, que o palco pinta depois de limpar a área
     de dano e antes dos avatares. O `reload` limpa as duas e fixa de novo
     (`pin_scenery`). Máximo de 16 (`KK_CONFIG_MAX_SCENERY`).
   Decidido não fazer: plugin de OBS lendo a memória compartilhada (a
   captura Xcomposite já serve) e Wayland nativo (o código é Xlib; no
   Wayland roda pelo XWayland, que o OBS já captura; nativo exigiria
   reescrever `window.c` e `pointer.c`, sem `override_redirect` nem
   layer-shell no GNOME). Fundos e pacotes .zip do SA também ficaram de
   fora. O que sobra em aberto é só a fase 6 (camadas HTML), opcional.
