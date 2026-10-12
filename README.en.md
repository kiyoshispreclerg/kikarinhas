# Kikarinhas

[Português](README.md) · **English**

Chat avatars walking around your livestream: everyone who talks in the chat
gets a little character that strolls, jumps and reacts in a transparent
window that OBS captures. Similar to Stream Avatars and Desktop Ponies, but
native to Linux (X11) and light on resources: C, Cairo and Pango.

> **Phases 4, 5 and 7 (partly).** Reads the public chat of a YouTube,
> Twitch, Kick or Odysee livestream (no login or key needed): each person who talks gets a Stream Avatars
> avatar that jumps and shows the message in a speech bubble. Through the
> chat, everyone picks an avatar, a color and accessories (saved for the next
> stream), makes the avatar dance, sit, hug or attack, and plays sounds from
> the sound board. Chat emoji, YouTube member emotes, Twitch emotes (BTTV,
> FFZ and 7TV included), Kick emotes (and 7TV), Odysee emotes and YouTube
> reactions fly across the screen (emote wall). Everything is configured in an `.ini` file
> or in `kikarinhas-config`, which also lists the viewers. Other programs
> send messages through a unix socket. See [PLANO.md](PLANO.md) (in Portuguese).

## Building

Dependencies (Debian/Ubuntu):

```sh
sudo apt install build-essential pkg-config libx11-dev libxext-dev \
    libcairo2-dev libpango1.0-dev libcurl4-openssl-dev libasound2-dev \
    libssl-dev libdbus-1-dev
sudo apt install libgtk2.0-dev   # optional: only for kikarinhas-config
sudo apt install fonts-noto-color-emoji   # colour emoji (emote wall and bubbles)
```

The emoji font is only needed at run time; without it (or another colour
emoji font), emoji show up white, without colour. For developers:
`tools/gen_emoji_table.py` regenerates `src/emoji_table.h` from the
`emoji-data.txt` of the `unicode-data` package (only when a new Unicode
version comes out).

```sh
make            # builds build/kikarinhas (and build/kikarinhas-config, with GTK2)
make test       # unit tests
make asan       # tests with AddressSanitizer/UBSan
make asan-run   # the program with AddressSanitizer/UBSan
```

## Installing

```sh
sudo make install PREFIX=/usr   # without PREFIX it goes to /usr/local
sudo make uninstall PREFIX=/usr
```

This installs `kikarinhas` and `kikarinhas-config` (the latter only if it was
built, i.e. with GTK2), the example `.ini` in `share/doc/kikarinhas/`, the
`.desktop` entries and the icon, so both programs show up in application
menus and launchers (the configurator as "Kikarinhas Settings"). If the menu
does not pick them up right away, run `update-desktop-database` or log out
and back in.

## Usage

The avatars come from several folders, read in this order (on a repeated
name the first one wins, so an avatar of yours can replace a Stream Avatars
one):

1. your own, `~/.local/share/kikarinhas/avatars` (the **Open my avatars
   folder** button of `kikarinhas-config` creates and opens it);
2. the ones in `paths` in the `.ini` (`--avatar-dir`, or **More avatar
   folders** in the Avatars tab), in the order given;
3. the Stream Avatars installation (the `data` folder inside the Proton
   prefix), looked up in the Steam libraries; if it is not found, point to
   it with `--sa-dir`, and `sa_dir = off` turns it off. Nothing there is
   copied or changed.

