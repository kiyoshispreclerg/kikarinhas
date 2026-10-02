# Kikarinhas

**Português** · [English](README.en.md)

Avatares do chat andando na sua live: cada pessoa que fala no chat ganha um
bonequinho que passeia, pula e reage numa janela transparente que o OBS
captura. Parecido com o Stream Avatars e o Desktop Ponies, mas nativo para
Linux (X11) e leve: C, Cairo e Pango.

> **Fases 4 e 7 (em parte).** Lê o chat público de uma live do YouTube (sem
> login nem chave): cada pessoa que fala ganha um avatar do Stream Avatars,
> que pula e mostra a mensagem num balão. Pelo chat, cada um escolhe avatar,
> cor e acessórios (guardados para a próxima live), manda o avatar dançar,
> sentar, abraçar ou atacar, e toca sons da mesa de som. Tudo se configura
> num `.ini` ou no `kikarinhas-config`, que também lista os espectadores.
> Outros programas mandam mensagens por um socket unix. Veja o
> [PLANO.md](PLANO.md).

## Compilar

Dependências (Debian/Ubuntu):

```sh
sudo apt install build-essential pkg-config libx11-dev libxext-dev \
    libcairo2-dev libpango1.0-dev libcurl4-openssl-dev libasound2-dev
sudo apt install libgtk2.0-dev   # opcional: só para o kikarinhas-config
```

```sh
make            # gera build/kikarinhas (e build/kikarinhas-config, com GTK2)
make test       # testes de unidade
make asan       # testes com AddressSanitizer/UBSan
make asan-run   # o programa com AddressSanitizer/UBSan
```

## Instalar

```sh
sudo make install PREFIX=/usr   # sem PREFIX, vai para /usr/local
sudo make uninstall PREFIX=/usr
```

Instala `kikarinhas` e `kikarinhas-config` (este só se foi compilado, com
GTK2), o `.ini` de exemplo em `share/doc/kikarinhas/`, os atalhos `.desktop`
e o ícone, então os dois programas aparecem nos menus e buscadores de
aplicativos (o configurador como "Configurações do Kikarinhas"). Se o menu
não mostrar na hora, rode `update-desktop-database` ou saia e entre na
sessão.

## Usar

Os avatares vêm da sua instalação do Stream Avatars (a pasta `data` dentro do
prefixo do Proton). Ela é procurada nas bibliotecas do Steam; se não for
achada, indique com `--sa-dir`. Nada é copiado nem alterado lá.

```sh
build/kikarinhas -y https://www.youtube.com/watch?v=ID   # chat de uma live
build/kikarinhas -y @SeuCanal           # espera o canal entrar ao vivo
build/kikarinhas --demo-chat            # chat de mentira, para testar
build/kikarinhas                        # 6 avatares sorteados, janela 1280x720
build/kikarinhas -n 20 -s 1920x1080     # 20 avatares em 1080p
build/kikarinhas -a pikachu -a crewmate # avatares escolhidos
build/kikarinhas -m desktop             # sobre o desktop, o clique atravessa
build/kikarinhas --list                 # lista os avatares encontrados
build/kikarinhas --check                # confere todas as spritesheets
```

| Opção | |
|---|---|
| `-m, --mode obs\|desktop` | `obs` (padrão): janela comum para capturar; `desktop`: sobreposição em tela cheia, sem receber cliques |
| `-s, --size LxA` | tamanho da janela no modo `obs` (padrão `1280x720`) |
| `-f, --fps N` | quadros por segundo (padrão 30) |
| `-y, --youtube ALVO` | link da live ou do canal, `@handle` ou id do vídeo |
| `--demo-chat` | chat de mentira com gente inventada |
| `--max N` | avatares do chat ao mesmo tempo (padrão 30); quem está calado há mais tempo sai primeiro |
| `--despawn S` | segundos em silêncio até o avatar sair (padrão 300) |
| `-d, --default-avatar NOME` | todos usam este avatar (padrão: um sorteado por pessoa, sempre o mesmo) |
| `-v, --verbose` | mostra as mensagens no terminal |
| `--users ARQ` | onde guardar as escolhas de cada pessoa |
| `--import-sa-users` | importa as escolhas do Stream Avatars de novo |
| `--sa-dir PASTA` | pasta `data` do Stream Avatars |
| `-n, --count N` | avatares sorteados fora do chat (padrão 6, ou 0 com chat ou `-a`) |
| `-a, --avatar NOME` | mostra este avatar; pode repetir |
| `--scale X` | escala dos avatares (padrão 2) |
| `--ground N` | pixels entre o chão e a borda de baixo (padrão: espaço do nome) |
| `--seed N` | semente do sorteio, para repetir a mesma cena |
| `-c, --config ARQ` | arquivo de configuração (padrão `~/.config/kikarinhas/kikarinhas.ini`) |
| `--socket CAMINHO\|off` | socket de controle (padrão `$XDG_RUNTIME_DIR/kikarinhas.sock`) |
| `--reload` | pede ao kikarinhas aberto para reler a configuração e sai |

