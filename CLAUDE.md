# Kikarinhas — notas para o Claude

Avatares do chat numa janela ARGB que o OBS captura (tipo Stream Avatars),
nativo X11, em C11 + Xlib + Cairo + Pango + libcurl. Leia o `PLANO.md`
antes de começar uma fase: ele tem a arquitetura, o estado de cada fase e as
lições aprendidas (formato do Stream Avatars, protocolo do YouTube).

O usuário escreve em português (pt-BR) e quer as respostas em português.
Mensagens para o usuário, logs e textos da interface ficam em português;
comentários e identificadores no código, em inglês.

## Compilar e testar

```sh
make            # build/kikarinhas, sem avisos (-Werror -Wpedantic etc.)
make test       # testes de unidade (tests/unit/test_*.c)
make asan       # os testes com ASan/UBSan
make asan-run ARGS="--demo-chat"   # o programa com ASan
build/kikarinhas --check           # valida todas as folhas do SA instalado
build/kikarinhas --demo-chat -v    # chat de mentira, sem rede
build/kikarinhas -y <link-da-live> -v
build/kikarinhas -c /tmp/x.ini --socket $XDG_RUNTIME_DIR/kk-teste.sock
```

- Os vazamentos do fontconfig são esperados e estão em `tools/lsan.supp`.
- `make lint` precisa do cppcheck (não instalado aqui); funções proibidas:
  strcpy, strcat, sprintf, vsprintf, gets.
- Testes com arquivos de usuário: use `--users` apontando para um arquivo
  temporário já existente (vazio evita importar o SA), nunca o
  `~/.local/share/kikarinhas/users.tsv` do usuário; idem `-c` para o .ini.
- Socket em teste: caminho curto (`$XDG_RUNTIME_DIR/kk-*.sock`); o do
  scratchpad passa dos 108 bytes de um socket unix.
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
| `src/stage.c` | avatares na tela, caches de folhas (paleta, gear), dano, nomes, balões, interações |
| `src/avatar.c` | máquina de estados, animação, geometria (sprite, gear, nome, balão) |
| `src/sprite.c` | folhas: recorte, recolor, escala (pixel art fica 1x e amplia com nearest) |
| `src/sa.c` | leitura do Stream Avatars: avatares, gear, pivôs, paletas, userData |
| `src/youtube.c`, `src/http.c` | chat do YouTube (InnerTube) sobre libcurl multi |
| `src/commands.c` | registro genérico de comandos: aliases, papéis, esperas |
| `src/actions.c` | comandos padrão (avatar, color, gear, dance, hug, sound...) |
| `src/users.c` | `users.tsv`: escolhas de cada pessoa, gravação atômica |
| `src/demochat.c` | chat de mentira para testes |
| `src/ini.c` | .ini lido e editado no lugar (mantém comentários) |
| `src/config.c` | configuração em camadas, comandos padrão, `[command.NOME]` |
| `src/control.c` | socket unix: bridges (JSON por linha), `reload`, `ping`, `quit` |
| `config/kikarinhas-config.c` | configurador GTK2 (opcional no build) |

## Convenções

- Prefixo `kk_` em tudo que é público; um módulo por arquivo, cabeçalho com
  comentário explicando o porquê.
- Laço único sem threads; nada pode bloquear (rede só via `kk_http`).
- Desempenho importa (meta: poucos % de CPU com 30 avatares): o que é
  estático vai para superfícies em cache e só é copiado a cada quadro.
- Caminhos com `kk_pathf` (falha em vez de truncar).
- Commits em português, terminando com a linha Co-Authored-By.

## Próximas fases (ver PLANO.md)

5. Twitch (IRC anônimo) e Odysee; podem nascer como bridges no socket.
6. Camadas HTML (WPE WebKit). 7. Extras: mesa de som (gancho `on_sound` em
   main.c), zips do SA, emojis como imagem, reações do YouTube.
