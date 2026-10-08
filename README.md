# Kikarinhas

**Português** · [English](README.en.md)

Avatares do chat andando na sua live: cada pessoa que fala no chat ganha um
bonequinho que passeia, pula e reage numa janela transparente que o OBS
captura. Parecido com o Stream Avatars e o Desktop Ponies, mas nativo para
Linux (X11) e leve: C, Cairo e Pango.

> **Fases 4, 5 e 7 (em parte).** Lê o chat público de uma live do YouTube,
> da Twitch, do Kick ou do Odysee (sem login nem chave): cada pessoa que
> fala ganha um avatar do Stream Avatars,
> que pula e mostra a mensagem num balão. Pelo chat, cada um escolhe avatar,
> cor e acessórios (guardados para a próxima live), manda o avatar dançar,
> sentar, abraçar ou atacar, e toca sons da mesa de som. Os emojis do chat,
> os emotes de membros do YouTube, os da Twitch (inclusive BTTV, FFZ e 7TV),
> do Kick (e 7TV) e do Odysee e as reações do YouTube voam pela tela (emote
> wall).
> Tudo se configura num `.ini` ou no `kikarinhas-config`, que também lista
> os espectadores. Outros programas mandam mensagens por um socket unix. Veja o
> [PLANO.md](PLANO.md).

## Compilar

Dependências (Debian/Ubuntu):

```sh
sudo apt install build-essential pkg-config libx11-dev libxext-dev \
    libcairo2-dev libpango1.0-dev libcurl4-openssl-dev libasound2-dev \
    libssl-dev libdbus-1-dev
sudo apt install libgtk2.0-dev   # opcional: só para o kikarinhas-config
sudo apt install fonts-noto-color-emoji   # emojis coloridos (emote wall e balões)
```

A fonte de emoji é usada só na hora de rodar; sem ela (ou outra fonte de
emoji colorido), os emojis aparecem em branco, sem cor. Para quem mexe no
código: `tools/gen_emoji_table.py` regenera `src/emoji_table.h` a partir do
`emoji-data.txt` do pacote `unicode-data` (só quando sair um Unicode novo).

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

Os avatares vêm de várias pastas, lidas nesta ordem (com nome repetido, vale
a primeira, então um avatar seu pode substituir um do Stream Avatars):

1. a sua, `~/.local/share/kikarinhas/avatars` (o botão **Abrir a minha pasta
   de avatares** do `kikarinhas-config` cria e abre);
2. as de `paths` no `.ini` (`--avatar-dir`, ou **Outras pastas de avatares**
   na aba Avatares), na ordem dada;
3. a instalação do Stream Avatars (a pasta `data` dentro do prefixo do
   Proton), procurada nas bibliotecas do Steam; se não for achada, indique
   com `--sa-dir`, e `sa_dir = off` desliga. Nada é copiado nem alterado lá.

Cada pasta pode ter uma pasta de dados (`streamavatars_json.txt`), um pacote
(`data.txt`, o formato dos `.zip` do SA, já descompactado) ou subpastas com
eles. Sem o Stream Avatars, o kikarinhas roda com o que houver nas outras.

Quer **criar os seus próprios avatares, acessórios e paletas**? O formato é o
do Stream Avatars e está explicado em [docs/avatares](docs/avatares/README.md)
(em português), com um kit de exemplo que funciona.

