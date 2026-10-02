# Kikarinhas

[Português](README.md) · **English**

Chat avatars walking around your livestream: everyone who talks in the chat
gets a little character that strolls, jumps and reacts in a transparent
window that OBS captures. Similar to Stream Avatars and Desktop Ponies, but
native to Linux (X11) and light on resources: C, Cairo and Pango.

> **Phases 4, 5 and 7 (partly).** Reads the public chat of a YouTube or
> Twitch livestream (no login or key needed): each person who talks gets a Stream Avatars
> avatar that jumps and shows the message in a speech bubble. Through the
> chat, everyone picks an avatar, a color and accessories (saved for the next
> stream), makes the avatar dance, sit, hug or attack, and plays sounds from
> the sound board. Chat emoji, YouTube member emotes, Twitch emotes (BTTV,
> FFZ and 7TV included) and YouTube reactions fly across the screen (emote
> wall). Everything is configured in an `.ini` file
> or in `kikarinhas-config`, which also lists the viewers. Other programs
> send messages through a unix socket. See [PLANO.md](PLANO.md) (in Portuguese).

## Building

Dependencies (Debian/Ubuntu):

```sh
sudo apt install build-essential pkg-config libx11-dev libxext-dev \
    libcairo2-dev libpango1.0-dev libcurl4-openssl-dev libasound2-dev \
    libssl-dev
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

The avatars come from your Stream Avatars installation (the `data` folder
inside the Proton prefix). It is looked up in the Steam libraries; if it is
not found, point to it with `--sa-dir`. Nothing there is copied or changed.

```sh
build/kikarinhas -y https://www.youtube.com/watch?v=ID   # a livestream's chat
build/kikarinhas -y @YourChannel        # waits for the channel to go live
build/kikarinhas -t yourchannel         # Twitch chat
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
| `--demo-chat` | fake chat with made-up people |
| `--max N` | chat avatars on screen at once (default 30); whoever has been silent the longest leaves first |
| `--despawn S` | seconds of silence until the avatar leaves (default 300) |
| `-d, --default-avatar NAME` | everyone uses this avatar (default: one random avatar per person, always the same) |
| `-v, --verbose` | prints the messages in the terminal |
| `--users FILE` | where to store each person's choices |
| `--import-sa-users` | imports the Stream Avatars choices again |
| `--sa-dir FOLDER` | Stream Avatars `data` folder |
| `-n, --count N` | random avatars outside the chat (default 6, or 0 with chat or `-a`) |
| `-a, --avatar NAME` | shows this avatar; may be repeated |
| `--scale X` | avatar scale (default 2) |
| `--ground N` | pixels between the ground and the bottom edge (default: room for the name) |
| `--seed N` | random seed, to repeat the same scene |
| `-c, --config FILE` | configuration file (default `~/.config/kikarinhas/kikarinhas.ini`) |
| `--socket PATH\|off` | control socket (default `$XDG_RUNTIME_DIR/kikarinhas.sock`) |
| `--reload` | asks the running kikarinhas to reread its configuration and exits |

Several streams can be read at once, from one platform or both: repeat
`-y`/`-t`, or separate them with commas in the `.ini` (`youtube = @one,
@two`, `twitch = channel1, channel2`). The avatars and emotes of all of
them show up together; the same person in two streams of the same platform
is a single avatar. With `-v`, each message says where it came from
(`[twitch #channel]`, `[youtube @handle]`). On `reload`, only the streams
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
`kikarinhas-config` (GTK2): tabs Window, Avatars, Chat, Commands, Sounds and
Viewers, and the **Save and apply** button. It keeps the file's comments and
only writes what differs from the default.

In the Avatars tab, `show_names`/`name_position` (below or above the avatar)
and `show_bubbles` turn the name tag and the speech bubble on or off.
`name_font`/`name_size` and `bubble_font`/`bubble_size` set the font (Pango
family and style, such as `Sans Bold`) and the size in points of the name and
of the bubble.

To apply changes without closing the program: **Save and apply**,
`kikarinhas --reload` or `kill -HUP`. Commands, default avatar, avatar limit,
despawn time, fps, fake chat, `verbose`, the YouTube and Twitch
targets, name
visibility/position, bubbles and the bubble font change on the spot. Size,
mode, scale, ground, fixed avatars, the name font, the Stream Avatars folder,
the people file and the socket only change on restart, and the log warns
about it. Comments go on their own lines (`#` or `;`), never after a value.

Chat commands are configured in `[command.NAME]` sections:

```ini
# aliases replaces the default list; cooldown is the per-person wait, in
# seconds; role is who may use it: anyone, member, mod or owner.
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
someone's avatar: with kikarinhas running the change takes effect at once;
when it is closed, it is saved for next time.

People who came from Stream Avatars before this version have no name or
dates in the file: with kikarinhas **closed**, run
`kikarinhas --import-sa-users` once. It fills in the name and dates
(`displayName`, `firstTimeSpawned` and `lastTimeUsed` from there) without
changing any choice.

### Emote wall

Chat emoji fly across the screen, over the avatars: common emoji (drawn
from the emoji font), the channel's YouTube member emotes, Twitch emotes and
the channel's BTTV, FFZ and 7TV emotes (downloaded once and kept in
`~/.cache/kikarinhas/emotes`; `third_party_emotes = no` in `[chat]` turns
off the last three) and, if you want, YouTube
reactions (the floating hearts), mixed in with the rest. Three effects:
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
`{"type":"set_avatar","user":"youtube:UC...","avatar":"pikachu"}` changes
someone's avatar (it is what the Viewers tab uses).

In `message`: `user_id` is required; `platform` (default `bridge`), `name`,
`text`, `kind` (`text`, `paid` or `member`), `amount` and `badges` (`owner`,
`mod`, `member`, `verified`, and also `broadcaster`, `moderator` and
`subscriber`) are optional. That way a platform kikarinhas does not know can
become a bridge written in any language.

Common emoji go in the `text` itself. Emotes that are images (Twitch's,
member emotes, 7TV...) go in `emotes`, with the URL of a PNG (`https` only)
and, optionally, where they are in the text (`start` and `len`, in bytes).
Reactions go on their own:

```sh
echo '{"type":"message","platform":"twitch","user_id":"123","text":"hi Kappa","emotes":[{"id":"25","name":"Kappa","url":"https://static-cdn.jtvnw.net/emoticons/v2/25/static/dark/2.0","start":3,"len":5}]}' \
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

- Odysee.
- Stream Avatars .zip packs, emotes as images inside the bubbles, animated
  emotes (GIF).
- Optional HTML layers (WPE WebKit) to replace some obs-browser sources.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE). Includes
[cJSON](vendor/cjson) (MIT), [dr_wav and dr_mp3](vendor/dr_libs) (public
domain or MIT-0) and [stb_vorbis](vendor/stb) (public domain or MIT).

YouTube chat is read from the same endpoints the browser's pop-out chat
window uses; it is not an official API and may change. Twitch chat is read
over IRC, as an anonymous guest.

The avatar artwork belongs to its authors and is not part of Kikarinhas.
