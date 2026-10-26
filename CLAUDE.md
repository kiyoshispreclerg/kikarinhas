# Kikarinhas — notas para o Claude

Avatares do chat numa janela ARGB que o OBS captura (tipo Stream Avatars),
nativo X11, em C11 + Xlib + Cairo + Pango + libcurl + ALSA + libdbus. Leia o `PLANO.md`
antes de começar uma fase: ele tem a arquitetura, o estado de cada fase e as
lições aprendidas (formato do Stream Avatars, protocolo do YouTube).

O usuário escreve em português (pt-BR) e quer as respostas em português.
Mensagens para o usuário, logs e textos da interface ficam em português;
comentários e identificadores no código, em inglês. Exceção: o
`kikarinhas-config` usa gettext com msgids em inglês (`_("...")`, `N_()`,
`ngettext()`) e traduções em `po/` (pt_BR e en); o `kikarinhas` em si não
traduz nada.

## Compilar e testar

```sh
make            # build/kikarinhas, sem avisos (-Werror -Wpedantic etc.)
make test       # testes de unidade (tests/unit/test_*.c)
make asan       # os testes com ASan/UBSan
make asan-run ARGS="--demo-chat"   # o programa com ASan
make pot                           # recria po/kikarinhas.pot e mescla nos .po
build/kikarinhas --check           # valida todas as folhas do SA instalado
tools/smoke-test.sh [--valgrind]   # confere um kikarinhas instalado (no PATH): versões,
                                   # libs, chat falso num Xvfb, configurador; a release roda
                                   # isto em cada .deb numa imagem limpa. Local:
                                   # make install PREFIX=/tmp/x && PATH=/tmp/x/bin:$PATH sh tools/smoke-test.sh
build/kikarinhas --demo-chat -v    # chat de mentira, sem rede
build/kikarinhas -y <link-da-live> -v
build/kikarinhas -k <canal-do-kick> -o @canal-do-odysee -v
build/kikarinhas -c /tmp/x.ini --socket $XDG_RUNTIME_DIR/kk-teste.sock
```

- Os vazamentos do fontconfig são esperados e estão em `tools/lsan.supp`.
- A CI (`ci.yml`) também roda o programa inteiro com o chat falso sob ASan
  por 15 s; `make asan` + `build/asan/kikarinhas --demo-chat ...` num Xvfb
  reproduz isso (já achou um vazamento ao sair que os testes de unidade não
  veem). Sem avatares seus nem SA (CI, HOME vazio) entra o pacote padrão de
  `data/avatars/default`, achado ao lado do executável.
- `make lint` roda o cppcheck (instalado aqui) e procura funções proibidas:
  strcpy, strcat, sprintf, vsprintf, gets.
- Testes com arquivos de usuário: use `--users` apontando para um arquivo
  temporário já existente (vazio evita importar o SA), nunca o
  `~/.local/share/kikarinhas/users.tsv` do usuário; idem `-c` para o .ini.
- Áudio em teste: o dispositivo ALSA `null` não precisa de placa de som
  (`device = null` ou `kk_audio_new("null", ...)`). Fora disso, o teste
  toca de verdade nas caixas do usuário: volume baixo.
- Captura de tela da janela: num X à parte, sem mexer no desktop do
  usuário: `Xephyr :78 -screen 1280x720 &` (o tamanho da janela; o usuário prefere o Xephyr,
  para ver também; o `Xvfb :77` serve quando não há o que olhar), rode
  com `DISPLAY=:78`, mouse com `xdotool` nesse display, e
  capture com `python3 -c "from PIL import ImageGrab;
  ImageGrab.grab(xdisplay=':78').save('x.png')"`. No X do usuário, `xwd
  -root` passou a dar segfault, e a janela costuma ficar atrás do editor
  (`xwd -id`, `import` e `scrot` também falham).
- Lives para teste: o Kick lista as que estão no ar em
  `https://kick.com/stream/featured-livestreams/pt`; o Odysee em
  `https://api.odysee.live/livestream/all` (chat bem parado: escolha pelo
  `comment.List` recente); a Twitch, `https://decapi.me/twitch/uptime/CANAL`.
- Socket em teste: caminho curto (`$XDG_RUNTIME_DIR/kk-*.sock`); o do
  scratchpad passa dos 108 bytes de um socket unix.