Clique com o botão direito na janela (modo `obs`) para abrir o
`kikarinhas-config` com o mesmo `.ini`; só abre um por vez.

Para sair: feche a janela ou use Ctrl+C. No modo `desktop`, que não recebe
cliques, use `pkill kikarinhas` ou `echo quit | socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/kikarinhas.sock`.

### Configuração

Tudo o que as opções fazem também pode ficar em
`~/.config/kikarinhas/kikarinhas.ini`; as opções da linha de comando valem
por cima do arquivo. O [data/kikarinhas.ini](data/kikarinhas.ini) explica
cada chave. O jeito mais fácil de editar é o `kikarinhas-config` (GTK2):
abas Janela, Avatares, Chat, Comandos, Sons e Espectadores, e o botão
**Salvar e aplicar**. Ele mantém os comentários do arquivo e só grava o que
for diferente do padrão.

Na aba Avatares, `show_names`/`name_position` (embaixo ou em cima do
avatar) e `show_bubbles` ligam ou desligam o nome e o balão com a mensagem
de cada um. `name_font`/`name_size` e `bubble_font`/`bubble_size` trocam a
fonte (família e estilo do Pango, como `Sans Bold`) e o tamanho em pontos do
nome e do balão.

Para aplicar sem fechar o programa: **Salvar e aplicar**, `kikarinhas
--reload` ou `kill -HUP`. Comandos, avatar padrão, limite de avatares,
tempo de sumiço, fps, chat de mentira, `verbose`, o alvo do YouTube,
nomes/posição, balões e a fonte dos balões mudam na hora. Tamanho, modo,
escala, chão, avatares fixos, fonte dos nomes, pasta do Stream Avatars,
arquivo de pessoas e socket só mudam reiniciando, e o log avisa.
Comentários ficam em linhas próprias (`#` ou `;`), nunca depois de um valor.

Os comandos do chat são configurados em seções `[command.NOME]`:

```ini
# aliases substitui a lista padrão; cooldown é a espera por pessoa, em
# segundos; role é quem pode: anyone, member, mod ou owner.
[command.dance]
aliases = danca, dança, baila
cooldown = 30
role = member

[command.attack]
enabled = no

# Comando novo: !buzina toca o som "buzina"; !pika vira o avatar pikachu.
[command.buzina]
action = sound
data = buzina
global_cooldown = 10

[command.pika]
action = avatar
data = pikachu
```

### Mesa de som

Cada som é uma seção `[sound.NOME]` com o arquivo (wav, ogg ou mp3), apelidos
e um volume próprio, de 0 a 400%:

```ini
# volume geral
[soundboard]
volume = 100

[sound.buzina]
file = ~/sons/buzina.ogg
aliases = buz, corneta
volume = 80
```

No chat: `!som buzina`, `!som buz` ou só `!buzina`. A espera do `!som` vale
para todos os sons juntos: quem tocou um espera para tocar outro. O som sai
pelo ALSA (com PipeWire ou PulseAudio, pelo plugin deles) como "ALSA plug-in
[kikarinhas]", então o OBS captura como áudio da área de trabalho, ou você
direciona esse fluxo para onde quiser. Os decodificadores vêm junto (nada
para instalar além do `libasound2`).

Na aba **Sons** do `kikarinhas-config`:
- **Adicionar…** escolhe arquivos; o nome do comando sai do nome do arquivo.
- **Importar do Stream Avatars** traz a mesa de som de lá, com os volumes.
- Clique duas vezes numa linha para ouvir com o volume dela.
- **Nivelar** mede cada som (a intensidade das partes não silenciosas) e
  acerta o volume para todos soarem parecidos, sem estourar. Ouça e ajuste
  se precisar.

### Espectadores

O arquivo de pessoas guarda, de quem já falou no chat, o nome, a primeira
vez, a vez mais recente e as escolhas. A aba **Espectadores** do
`kikarinhas-config` lista todo mundo (com busca e ordenação) e troca o
avatar de alguém: com o kikarinhas aberto, a troca vale na hora; fechado,
fica gravada para a próxima vez.