Each folder may hold a data folder (`streamavatars_json.txt`), a pack
(`data.txt`, the format of SA's `.zip` files, unzipped) or subfolders with
those. Without Stream Avatars, kikarinhas runs with whatever the other
folders have.

Want to **make your own avatars, gear and palettes**? The format is the Stream
Avatars one and is explained in [docs/avatares](docs/avatares/README.md) (in
Portuguese only for now), with a working example kit.

```sh
build/kikarinhas -y https://www.youtube.com/watch?v=ID   # a livestream's chat
build/kikarinhas -y @YourChannel        # waits for the channel to go live
build/kikarinhas -t yourchannel         # Twitch chat
build/kikarinhas -k yourchannel         # Kick chat
build/kikarinhas -o @YourChannel        # Odysee: waits for the channel to go live
build/kikarinhas -y @YourChannel -t yourchannel -t otherchannel   # several streams at once
build/kikarinhas --demo-chat            # fake chat, for testing
build/kikarinhas                        # 6 random avatars, 1280x720 window
build/kikarinhas -n 20 -s 1920x1080     # 20 avatars at 1080p
build/kikarinhas -a pikachu -a crewmate # chosen avatars
build/kikarinhas -m desktop             # over the desktop, clicks go through
build/kikarinhas --list                 # lists the avatars found
build/kikarinhas --check                # checks every spritesheet
```

| Option | |
|---|---|
| `-m, --mode obs\|desktop` | `obs` (default): ordinary window to capture; `desktop`: full-screen overlay that does not receive clicks |
| `-s, --size WxH` | window size in `obs` mode (default `1280x720`) |
| `-f, --fps N` | frames per second (default 30) |
| `-y, --youtube TARGET` | livestream or channel link, `@handle` or video id |
| `-t, --twitch CHANNEL` | Twitch channel: the name or the `twitch.tv/channel` link |
| `-k, --kick CHANNEL` | Kick channel: the name or the `kick.com/channel` link |
| `-o, --odysee TARGET` | Odysee: link to the stream or channel, `@channel` or the stream's claim id |
| `--demo-chat` | fake chat with made-up people |
| `--max N` | chat avatars on screen at once (default 30); whoever has been silent the longest leaves first |
| `--despawn S` | seconds of silence until the avatar leaves (default 300) |
| `-d, --default-avatar NAME` | everyone uses this avatar (default: one random avatar per person, always the same) |
| `-v, --verbose` | prints the messages in the terminal |
| `--users FILE` | where to store each person's choices |
| `--import-sa-users` | imports the Stream Avatars choices again |
| `--sa-dir FOLDER\|off` | Stream Avatars `data` folder; `off` doesn't use SA |
| `--avatar-dir FOLDER` | another avatar folder (may repeat) |
| `-n, --count N` | random avatars outside the chat (default 6, or 0 with chat or `-a`) |
| `-a, --avatar NAME` | shows this avatar; may be repeated |
| `--scale X` | avatar scale (default 2) |
| `--ground N` | pixels between the ground and the bottom edge (default: room for the name) |
| `--seed N` | random seed, to repeat the same scene |
| `-c, --config FILE` | configuration file (default `~/.config/kikarinhas/kikarinhas.ini`) |
| `--socket PATH\|off` | control socket (default `$XDG_RUNTIME_DIR/kikarinhas.sock`) |
| `--reload` | asks the running kikarinhas to reread its configuration and exits |

Several streams can be read at once, from one platform or several: repeat
`-y`/`-t`/`-k`/`-o`, separate them with commas in the `.ini` (`youtube =
@one, @two`, `twitch = channel1, channel2`, `kick = ...`, `odysee = ...`),
or use the **Add** button in the Chat
tab of `kikarinhas-config`, which builds up a list of streams (and tells
the platform from the link). The avatars and emotes of all of
them show up together; the same person in two streams of the same platform
is a single avatar. With `-v`, each message says where it came from
(`[twitch #channel]`, `[youtube @handle]`, `[kick kick.com/channel]`,
`[odysee @channel]`). On `reload`, only the streams
that entered or left the list connect or disconnect.

Right-click the window (`obs` mode) to open `kikarinhas-config` on the same
`.ini`; only one is opened at a time.

### Configurator languages

`kikarinhas-config` speaks the session's language (`LANGUAGE`, `LC_ALL` or
`LANG`): Brazilian Portuguese (`pt_BR`) and English (`en`; any other
language falls back to English). `kikarinhas` itself has no interface text.
To add a language, put its code in `po/LINGUAS`, create the `.po` with
`msginit -l xx -i po/kikarinhas.pot -o po/xx.po`, translate it and run `make`
(the `.mo` files land in `build/locale`, which the program finds next to its
executable; `make install` installs them). After changing strings in the
code, `make pot` refreshes the `.pot` and merges it into every `.po`.