- `pkill`/`pgrep` sem `-f` casam pelo `comm` (`/proc/PID/comm`), truncado em
  15 bytes: `kikarinhas-config` vira `kikarinhas-conf`, não
  `kikarinhas-confi` como o prefixo do nome sugere.
- Mudar `VERSION` (ou outro `-D` de `config.mk`) não força recompilação:
  `make` não enxerga flags do compilador como dependência, só arquivos. Dá
  `make clean` antes de conferir o valor novo.
- Fixtures de teste são sintéticas (nomes e ids inventados), no formato real.
  Não grave dados de pessoas reais do chat no repositório.
- O SA do usuário está em `~/Steam/Library/steamapps/compatdata/665300/...`
  (achado sozinho por `kk_sa_find_data_dir`). O `streamavatars_json.txt` tem
  tokens em `loginDetails`: nunca leia, imprima nem copie esse campo.

## Estrutura

| Arquivo | Papel |
|---|---|
| `src/main.c` | opções, laço principal (`kk_http_wait` = poll + libcurl), ligação de tudo |
| `src/window.c` | janela ARGB, buffer MIT-SHM, envio só das regiões alteradas |
| `src/pointer.c` | mouse na janela (só modo OBS): clique, arrasto e arremesso, entregues aos alvos registrados |
| `src/stage.c` | avatares na tela, caches de folhas (paleta, gear), dano, nomes, balões, interações |
| `src/avatar.c` | máquina de estados, animação, geometria (sprite, gear, nome, balão) |
| `src/sprite.c` | folhas: recorte, recolor, escala (pixel art fica 1x e amplia com nearest) |
| `src/sa.c` | leitura do Stream Avatars: avatares, gear, pivôs, paletas, userData; pacotes e junção de bibliotecas |
| `src/library.c` | de onde vêm os avatares: pasta própria, `[avatars] paths`, o SA e, por último, as pastas do sistema (`kikarinhas/avatars` em cada `XDG_DATA_DIRS`, um kit por subpasta; ali o programa instala o pacote padrão `data/avatars/default`, gerado por `tools/gen_default_avatars.py`); os avatares do sistema levam `system = true` e o palco não os sorteia por hash enquanto houver outros |
| `src/youtube.c`, `src/http.c` | chat do YouTube (InnerTube) sobre libcurl multi |
| `src/tls.c`, `src/ws.c` | TLS sem bloquear (BIOs de memória, fd no `poll()`) e websocket por cima |
| `src/twitch.c` | chat da Twitch (IRC anônimo sobre TLS), emotes do BTTV/FFZ/7TV |
| `src/kick.c` | chat do Kick (websocket do Pusher), emotes do Kick e do 7TV, Kicks e subs |
| `src/odysee.c`, `src/odysee_emotes.h` | chat do Odysee (websocket do Commentron), canal → live; tabela de emotes gerada por `tools/gen_odysee_emotes.py` |
| `src/commands.c` | registro genérico de comandos: aliases, papéis, esperas |
| `src/actions.c` | comandos padrão (avatar, color, gear, dance, hug, sound, bomb, slots...) |
| `src/props.c` | objetos do palco (camada): bomba que arremessa os avatares, estrelas para pegar pulando |
| `src/games.c` | jogos de sorte puros: dado, bola 8, caça-níquel e roleta (números do SA) |
| `src/users.c` | `users.tsv`: escolhas de cada pessoa, nível, pontos, gravação atômica |
| `src/points.c` | pontos: ganhos (mensagem, presença, chegada, pagos, membros), saldo, ranking |
| `src/timers.c` | timers: quando cada `[timer.NOME]` vence (minutos, mensagens, pessoas na tela) |
| `src/demochat.c` | chat de mentira para testes |
| `src/ini.c` | .ini lido e editado no lugar (mantém comentários) |
| `src/config.c` | configuração em camadas, comandos padrão, `[command.NOME]` |
| `src/control.c` | socket unix: bridges (JSON por linha), `reload`, `play`, `set_avatar`... |
| `src/sample.c`, `src/decode.h` | sons: decodifica (vendor/decoders.c), 48 kHz, mede para nivelar |
| `src/mpris.c` | música tocando no desktop (MPRIS na sessão D-Bus, sem bloquear) |
| `src/image.c`, `src/overlay.c` | imagens de arquivo (vendor/images.c: PNG, JPEG, GIF animado, BMP) e a camada do `action = image` e do cenário fixo (`[scenery.NOME]`, `kk_overlay_pin`) |
| `src/audio.c` | mixer + saída ALSA sem bloquear (fds no `poll()`) |
| `src/soundboard.c` | `[sound.NOME]` → mixer, cache dos arquivos decodificados |
| `src/chat.h` | eventos neutros de todas as plataformas: mensagem, emote, reação, `kk_chat_sink` |
| `src/emoji.c`, `src/emoji_table.h` | acha emojis Unicode no texto (tabela gerada por `tools/gen_emoji_table.py`) |
| `src/emotes.c` | imagens dos emotes: emoji pela fonte, PNG/GIF baixado (todos os quadros, `kk_emote_anim` com contagem de referências) + cache em `~/.cache/kikarinhas/emotes` |
| `src/emotewall.c`, `src/layer.h` | emote wall: regras (mínimo, combo, blacklist), efeitos, camada sobre o palco |
| `src/chatview.c`, `src/chatwin.c` | janela de chat clássico: histórico e desenho (sem X, testável) e a janela em si (filha da principal na mesma conexão X, `kk_window_open_child`) |
| `src/danmaku.c` | danmaku: mensagens atravessando a tela no lugar dos balões, faixas sorteadas desviando dos avatares |
| `docs/screenshots/`, `tools/screenshots.py` | imagens do README, tiradas por um Xephyr com cena montada pelo socket (`python3 tools/screenshots.py`) |
| `docs/avatares/` | guia para criar avatares, gear e paletas (formato do SA); `kit-exemplo/` é gerado por `tools/gen_kit_exemplo.py`. Mudou o leitor (`sa.c`)? Atualize aqui |
| `data/` | `kikarinhas.ini` de exemplo, `.desktop` dos dois programas, o ícone e `avatars/` (instalado como `share/kikarinhas/avatars`; hoje só o kit `default`) |
| `po/` | traduções do configurador: `kikarinhas.pot`, `LINGUAS`, `POTFILES.in`, um `.po` por idioma |
| `config/main.c` | configurador GTK2 (opcional no build): janela e abas gerais |
| `config/sounds.c`, `config/wall.c`, `config/danmaku.c`, `config/audience.c` | abas Sons, Emote wall, Danmaku e Espectadores |
| `config/chatwin.c` | aba Janela de chat: `[chat_window]` e o botão que mostra/esconde a janela pelo socket |
| `config/points.c` | aba Pontos: tudo o que dá e tira pontos, custos de comandos e sons |
| `config/timers.c` | aba Timers: lista e diálogo de cada timer |
| `config/scenery.c` | aba Cenário: imagens fixas atrás ou na frente dos avatares |
| `config/commands.c` | aba Comandos: lista e diálogo de edição por ação (texto, imagem com prévia, som...) |
| `config/sources.c`, `config/avatars.c` | lista de lives da aba Chat; pastas de avatares e biblioteca carregada uma vez |