Quem veio do Stream Avatars antes desta versão não tem nome nem datas no
arquivo: com o kikarinhas **fechado**, rode uma vez
`kikarinhas --import-sa-users`. Ele completa nome e datas (o `displayName`,
`firstTimeSpawned` e `lastTimeUsed` de lá) sem mudar nenhuma escolha.

### Socket de controle e bridges

O kikarinhas escuta em `$XDG_RUNTIME_DIR/kikarinhas.sock` (só o seu usuário
acessa). Cada linha é um JSON e recebe uma linha de resposta:

```sh
S=$XDG_RUNTIME_DIR/kikarinhas.sock
echo '{"type":"message","platform":"twitch","user_id":"123","name":"Fulana","text":"oi !jump","badges":["subscriber"]}' \
    | socat - UNIX-CONNECT:$S        # {"ok":true}
echo reload | socat - UNIX-CONNECT:$S
echo ping   | socat - UNIX-CONNECT:$S
echo '{"type":"play","sound":"buzina"}' | socat - UNIX-CONNECT:$S   # stop para parar
```

Outros pedidos: `save` grava o arquivo de pessoas agora, e
`{"type":"set_avatar","user":"youtube:UC...","avatar":"pikachu"}` troca o
avatar de alguém (é o que a aba Espectadores usa).

Em `message`: `user_id` é obrigatório; `platform` (padrão `bridge`), `name`,
`text`, `kind` (`text`, `paid` ou `member`), `amount` e `badges` (`owner`,
`mod`, `member`, `verified`, e também `broadcaster`, `moderator` e
`subscriber`) são opcionais. Assim, uma plataforma que o kikarinhas não
conhece pode virar uma bridge em qualquer linguagem.

### Comandos do chat

| Comando | Também | O que faz | Espera por pessoa |
|---|---|---|---|
| `!avatar NOME` | `!personagem`, `!char`, ou só `!NOME` | troca de avatar (`random` sorteia) | 5 s |
| `!color PALETA` | `!cor`, `!paleta`, ou só `!PALETA` | troca a cor (`none` volta ao original) | 5 s |
| `!gear PEÇA` | `!item`, `!acessorio`, ou só `!PEÇA` | veste um acessório (`none` tira tudo) | 5 s |
| `!jump` | `!pula` | pula | 3 s |
| `!sit` | `!senta` | senta | 10 s |
| `!dance` | `!danca`, `!dança` | dança (ou outra animação, se não tiver) | 60 s |
| `!emote NOME` | `!anim` | qualquer animação extra do avatar | 15 s |
| `!hug [@nome]` | `!abraco`, `!abraço` | vai até alguém e abraça | 60 s |
| `!attack [@nome]` | `!ataque`, `!bater` | vai até alguém e ataca | 120 s |
| `!sound NOME` | `!som`, `!play`, `!sfx`, ou só `!NOME` | toca um som da mesa de som | 30 s (3 s para todos) |

`!` de largura total (`！`, comum em teclados japoneses) também vale. O dono
do canal não tem espera. Comando que não faz nada (nome errado) não conta a
espera; mensagem com comando não aparece em balão.

As escolhas de cada pessoa ficam em `~/.local/share/kikarinhas/users.tsv`. Na
primeira vez, o Kikarinhas importa as escolhas que o pessoal tinha no Stream
Avatars (YouTube e Twitch); depois, `--import-sa-users` traz só quem ainda
não estiver lá.

### No OBS

1. Adicione uma fonte **Captura de janela (Xcomposite)**.
2. Escolha a janela **Kikarinhas** e marque **Permitir transparência**.
3. O fundo deve sumir, ficando só os avatares e os nomes.

Precisa de um compositor ativo (o do KWin serve) para a janela parecer
transparente também na tela.

## Previsto

- Twitch e Odysee.
- Pacotes .zip do Stream Avatars, emojis como imagem.
- Camadas HTML opcionais (WPE WebKit) para substituir alguns obs-browser.

## Licença

GPL-3.0-or-later. Veja [LICENSE](LICENSE). Inclui o
[cJSON](vendor/cjson) (MIT), o [dr_wav e o dr_mp3](vendor/dr_libs) (domínio
público ou MIT-0) e o [stb_vorbis](vendor/stb) (domínio público ou MIT).

O chat do YouTube é lido pelos mesmos endereços que o chat em janela
separada do navegador usa; não é uma API oficial e pode mudar.

As artes dos avatares pertencem aos seus autores e não fazem parte do
Kikarinhas.
