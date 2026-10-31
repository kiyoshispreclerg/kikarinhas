# Kikarinhas

[Português](README.pt-BR.md) · **English**

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

![Avatars with speech bubbles and emotes flying over the stage](docs/screenshots/chat.png)

The same chat as **danmaku**, flying right to left (see [Danmaku](#danmaku)):

![Chat messages flying across the screen as danmaku](docs/screenshots/danmaku.png)

*The avatars are the default pack that comes with kikarinhas, over a
backdrop made with the scenery feature; the people and lines are made up
(`tools/screenshots.py` takes these pictures).*

## Building

Dependencies (Debian/Ubuntu):

```sh
sudo apt install build-essential pkg-config libx11-dev libxext-dev \
    libcairo2-dev libpango1.0-dev libcurl4-openssl-dev libasound2-dev \
    libssl-dev libdbus-1-dev libwebp-dev
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

To install what you built (without `PREFIX` it goes to `/usr/local`):

```sh
sudo make install PREFIX=/usr
sudo make uninstall PREFIX=/usr
```

This puts the same files as the `.deb` in place (`kikarinhas-config` only if it
was built, i.e. with GTK2). `tools/build-deb.sh` builds a `.deb` from the tree.

## Installing

The easiest way is the `.deb` package from the
[releases page](https://github.com/kiyoshispreclerg/kikarinhas/releases).
Each release has one for Ubuntu 22.04, 24.04 and 26.04, Devuan 5 "daedalus" (also
fits Debian 12) and Devuan 6 "excalibur" (also fits Debian 13); pick the one
that matches your system. They are named
`kikarinhas_VERSION-1_SYSTEM_ARCH.deb`, and each one was installed and run on
a clean system of its own kind before being published.

```sh
sudo apt install ./kikarinhas_0.4.0-1_ubuntu24.04_amd64.deb   # use the file you downloaded
sudo apt remove kikarinhas
```

There is also an AppImage (`kikarinhas-VERSION-x86_64.AppImage`) that runs
without installing anything: `chmod +x` it and run it, or run it with
`--config` for the settings window. It still needs the system's X11 and
fontconfig libraries.

`apt` pulls in the libraries it needs. The package installs `kikarinhas` and
`kikarinhas-config`, the example `.ini` in `share/doc/kikarinhas/`, the default
avatar pack, the `.desktop` entries and the icon, so both programs show up in
application menus and launchers (the configurator as "Kikarinhas Settings").
If the menu does not pick them up right away, run `update-desktop-database` or
log out and back in. For colour emoji, also install `fonts-noto-color-emoji`.

To install from source instead, see [Building](#building).

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
   copied or changed;
4. the **system-wide** folders, `kikarinhas/avatars` in each data directory
   (`/usr/share`, `/usr/local/share` and the others in `$XDG_DATA_DIRS`),
   one kit per subfolder: where the program installs its default pack, and
   where a `.deb` or any other installer can put new avatar kits for
   everyone on the machine.

Each folder may hold a data folder (`streamavatars_json.txt`), a pack
(`data.txt`, the format of SA's `.zip` files, unzipped) or subfolders with
those. Without Stream Avatars, kikarinhas runs with whatever the other
folders have.

**A small default pack comes with kikarinhas** and is what you get on a fresh
install: five characters (cat, slime, robot, ghost and mushroom) with three
colours each, and hats, glasses and armchairs to wear (`!gear coroa`...),
all drawn for the project (`data/avatars/default`, installed in
`/usr/share/kikarinhas/avatars/default`; `tools/gen_default_avatars.py`
draws them). Avatars of the system-wide folders are there for everyone
(`!avatar cat`, `!avatar random`), but they are not handed out by hash as
somebody's default avatar while there are others, so having your own avatars
or Stream Avatars, or installing a new kit, never changes who looks like
what.

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
| `--chat-window` | opens the classic chat window (see [Chat window](#chat-window)) |
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

With the left button (also `obs` mode only; in `desktop` mode clicks go
through to the desktop), click an avatar to make it jump, or pick it up and
drag it around: it hangs from the pointer, swinging as you carry it, and
flies off at the speed you let go with, bouncing off the walls like with
`!sling`.

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

**First live.** The first time someone is seen, the people file also keeps the
link of the live they came from and, when the platform tells it, its title
(YouTube and Kick; Twitch and Odysee give only the link; a YouTube live
started from a video link has no title). It is never replaced by later lives.
The **First live** column shows it (hover for title and link), and the field
next to the search filters by it, by title or link.

**Removing people.** The list accepts several rows (Ctrl+click, Shift+click,
or **Select all** / Ctrl+A for everyone shown, so the search and the live
filter narrow it first). **Remove selected** (or the Delete key) asks for
confirmation and forgets those people: avatar, points and dates. Whoever
talks again enters as new.

**Backup.** **Export...** saves the whole list, every field of every person,
to a `.tsv` file (the same format as the people file). **Import...** adds the
people of such a file, so a mistaken removal can be undone: you choose between
**Skip**, which keeps whoever is already in the list as they are and adds only
the missing ones, and **Overwrite**, which replaces everything about them with
what the file says. With kikarinhas running, the import goes through it.

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

### Danmaku

Chat messages can fly across the screen from right to left instead of going
in speech bubbles, as on Niconico and Bilibili: only the text, on one line,
with an outline and no name, at a random height. Emotes stay in the text as
pictures; paid messages come in their own colour with the amount first, and
so do new members. Among the free lines (where a message can't catch up
with the one ahead), it prefers those that cross the avatars least, so the
text goes over the empty part of the stage when there is room. Help and
text commands (`!help`, `!discord`...) keep their bubbles, and the avatar
still jumps when its person talks.

```ini
[danmaku]
enabled = yes
# seconds to cross the screen (longer messages go faster)
seconds = 8
font = Sans Bold
size = 24
color = #ffffff
outline = #000000
paid_color = #ffd24a
member_color = #8ef09c
# percent
opacity = 100
avoid_avatars = yes
max_on_screen = 100
```

All of it is also in the **Danmaku** tab of `kikarinhas-config`.

### Chat window

An optional classic chat window: the messages of every live, from every
platform, in one list, newest at the bottom (or the top). It is there to
read the chat and for OBS to capture (the **Kikarinhas — chat** window, with
**Allow transparency**) instead of a browser-source chat. Open it from the
**Chat window** tab of `kikarinhas-config`, with Shift+C on the main window
(OBS mode), with `--chat-window`, over the socket
(`{"type":"chat_window","action":"show"}`; also `hide`, `toggle` and `state`)
or with `open = yes`; the window manager's X closes it and gives
back the memory it used. Messages are kept (`history`) even while it is
closed, so it opens with the chat so far.

The mouse wheel scrolls back; after 30 s without scrolling it goes back to
the newest messages by itself, so the stream doesn't stay stuck on old ones.
A right click opens a menu (in a window of its own, which OBS doesn't
capture): highlight the message on the stage, copy its text, hide it, hide
everything from that person until kikarinhas restarts (bots, spam), go back
to the newest messages, close the window.

```ini
[chat_window]
open = no
size = 400x600
resizable = yes
above = no            # kept over the other windows
style = own           # or bubbles: each message in a bubble like the avatars'
font = Sans
font_size = 14
text_color = #ffffff
name_color = #ffffff
badge_colors = yes    # owner, mods and members in the name tags' colours
outline = #000000
outline_width = 2     # px; 0: none
paid_color = #ffd24a
member_color = #8ef09c
background = transparent   # color (background_color = #rrggbbaa) or image
background_color = #00000080
background_image = ~/pictures/chat.png
background_mode = stretch  # or tile
show_platform = yes   # the platform's icon before the name
show_photo = yes      # the viewer's profile picture, as on the platform
show_time = no
hide_commands = yes
hide_users = Nightbot, StreamElements
fade = 0              # seconds until a message goes; 0: never
newest = bottom       # or top
history = 200
```

#### Highlighted comment

Click a message in the chat window and it shows big on the stage, in a card,
while a spotlight follows its author's avatar (who comes back to the stage if
they had left); click it again, or press Esc in the chat window, to take
it away. In the chat window it only
gets a discreet mark, since that window may be on the stream too. Over the
socket: `{"type":"highlight","id":N}` (the id of a message in the history)
and `{"type":"highlight","clear":true}`.

```ini
[highlight]
card = yes       # no: only the spotlight
x = 50           # centre of the card, % of the window
y = 25
width = 50       # widest it gets, % of the window width
font = Sans
font_size = 18
seconds = 0      # until it goes by itself; 0: until clicked again
spotlight = yes
```

#### Question queue

The chat sends questions with `!pergunta TEXT` (or `!question`, `!pergunte`,
`!duvida`; the command is in the Commands tab like the others), and with
`auto = yes` every message ending in `?` goes in too. The queue is for the
streamer only: it shows in the **Questions** tab of `kikarinhas-config`
(refreshed every couple of seconds), never on the stream. Highlight one
(double click) to show it on the stage like a click in the chat window,
with the spotlight on whoever asked; remove it, or empty the queue. The
chat window's right-click menu can also put any message in the queue.

```ini
[questions]
auto = no   # messages ending in "?" go in too
max = 50    # past that, the oldest go
```

Over the socket: `{"type":"questions"}` lists them, and
`{"type":"question","action":"highlight"|"remove","id":N}` or
`{"type":"question","action":"clear"}` handle them.

#### Polls

Ask the chat something from the **Polls** tab of `kikarinhas-config`: a
question, 2 to 10 options and, if you want, a time limit (otherwise it stays
open until you click **End now**). The chat votes with `!a`, `!b`... or
`!1`, `!2`... (the ones that are not already another command) and with an
option's own command, if you give it one (`!pizza`); the running kikarinhas
refuses to start a poll whose own commands clash with a command or an avatar
name, and the dialog says which. Each person has one vote; voting again
changes it.

On the stage the window's width splits into one part per option, each with
its coloured floor: the avatar of whoever votes walks to their option's part
and wanders only there until the poll ends. On top go the question and a
card per option (what to type, the votes and a bar), which can be turned
off, moved down or have the question on one side. Once it ends the final
result stays a few seconds, the winner marked with ★.

Only one poll is open at a time. Every poll stays in the history
(`~/.local/share/kikarinhas/polls.json`) with its votes in total and per
platform, when it opened and closed and which lives were being read; the tab
lists them, shows the results (partial while open) and can start a new poll
from an old one or delete it.

```ini
[polls]
file = ~/.local/share/kikarinhas/polls.json
panel = yes          # question and cards on the stage; no: only the floor
align = center       # the question: center, left or right
y = 0                # where the cards start, % of the window height
font = Sans
font_size = 16
results_seconds = 15 # the final result on the stage
```

Over the socket: `{"type":"poll","action":"start","question":"...",
"options":[{"text":"Pizza","command":"pizza"},{"text":"Coxinha"}],
"duration":60,"show_counts":true}`, `{"type":"poll","action":"close"}`,
`{"type":"poll","action":"remove","id":N}` and
`{"type":"poll","action":"state"}` (the open one and the history's path).

### Scenery

Pictures kept on the window all the time: a ground or backdrop behind the
avatars, a frame or a logo in front of them. They are your own files (PNG,
JPEG, BMP or GIF; animated ones play; transparency is kept), one
`[scenery.NAME]` section each, or the **Scenery** tab of `kikarinhas-config`:

```ini
[scenery.grass]
file = ~/pictures/grass.png
x = 50          # centre, in % of the window width
y = 94          # centre, in % of the window height
scale = 100     # % of the file's size
layer = behind  # behind (default) or front of the avatars
enabled = yes
```

Up to 16; the later ones are drawn over the earlier ones. Pictures shown
by chat commands and the emote wall go over everything. `reload` picks up
changes.

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
`{"type":"remove_users","users":["youtube:UC...", ...]}` forgets those
people (`removed` in the reply says how many were there), and
`{"type":"import_users","path":"/path/backup.tsv","overwrite":false}` adds
the people of an exported file (`added`, `replaced` and `skipped` in the
reply).

In `message`: `user_id` is required; `platform` (default `bridge`), `name`,
`photo` (profile picture, `https` URL, for the chat window), `text`, `kind`
(`text`, `paid` or `member`), `amount` and `badges` (`owner`,
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
| `!question TEXT` | `!pergunta`, `!pergunte`, `!duvida` | puts a question in the queue the streamer goes through (see [Question queue](#question-queue)) | 30 s |
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

On Wayland, Kikarinhas runs through XWayland (it is an X11 program) and OBS
captures it the same way; there is no native Wayland mode.

## Planned

- An optional classic chat window, to read the chat and to capture in OBS
  instead of a browser-source chat, with a highlighted comment on the stage.
- Polls that gather the voters' avatars into groups, one per answer.

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