```sh
build/kikarinhas -y https://www.youtube.com/watch?v=ID   # chat de uma live
build/kikarinhas -y @SeuCanal           # espera o canal entrar ao vivo
build/kikarinhas -t seucanal            # chat da Twitch
build/kikarinhas -k seucanal            # chat do Kick
build/kikarinhas -o @SeuCanal           # Odysee: espera o canal entrar ao vivo
build/kikarinhas -y @SeuCanal -t seucanal -t outrocanal   # várias lives juntas
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
| `-t, --twitch CANAL` | canal da Twitch: o nome ou o link `twitch.tv/canal` |
| `-k, --kick CANAL` | canal do Kick: o nome ou o link `kick.com/canal` |
| `-o, --odysee ALVO` | Odysee: link da live ou do canal, `@canal` ou o id da live (claim id) |
| `--demo-chat` | chat de mentira com gente inventada |
| `--max N` | avatares do chat ao mesmo tempo (padrão 30); quem está calado há mais tempo sai primeiro |
| `--despawn S` | segundos em silêncio até o avatar sair (padrão 300) |
| `-d, --default-avatar NOME` | todos usam este avatar (padrão: um sorteado por pessoa, sempre o mesmo) |
| `-v, --verbose` | mostra as mensagens no terminal |
| `--users ARQ` | onde guardar as escolhas de cada pessoa |
| `--import-sa-users` | importa as escolhas do Stream Avatars de novo |
| `--sa-dir PASTA\|off` | pasta `data` do Stream Avatars; `off` não usa o SA |
| `--avatar-dir PASTA` | outra pasta de avatares (pode repetir) |
| `-n, --count N` | avatares sorteados fora do chat (padrão 6, ou 0 com chat ou `-a`) |
| `-a, --avatar NOME` | mostra este avatar; pode repetir |
| `--scale X` | escala dos avatares (padrão 2) |
| `--ground N` | pixels entre o chão e a borda de baixo (padrão: espaço do nome) |
| `--seed N` | semente do sorteio, para repetir a mesma cena |
| `-c, --config ARQ` | arquivo de configuração (padrão `~/.config/kikarinhas/kikarinhas.ini`) |
| `--socket CAMINHO\|off` | socket de controle (padrão `$XDG_RUNTIME_DIR/kikarinhas.sock`) |
| `--reload` | pede ao kikarinhas aberto para reler a configuração e sai |

Dá para ler várias lives ao mesmo tempo, de uma plataforma ou de várias:
repita `-y`/`-t`/`-k`/`-o`, separe por vírgula no `.ini` (`youtube = @um,
@dois`, `twitch = canal1, canal2`, `kick = ...`, `odysee = ...`) ou use o
botão **Adicionar** da aba Chat do
`kikarinhas-config`, que vai juntando as lives numa lista (e reconhece a
plataforma pelo link). Os avatares e emotes de todas aparecem juntos;
a mesma pessoa em duas lives da mesma plataforma é um avatar só. Com `-v`,
cada mensagem diz de onde veio (`[twitch #canal]`, `[youtube @handle]`,
`[kick kick.com/canal]`, `[odysee @canal]`). No
`reload`, só as lives que entraram ou saíram da lista conectam ou
desconectam.

Clique com o botão direito na janela (modo `obs`) para abrir o
`kikarinhas-config` com o mesmo `.ini`; só abre um por vez.

### Idiomas do configurador

O `kikarinhas-config` fala o idioma da sessão (`LANGUAGE`, `LC_ALL` ou
`LANG`): português do Brasil (`pt_BR`) e inglês (`en`; qualquer outro
idioma cai no inglês). O `kikarinhas` em si não tem textos de interface.
Para acrescentar um idioma, adicione o código em `po/LINGUAS`, crie o `.po`
com `msginit -l xx -i po/kikarinhas.pot -o po/xx.po`, traduza e rode `make`
(os `.mo` saem em `build/locale`, que o programa acha ao lado do executável;
`make install` os instala). Depois de mudar textos no código, `make pot`
atualiza o `.pot` e mescla nos `.po`.

Para sair: feche a janela ou use Ctrl+C. No modo `desktop`, que não recebe
cliques, use `pkill kikarinhas` ou `echo quit | socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/kikarinhas.sock`.

### Configuração

Tudo o que as opções fazem também pode ficar em
`~/.config/kikarinhas/kikarinhas.ini`; as opções da linha de comando valem
por cima do arquivo. O [data/kikarinhas.ini](data/kikarinhas.ini) explica
cada chave. O jeito mais fácil de editar é o `kikarinhas-config` (GTK2):
abas Janela, Avatares, Avatares instalados, Chat, Comandos, Sons, Emote wall
e Espectadores, e o botão **Salvar e aplicar**. Ele mantém os comentários do
arquivo e só grava o que for diferente do padrão. A aba **Avatares
instalados** lista os avatares de todas as pastas, com miniatura, tamanho da
célula, animações (as que o `!emote` aceita), paletas, conjuntos de gear e a
pasta de onde cada um veio, com busca. Na aba **Comandos**, clicar duas
vezes num comando (ou **Editar**) abre um diálogo com o que a ação dele
precisa: o texto de um comando de texto, a imagem com a posição, o tamanho e
uma prévia de onde ela fica na janela, o som, o avatar, o filtro de player
do `!song`, a espera e o nível mínimo.