## Convenções

- Prefixo `kk_` em tudo que é público; um módulo por arquivo, cabeçalho com
  comentário explicando o porquê.
- Laço único sem threads; nada pode bloquear (rede só via `kk_http`).
- Desempenho importa (meta: poucos % de CPU com 30 avatares): o que é
  estático vai para superfícies em cache e só é copiado a cada quadro.
- Caminhos com `kk_pathf` (falha em vez de truncar).
- `README.md` (inglês, padrão) e `README.pt-BR.md` andam juntos: mudou um, mude o
  outro.
- Commits em inglês, terminando com a linha Co-Authored-By.

## Próximas fases (ver PLANO.md)

5. Feita: Twitch, Kick e Odysee.
6. Janela de chat clássico (opcional): para ler e para o OBS capturar;
   comentário em destaque no palco, fila de perguntas.
7. Extras feitos: mesa de som, espectadores, emote wall, bomba, estilingue,
   estrelas, jogos de sorte, emotes animados e cenário fixo.
   Decidido não fazer: camadas HTML (WPE), plugin de OBS por memória
   compartilhada, Wayland nativo (roda pelo XWayland) e fundos/zips do SA.
   Para o futuro (seção no PLANO.md): enquetes com grupinhos de avatares,
   ler em voz alta, duelo e minijogos grandes, pendências pequenas.