To quit: close the window or press Ctrl+C. In `desktop` mode, which does not
receive clicks, use `pkill kikarinhas` or
`echo quit | socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/kikarinhas.sock`.

### Configuration

Everything the options do can also live in
`~/.config/kikarinhas/kikarinhas.ini`; command-line options win over the
file. [data/kikarinhas.ini](data/kikarinhas.ini) explains every key (the
comments are in Portuguese). The easiest way to edit it is
`kikarinhas-config` (GTK2): tabs Window, Avatars, Installed avatars, Chat,
Commands, Sounds, Emote wall and Viewers, and the **Save and apply** button.
It keeps the file's comments and only writes what differs from the default.
The **Installed avatars** tab lists the avatars of every folder, with a
thumbnail, cell size, animations (the ones `!emote` takes), palettes, gear
sets and the folder each one came from, with search. In the **Commands**
tab, double-clicking a command (or **Edit**) opens a dialog with what its
action needs: the text of a text command, the picture with its place, size
and a preview of where it lands in the window, the sound, the avatar, the
player filter of `!song`, the cooldowns and the minimum level.

In the Avatars tab, `show_names`/`name_position` (below or above the avatar)
and `show_bubbles` turn the name tag and the speech bubble on or off.
`name_font`/`name_size` and `bubble_font`/`bubble_size` set the font (Pango
family and style, such as `Sans Bold`) and the size in points of the name and
of the bubble. `name_idle_hide`/`name_idle_seconds` hide the name after that
many seconds without activity (arrival, message, command or bubble), leaving
only the drawing; it is separate from `despawn`, which removes the avatar.

To apply changes without closing the program: **Save and apply**,
`kikarinhas --reload` or `kill -HUP`. Commands, default avatar, avatar limit,
despawn time, fps, fake chat, `verbose`, the streams (YouTube, Twitch,
Kick and Odysee), name
visibility/position, bubbles and the bubble font change on the spot. Size,
mode, scale, ground, fixed avatars, the name font, the avatar folders,
the people file and the socket only change on restart, and the log warns
about it. Comments go on their own lines (`#` or `;`), never after a value.

Chat commands are configured in `[command.NAME]` sections:

```ini
# aliases replaces the default list; cooldown is the per-person wait, in
# seconds; role is the minimum level: anyone, member, trusted, mod or owner.
[command.dance]
aliases = danca, dança, baila
cooldown = 30
role = member

[command.attack]
enabled = no

# New command: !buzina plays the sound "buzina"; !pika becomes the pikachu avatar.
[command.buzina]
action = sound
data = buzina
global_cooldown = 10

[command.pika]
action = avatar
data = pikachu

# !discord shows a text in a bubble over the sender's avatar (even with
# message bubbles off). \n breaks the line and {name} becomes the sender's
# name; seconds is how long it stays (auto: by its length).
[command.discord]
action = text
data = Hi, {name}! Join the Discord:\ndiscord.gg/example
aliases = dc
seconds = 10

# !logo shows a picture (PNG, JPEG, animated GIF or BMP; a relative path is
# from the .ini's folder) centred at x% and y% of the window, at scale% of
# its size, for seconds seconds (default 5), fading in and out. Asking again
# while it shows starts its time over.
[command.logo]
action = image
data = ~/pictures/logo.png
x = 80
y = 20
scale = 50
seconds = 8
```

### Sound board

Each sound is a `[sound.NAME]` section with the file (wav, ogg or mp3),
aliases and its own volume, from 0 to 400%:

```ini
# overall volume
[soundboard]
volume = 100

[sound.buzina]
file = ~/sounds/horn.ogg
aliases = buz, corneta
volume = 80
```

In the chat: `!sound buzina`, `!sound buz` or just `!buzina`. The `!sound`
cooldown applies to all sounds together: whoever played one waits before
playing another. Audio goes out through ALSA (with PipeWire or PulseAudio,
through their plugin) as "ALSA plug-in [kikarinhas]", so OBS captures it as
desktop audio, or you can route that stream wherever you like. The decoders
are bundled (nothing to install besides `libasound2`).