Na aba Avatares, `show_names`/`name_position` (embaixo ou em cima do
avatar) e `show_bubbles` ligam ou desligam o nome e o balão com a mensagem
de cada um. `name_font`/`name_size` e `bubble_font`/`bubble_size` trocam a
fonte (família e estilo do Pango, como `Sans Bold`) e o tamanho em pontos do
nome e do balão.

Para aplicar sem fechar o programa: **Salvar e aplicar**, `kikarinhas
--reload` ou `kill -HUP`. Comandos, avatar padrão, limite de avatares,
tempo de sumiço, fps, chat de mentira, `verbose`, as lives (YouTube,
Twitch, Kick e Odysee), nomes/posição, balões e a fonte dos balões mudam na hora. Tamanho, modo,
escala, chão, avatares fixos, fonte dos nomes, pastas de avatares,
arquivo de pessoas e socket só mudam reiniciando, e o log avisa.
Comentários ficam em linhas próprias (`#` ou `;`), nunca depois de um valor.

Os comandos do chat são configurados em seções `[command.NOME]`:

```ini
# aliases substitui a lista padrão; cooldown é a espera por pessoa, em
# segundos; role é o nível mínimo: anyone, member, trusted, mod ou owner.
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

# !discord mostra um texto num balão sobre o avatar de quem pediu (mesmo com
# os balões de mensagem desligados). \n quebra a linha e {name} vira o nome
# de quem pediu; seconds é quanto tempo fica (auto: pelo tamanho).
[command.discord]
action = text
data = Oi, {name}! Entre no Discord:\ndiscord.gg/exemplo
aliases = dc
seconds = 10

# !logo mostra uma imagem (PNG, JPEG, GIF animado ou BMP; caminho relativo é a
# partir da pasta do .ini) com o centro em x% e y% da janela, em scale% do
# tamanho dela, por seconds segundos (padrão 5), aparecendo e sumindo aos
# poucos. Pedir de novo enquanto ela está na tela recomeça o tempo.
[command.logo]
action = image
data = ~/imagens/logo.png
x = 80
y = 20
scale = 50
seconds = 8
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
avatar de alguém: clicar duas vezes na pessoa abre uma grade com as
miniaturas de todos os avatares e uma caixa de busca (ou **Sorteado**, para
voltar ao avatar sorteado de sempre). Com o kikarinhas aberto, a troca vale
na hora; fechado, fica gravada para a próxima vez.

**Níveis.** Cada mensagem grava o nível que os selos do chat dão (dono,
moderador, membro/assinante; sem selo é comum), visto na coluna **Nível**.
A coluna **Confiável** marca à mão quem é de confiança. Um comando pode
exigir um nível mínimo (`role` em `[command.NOME]`, ou "Quem pode" na aba
Comandos), do menor para o maior: `anyone` (todos), `member` (membros),
`trusted` (confiáveis), `mod` (moderadores) e `owner` (dono). O confiável
fica acima do membro, como os "regulars" de outros bots: um comando só para
confiáveis também vale para moderadores e para o dono, mas não para membros
que não foram marcados.

Quem veio do Stream Avatars antes desta versão não tem nome nem datas no
arquivo: com o kikarinhas **fechado**, rode uma vez
`kikarinhas --import-sa-users`. Ele completa nome e datas (o `displayName`,
`firstTimeSpawned` e `lastTimeUsed` de lá) sem mudar nenhuma escolha.

### Pontos

Cada espectador tem um saldo de pontos, guardado no arquivo de pessoas. A aba
**Pontos** do `kikarinhas-config` lista tudo o que dá e tira pontos (0
desliga cada forma de ganhar):

| Ganha | Padrão |
|---|---|
| cada mensagem, no máximo uma vez a cada 30 s (`message`, `message_cooldown`) | 1 |
| a cada 5 minutos com o avatar na tela (`watch`, `watch_minutes`) | 5 |
| a primeira mensagem de cada sessão (`arrival`) | 10 |
| Super Chat, Super Sticker, bits (`paid`) | 100 |
| membro ou inscrito novo, renovação, presente (`member`) | 100 |
| quem nunca apareceu começa com (`start`) | 0 |

Gasta: cada comando pode ter um custo (`cost` em `[command.NOME]`) e cada som
também (`cost` em `[sound.NOME]`; sem ele, vale o custo do `!som`), além do
atalho `!nome` (`shortcut_cost` em `[commands]`). O custo só é cobrado quando
o comando faz alguma coisa, e o dono do canal nunca paga. Quem não tem pontos
vê "Custa N pontos; você tem M" no balão. Comandos: `!pontos` mostra o
saldo, `!doar @nome N` passa pontos para alguém que está na tela,
`!premiar @nome N` (só moderadores; negativo tira) cria pontos e `!ranking`
mostra os cinco com mais. Na aba Espectadores, a coluna **Pontos** muda um
saldo à mão.

```ini
[points]
name = moedas
watch = 10
watch_minutes = 10

