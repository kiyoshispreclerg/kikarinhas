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
| miniz (embutido, ainda não incluído) | ler os zips do SA/workshop |
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
   Chat e membros (só nas fixtures). Pendente: emojis de canal viram
   `:atalho:` em texto (imagens na fase 7).
   Ideia anotada: as **reações** do YouTube (coração, 100, risada...)
   parecem vir em `frameworkUpdates` da mesma resposta, como contagens
   agregadas por emoji, não por pessoa; dariam um "emote wall" de ícones
   subindo. Falta confirmar o formato numa live com reações.
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
4. **Socket unix e configurador GTK2**: protocolo de bridges, `reload`,
   `kikarinhas-config`.
5. **Twitch** (IRC anônimo) e **Odysee** (Commentron).
6. **kikarinhas-web** (WPE) como camadas.
7. **Extras**: fundos e sons do SA, emojis/emotes como imagem no balão,
   plugin de OBS lendo a memória compartilhada direto (sem Xcomposite),
   Wayland (layer-shell) no futuro.