In the **Sounds** tab of `kikarinhas-config`:
- **Add…** picks files; the command name comes from the file name.
- **Import from Stream Avatars** brings the sound board over, volumes included.
- Double-click a row to listen at its volume.
- **Level** measures each sound (the loudness of its non-silent parts) and
  sets the volume so they all sound alike without clipping. Listen and
  adjust if needed.

### Viewers

The people file stores, for everyone who has talked in the chat, the name,
first seen, most recent visit and choices. The **Viewers** tab of
`kikarinhas-config` lists everybody (with search and sorting) and changes
someone's avatar: double-clicking a person opens a grid with the thumbnails
of every avatar and a search box (or **Random**, back to the avatar always
picked for them). With kikarinhas running the change takes effect at once;
when it is closed, it is saved for next time.

**Levels.** Each message stores the level the chat badges give (owner,
moderator, member/subscriber; no badge is a common viewer), shown in the
**Level** column. The **Trusted** column marks trusted people by hand. A
command can require a minimum level (`role` in `[command.NAME]`, or "Who
can" in the Commands tab), from lowest to highest: `anyone`, `member`,
`trusted`, `mod` and `owner`. Trusted ranks above member, like the
"regulars" of other bots: a trusted-only command also works for moderators
and the owner, but not for members who were not marked.

People who came from Stream Avatars before this version have no name or
dates in the file: with kikarinhas **closed**, run
`kikarinhas --import-sa-users` once. It fills in the name and dates
(`displayName`, `firstTimeSpawned` and `lastTimeUsed` from there) without
changing any choice.

### Points

Each viewer has a balance of points, kept in the people file. The **Points**
tab of `kikarinhas-config` lists everything that gives and takes points (0
turns each way of earning off):

| Earns | Default |
|---|---|
| each message, at most once every 30 s (`message`, `message_cooldown`) | 1 |
| every 5 minutes with their avatar on screen (`watch`, `watch_minutes`) | 5 |
| the first message of each session (`arrival`) | 10 |
| Super Chat, Super Sticker, bits (`paid`) | 100 |
| new member or subscriber, renewal, gift (`member`) | 100 |
| someone never seen starts with (`start`) | 0 |
| each star caught from `!spawn` (`catch`; a gem is worth three times as much) | 5 |

Spends: each command can have a cost (`cost` in `[command.NAME]`) and so can
each sound (`cost` in `[sound.NAME]`; without it, `!sound`'s cost applies),
plus the `!name` shortcut (`shortcut_cost` in `[commands]`). The cost is
only taken when the command does something, and the channel owner never
pays. Someone without the points sees "Custa N pontos; você tem M" in a
bubble. Commands: `!points` (`!pontos`) shows the balance, `!give @name N`
(`!doar`) passes points to someone on screen, `!award @name N` (`!premiar`,
moderators only; negative takes) creates points and `!top` (`!ranking`)
shows the five with the most. In the Viewers tab, the **Points** column
changes a balance by hand.

```ini
[points]
name = coins
watch = 10
watch_minutes = 10

[command.jump]
cost = 5

[sound.buzina]
file = ~/sounds/horn.ogg
cost = 50
```

### Timers

A timer runs a command by itself now and then, in `[timer.NAME]` sections
or the **Timers** tab of `kikarinhas-config`. It runs when **every**
condition set holds (`minutes` or `messages` is required):

- `minutes`: minutes since it last ran (or since kikarinhas started);
- `messages`: chat messages since then;
- `viewers`: at least that many people of the chat with an avatar on screen.

The command runs as if the channel owner typed it: no cooldown, no cost.
Bubble commands (`text`, `!song`...) show over someone on screen.

```ini
# The logo every 15 minutes.
[timer.logo]
command = !logo
minutes = 15

# The Discord link every 10 minutes, but only if the chat moved.
[timer.discord]
command = !discord
minutes = 10
messages = 30
viewers = 3
```

### Emote wall

Chat emoji fly across the screen, over the avatars: common emoji (drawn
from the emoji font), the channel's YouTube member emotes, Twitch emotes and
the channel's BTTV, FFZ and 7TV emotes, Kick emotes (and the channel's 7TV
ones on Kick) and Odysee emotes and stickers, downloaded once and kept in
`~/.cache/kikarinhas/emotes` (`third_party_emotes = no` in `[chat]` turns
off BTTV, FFZ and 7TV), and, if you want, YouTube
reactions (the floating hearts), mixed in with the rest. Animated emotes
(GIFs on Twitch, BTTV, 7TV and Kick) move on screen and in the bubbles; the
ones only served as WebP (some FFZ ones) stay still. Three effects:
`rise` (they rise in waves), `bounce` (diagonals bouncing off the edges,
like the DVD logo) and `fly` (they cross the screen).

What goes up is decided per message, and one rule is enough:
- **per message**: a message with at least `min_per_message` emoji sends
  all of them;
- **combo**: the same emoji in `combo_count` messages within
  `combo_window` seconds starts a combo, and while it lasts every message
  with that emoji goes up too.

```ini
[emote_wall]
enabled = yes
# rise, bounce or fly
style = rise
# seconds on screen and size in pixels
duration = 5
size = 48
# 0 turns a rule off; with both at 0, every emoji goes up
min_per_message = 3
combo_count = 3
combo_window = 10
max_per_message = 10
max_on_screen = 150
# YouTube reactions
reactions = yes
reactions_per_icon = 1
# emoji, shortcuts or names that never go up
blacklist = 🍆, :_spam:, Kappa
```

All of this is also in the **Emote wall** tab of `kikarinhas-config`. In
the blacklist, `❤` and `❤️` are the same emoji, and `:_hi:`, `:hi:` and `hi`
are the same shortcut.

### Control socket and bridges

kikarinhas listens on `$XDG_RUNTIME_DIR/kikarinhas.sock` (only your user can
access it). Each line is a JSON object and gets one line of reply:

```sh
S=$XDG_RUNTIME_DIR/kikarinhas.sock
echo '{"type":"message","platform":"twitch","user_id":"123","name":"Jane","text":"hi !jump","badges":["subscriber"]}' \
    | socat - UNIX-CONNECT:$S        # {"ok":true}
echo reload | socat - UNIX-CONNECT:$S
echo ping   | socat - UNIX-CONNECT:$S
echo '{"type":"play","sound":"buzina"}' | socat - UNIX-CONNECT:$S   # stop to stop
```

Other requests: `save` writes the people file now, and
`{"type":"set_user","user":"youtube:UC...","avatar":"pikachu","trusted":true,"points":100}`
changes someone's avatar (`""` goes back to the random one), marks them as
trusted and/or sets their points (each field is
optional; it is what the Viewers tab uses; `set_avatar` is the old name).

In `message`: `user_id` is required; `platform` (default `bridge`), `name`,
`text`, `kind` (`text`, `paid` or `member`), `amount` and `badges` (`owner`,
`mod`, `member`, `verified`, and also `broadcaster`, `moderator` and
`subscriber`) are optional. That way a platform kikarinhas does not know can
become a bridge written in any language.

Common emoji go in the `text` itself. Emotes that are images (Twitch's,
member emotes, 7TV...) go in `emotes`, with the URL of a PNG or GIF (`https` only)
and, optionally, where they are in the text (`start` and `len`, in bytes): with
that the emote shows as an image, animated if it is a GIF, inside the message bubble.
Reactions go on their own:

```sh
echo '{"type":"message","platform":"twitch","user_id":"123","text":"hi Kappa","emotes":[{"id":"25","name":"Kappa","url":"https://static-cdn.jtvnw.net/emoticons/v2/25/default/dark/2.0","start":3,"len":5}]}' \
    | socat - UNIX-CONNECT:$S
echo '{"type":"reaction","platform":"x","emoji":"❤","count":3}' | socat - UNIX-CONNECT:$S
```

### Chat commands

| Command | Also | What it does | Per-person cooldown |
|---|---|---|---|
| `!avatar NAME` | `!personagem`, `!char`, or just `!NAME` | changes avatar (`random` picks one) | 5 s |
| `!color PALETTE` | `!cor`, `!paleta`, or just `!PALETTE` | changes the color (`none` restores the original) | 5 s |
| `!gear PIECE` | `!item`, `!acessorio`, or just `!PIECE` | puts on an accessory (`none` removes everything) | 5 s |
| `!jump` | `!pula` | jumps | 3 s |
| `!sit` | `!senta` | sits | 10 s |
| `!dance` | `!danca`, `!dança` | dances (or another animation, if there is no dance) | 60 s |
| `!emote NAME` | `!anim` | any extra animation of the avatar | 15 s |
| `!hug [@name]` | `!abraco`, `!abraço` | walks to someone and hugs | 60 s |
| `!attack [@name]` | `!ataque`, `!bater` | walks to someone and attacks | 120 s |
| `!sound NAME` | `!som`, `!play`, `!sfx`, or just `!NAME` | plays a sound from the sound board | 30 s (3 s for everyone) |
| `!help` | `!ajuda`, `!comandos`, `!commands` | a bubble with a few random commands | 10 s (2 s for everyone) |
| `!song` | `!musica`, `!música`, `!np`, `!tocando` | a bubble with the music playing on the computer | 10 s (3 s for everyone) |
| `!points` | `!pontos`, `!saldo`, `!carteira` | a bubble with the points balance | 10 s |
| `!give @name N` | `!doar`, `!dar`, `!presentear` | passes N points to someone on screen | 30 s |
| `!award @name N` | `!premiar`, `!bonus` | creates (or takes) someone's points; moderators only | — |
| `!top` | `!ranking`, `!rank` | the five with the most points | 30 s (5 s for everyone) |
| `!left [N]` / `!right [N]` | `!esquerda` / `!direita` | walks N% of the screen width (default 15) | 3 s |
| `!move [N]` | `!mover`, `!andar` | walks to N% of the screen (0 is the left edge); without N, anywhere | 3 s |
| `!stand` | `!levantar`, `!levanta` | stands up, if sitting | 3 s |
| `!sling [angle] [power]` | `!estilingue`, `!lancar` | slingshots your own avatar: angle 1 to 179 (90 is straight up), power 1 to 100 | 30 s |
| `!bomb [@name]` | `!bomba` | throws a bomb at someone (or anyone); costs 10 points | 30 s |
| `!roll [N]` | `!dado`, `!rolar` | a number from 1 to 100, 1 to N, or dice (`!roll 2d6`) | 10 s |
| `!bola8 [question]` | `!8ball`, `!bola` | the magic 8-ball answers | 10 s |
| `!slots N` | `!cacaniquel`, `!slot` | slot machine: bets N points | 30 s |
| `!bet N TYPE` | `!aposta`, `!roleta` | a roulette bet (see below) | 4 s |
| `!freeze [@name]` | `!congelar` | freezes (or unfreezes) the avatar in place; moderators only | — |
| `!scale [@name] [N]` | `!tamanho`, `!escala` | changes the size (`150`, `150%` or `1.5`; 25% to 400%; without N, back to normal); moderators only | — |
| `!spawn [N]` | `!estrelas` | releases N stars to catch (default 5); moderators only | 10 s for everyone |

#### Fun and games

The numbers follow Stream Avatars'.

- **Bomb** (`!bomb`): flies in an arc from the thrower's hand, the fuse
  burns, and the blast throws everyone near it into the air, tumbling.
- **Slingshot** (`!sling`): launches your own avatar, which bounces off the
  walls and the ground until it settles. Flying avatars don't walk, sit or
  hug.
- **Stars** (`!spawn`): they float a little above the avatars' heads, where
  only a jump reaches. Whoever touches one takes it and earns the `catch`
  points of `[points]` (5 by default; a rare gem is worth three times as
  much). Walking under them (`!left`, `!right`, `!move`) and jumping
  (`!jump`) works, and so does being thrown by a bomb or the slingshot. They
  fade away after 90 s. To release them on their own every so often, use a
  timer (`[timer.stars]` with `command = !spawn` and `minutes = 15`).
- **Slot machine** (`!slots N`): 46% of spins win. Three cherries give the
  bet back, oranges half of it, sevens twice, coins 4x and bags 10x; on
  average 95% of what is bet comes back.
- **Roulette** (`!bet N TYPE`, or `TYPE N`): the first bet opens a 20 s
  round (`roulette_seconds`); whoever bets meanwhile joins the same round,
  and the ball lands on 0 to 36. A number or `verde` (green, 0) pays 36x;
  `vermelho`/`preto` (red/black), `par`/`ímpar` (even/odd) and `baixo`
  (1-18)/`alto` (19-36) pay 2x; `1a12`, `2a12` and `3a12` (dozens) pay 3x.
  The English words (`red`, `black`, `odd`, `even`, `low`, `high`, `green`,
  `1st12`...) work too. Zero loses every bet not on it. Each player sees
  the result in a bubble.
- **Limits**: bets go from `min_bet` to `max_bet` points (5 and 5000;
  `max_bet = 0` removes the limit), and `tudo`/`all` bets as much as
  possible. The bet leaves the balance at once; if the program closes with
  a round open, it is given back.
- `!freeze`, `!scale` and bomb effects last until the end of the session:
  they are not saved for the next stream.

`!song` shows the music playing on the streamer's computer, the same the
Linux panels show (MPRIS over D-Bus: Spotify, VLC, Audacious, browsers...).
With more than one player, the one that started playing last wins; if
nothing plays, a paused one does. Careful: a browser tab playing a video
counts too. To limit it to one player, put part of its name in the
command's `data` (`[command.song]` with `data = spotify`); whatever people
type after the command is ignored. Over the socket, `{"type":"nowplaying"}`
answers the same.

`!help` shows, in a blue bubble over the requester's avatar, a few random
commands out of the ones that person may use, sounds included (one by one).
Every request draws again, so whoever wants to learn them all has to ask
many times. It works even with `show_bubbles = no`. In `[commands]`:
`help_bubbles = no` turns it off and `help_count` (default 3, up to 20)
sets how many show; both are in the configurator's Commands tab.
`help_seconds` and, in `[avatars]`, `bubble_seconds` set how many seconds the
help bubble and the message bubble stay on screen (`auto`, the default: 4 to
12 s by the text length, and 4 s plus 1.5 s per command for help).

A full-width `!` (`！`, common on Japanese keyboards) works too. The channel
owner never waits. A command that does nothing (wrong name) does not start
the cooldown; a message with a command does not appear in a bubble.

Each person's choices are stored in `~/.local/share/kikarinhas/users.tsv`.
The first time, Kikarinhas imports the choices people had in Stream Avatars
(YouTube and Twitch); after that, `--import-sa-users` brings in only whoever
is not there yet.

### In OBS

1. Add a **Window Capture (Xcomposite)** source.
2. Choose the **Kikarinhas** window and check **Allow transparency**.
3. The background should disappear, leaving only the avatars and the names.

A running compositor is needed (KWin's will do) for the window to look
transparent on screen too.

## Planned

- Stream Avatars .zip packs and backgrounds.
- Optional HTML layers (WPE WebKit) to replace some obs-browser sources.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE). Includes
[cJSON](vendor/cjson) (MIT), [dr_wav and dr_mp3](vendor/dr_libs) (public
domain or MIT-0) and [stb_vorbis and stb_image](vendor/stb) (public domain or
MIT). The list of Odysee emotes and stickers (`src/odysee_emotes.h`) comes
from [odysee-frontend](https://github.com/OdyseeTeam/odysee-frontend) (MIT).

YouTube chat is read from the same endpoints the browser's pop-out chat
window uses; it is not an official API and may change. Twitch chat is read
over IRC, as an anonymous guest. Kick chat comes from the same websocket
(Pusher) the site uses, and Odysee chat from its comments websocket
(Commentron); these are not official APIs either.

The avatar artwork belongs to its authors and is not part of Kikarinhas.