[command.jump]
cost = 5

[sound.buzina]
file = ~/sons/buzina.ogg
cost = 50
```

### Timers

Um timer executa um comando sozinho de tempos em tempos, em seções
`[timer.NOME]` ou na aba **Timers** do `kikarinhas-config`. Ele roda quando
**todas** as condições preenchidas valem (é preciso `minutes` ou `messages`):

- `minutes`: minutos desde a última vez (ou desde que o kikarinhas abriu);
- `messages`: mensagens no chat desde a última vez;
- `viewers`: pelo menos tantas pessoas do chat com avatar na tela.

O comando roda como se o dono do canal tivesse digitado: sem espera e sem
custo. Comandos de balão (`text`, `!song`...) aparecem sobre alguém da tela.

```ini
# O logo a cada 15 minutos.
[timer.logo]
command = !logo
minutes = 15

# O link do Discord a cada 10 minutos, mas só se o chat andou.
[timer.discord]
command = !discord
minutes = 10
messages = 30
viewers = 3
```

### Emote wall

Os emojis do chat voam pela tela, por cima dos avatares: emojis comuns
(desenhados pela fonte de emoji), os emotes de membros do canal do YouTube,
os da Twitch e os do BTTV, FFZ e 7TV do canal, os do Kick (e os do 7TV do
canal no Kick) e os do Odysee (emotes e stickers), baixados uma vez e
guardados em `~/.cache/kikarinhas/emotes` (`third_party_emotes = no` em
`[chat]` desliga BTTV, FFZ e 7TV), e, se quiser,
as reações do YouTube (os corações flutuantes), misturadas com o resto.
Três efeitos: `rise` (sobem em ondas), `bounce` (diagonal quicando nas
bordas, como o logo do DVD) e `fly` (atravessam a tela).

O que sobe é decidido por mensagem, e basta uma das regras:
- **por mensagem**: uma mensagem com pelo menos `min_per_message` emojis
  manda todos;
- **combo**: o mesmo emoji em `combo_count` mensagens dentro de
  `combo_window` segundos começa um combo, e enquanto ele dura cada mensagem
  com esse emoji também sobe.

```ini
[emote_wall]
enabled = yes
# rise, bounce ou fly
style = rise
# segundos na tela e tamanho em pixels
duration = 5
size = 48
# 0 desliga a regra; com as duas em 0, todo emoji sobe
min_per_message = 3
combo_count = 3
combo_window = 10
max_per_message = 10
max_on_screen = 150
# reações do YouTube
reactions = yes
reactions_per_icon = 1
# emojis, atalhos ou nomes que nunca sobem
blacklist = 🍆, :_spam:, Kappa
```

Tudo isso também está na aba **Emote wall** do `kikarinhas-config`. Na
blacklist, `❤` e `❤️` são o mesmo emoji, e `:_oi:`, `:oi:` e `oi` são o mesmo
atalho.

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
`{"type":"set_user","user":"youtube:UC...","avatar":"pikachu","trusted":true,"points":100}`
troca o avatar de alguém (`""` volta ao sorteado), marca como confiável e/ou
muda o saldo de pontos (cada campo é opcional; é
o que a aba Espectadores usa; `set_avatar` é o nome antigo).

Em `message`: `user_id` é obrigatório; `platform` (padrão `bridge`), `name`,
`text`, `kind` (`text`, `paid` ou `member`), `amount` e `badges` (`owner`,
`mod`, `member`, `verified`, e também `broadcaster`, `moderator` e
`subscriber`) são opcionais. Assim, uma plataforma que o kikarinhas não
conhece pode virar uma bridge em qualquer linguagem.

Emojis comuns vão no próprio `text`. Emotes que são imagem (os da Twitch, de
membros, 7TV...) vão em `emotes`, com a URL de um PNG (só `https`) e, se
quiser, onde estão no texto (`start` e `len`, em bytes). Reações vão à
parte:

```sh
echo '{"type":"message","platform":"twitch","user_id":"123","text":"oi Kappa","emotes":[{"id":"25","name":"Kappa","url":"https://static-cdn.jtvnw.net/emoticons/v2/25/static/dark/2.0","start":3,"len":5}]}' \
    | socat - UNIX-CONNECT:$S
