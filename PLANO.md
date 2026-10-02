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
   Não verificado: a interface do configurador só foi conferida em parte
   (abre, salva sem mudar nada do arquivo, aplica no programa aberto e
   grava um campo alterado); não deu para capturar a tela aqui, então a
   edição da lista de comandos pela interface ficou sem teste.
5. **Twitch** (IRC anônimo) e **Odysee** (Commentron). *Adiada*: cada
   conector é isolado e entrega o mesmo `kk_chat_msg`, então nada depende
   dela; pode começar como bridge no socket.
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
   - **Versão**: `config.mk` em 0.1.0; `-V`/`--version` também no
     `kikarinhas-config` (antes só o `kikarinhas` tinha). O rodapé do
     configurador ganhou um rótulo fixo (separado da linha de status, que
     muda a cada ação) com a versão do kikarinhas do outro lado do socket,
     atualizado a cada `ping`: no abrir da janela, depois de "Salvar e
     aplicar" e sempre que a aba Espectadores atualiza (que já fala com o
     socket). A barra de título leva a própria versão do configurador.
   Lições:
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
   Pendentes: fundos do SA, emojis/emotes como imagem no balão, plugin de
   OBS lendo a memória compartilhada direto (sem Xcomposite), Wayland
   (layer-shell) no futuro, reações do YouTube.