echo '{"type":"reaction","platform":"x","emoji":"❤","count":3}' | socat - UNIX-CONNECT:$S
```

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
| `!help` | `!ajuda`, `!comandos`, `!commands` | balão com alguns comandos sorteados | 10 s (2 s para todos) |
| `!song` | `!musica`, `!música`, `!np`, `!tocando` | balão com a música tocando no computador | 10 s (3 s para todos) |
| `!points` | `!pontos`, `!saldo`, `!carteira` | balão com o saldo de pontos | 10 s |
| `!give @nome N` | `!doar`, `!dar`, `!presentear` | passa N pontos para alguém na tela | 30 s |
| `!award @nome N` | `!premiar`, `!bonus` | cria (ou tira) pontos de alguém; só moderadores | — |
| `!top` | `!ranking`, `!rank` | os cinco com mais pontos | 30 s (5 s para todos) |

`!song` mostra a música que está tocando no computador de quem transmite,
a mesma que os painéis do Linux mostram (MPRIS pelo D-Bus: Spotify, VLC,
Audacious, navegadores...). Com mais de um player, vale o que começou a
tocar por último; se nada toca, vale um pausado. Cuidado: uma aba do
navegador tocando vídeo também conta. Para limitar a um player, ponha parte
do nome dele no `data` do comando (`[command.song]` com `data = spotify`); o
que a pessoa digita depois do comando é ignorado. No socket,
`{"type":"nowplaying"}` responde a mesma coisa.

`!help` mostra, num balão azul sobre o avatar de quem pediu, alguns comandos
sorteados entre os que essa pessoa pode usar, com os sons incluídos (um
por um). Cada pedido sorteia de novo, então quem quiser conhecer todos
precisa pedir várias vezes. Funciona mesmo com `show_bubbles = no`. Em
`[commands]`: `help_bubbles = no` desliga e `help_count` (padrão 3, até 20)
muda quantos aparecem; ambos estão na aba Comandos do configurador.
`help_seconds` e, em `[avatars]`, `bubble_seconds` definem quantos segundos
o balão de ajuda e o de mensagem ficam na tela (`auto`, o padrão: de 4 a 12 s
conforme o texto, e 4 s mais 1,5 s por comando na ajuda).

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

- Odysee.
- Pacotes .zip do Stream Avatars, emotes como imagem dentro dos balões,
  emotes animados (GIF).
- Camadas HTML opcionais (WPE WebKit) para substituir alguns obs-browser.

## Licença

GPL-3.0-or-later. Veja [LICENSE](LICENSE). Inclui o
[cJSON](vendor/cjson) (MIT), o [dr_wav e o dr_mp3](vendor/dr_libs) (domínio
público ou MIT-0) e o [stb_vorbis e o stb_image](vendor/stb) (domínio público ou
MIT). A lista de emotes e stickers do Odysee (`src/odysee_emotes.h`) vem do
[odysee-frontend](https://github.com/OdyseeTeam/odysee-frontend) (MIT).

O chat do YouTube é lido pelos mesmos endereços que o chat em janela
separada do navegador usa; não é uma API oficial e pode mudar. O da Twitch
é lido pelo IRC, como visitante anônimo. O do Kick vem do mesmo websocket
(Pusher) que o site usa, e o do Odysee do websocket de comentários
(Commentron); também não são APIs oficiais.

As artes dos avatares pertencem aos seus autores e não fazem parte do
Kikarinhas.
