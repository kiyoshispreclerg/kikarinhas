# 01 — Estrutura de pastas e arquivos

## A pasta de dados

O Kikarinhas lê **uma pasta de dados** (a `data` do SA). Ele a encontra
sozinho na instalação do SA via Steam/Proton; para usar outra, passe
`--sa-dir PASTA` ou ponha `sa_dir = PASTA` no `.ini`.

```
data/
├── streamavatars_json.txt     ← descrição de tudo (JSON, UTF-8)
├── avatars/
│   ├── bloco.png              ← uma spritesheet por avatar
│   └── Outro Avatar.png
├── gear/
│   ├── hats/                  ← um conjunto de acessórios (set)
│   │   ├── cap.png            ← uma peça
│   │   └── crown.png
│   └── chairs/
│       └── banco.png
├── sounds/                    ← opcional: sons da mesa de som
│   └── buzina.ogg
└── backgrounds/               ← fundos (o Kikarinhas ainda não usa)
```

Só `streamavatars_json.txt` e `avatars/` são necessários para ter avatares.
`gear/` é opcional.

### Arquivo `streamavatars_json.txt`

- JSON em UTF-8, **com ou sem BOM** (o SA grava com BOM; o Kikarinhas aceita
  os dois).
- Chaves da raiz que importam para avatares:

| Chave | Conteúdo |
|---|---|
| `avatarData` | objeto: uma entrada por avatar (ver [03](03-avatar-json.md)) |
| `gear` | objeto: um conjunto por entrada, com suas peças (ver [04](04-gear.md)) |
| `soundData` | sons da mesa de som (só na importação) |
| `userData` | quem usa qual avatar (só na importação; ver abaixo) |

O arquivo de uma instalação do SA tem muitas outras chaves (configurações
de moedas, minigames, comandos...). O Kikarinhas as ignora. **Num kit
que você distribui, só precisa de `avatarData` e `gear`.**

> ⚠️ **Nunca distribua o `streamavatars_json.txt` de uma instalação
> real.** Ele tem `loginDetails` (tokens de login) e `userData` (dados
> de espectadores). Monte o arquivo do kit à mão ou com um script que copie
> só `avatarData` e `gear`.

### Pacotes `.zip` do SA

Os pacotes distribuídos do SA (`premade/`, oficina do Steam) são `.zip`
com o mesmo conteúdo, mas com outros nomes:

```
pacote.zip
├── data.txt                   ← JSON; raiz com "avatar", "gear", "backgrounds"
├── avatars/<nome>.png
├── gear/<conjunto>/<peça>.png
└── backgrounds/*.png
```

A diferença no JSON é só o nome da chave da raiz: **`avatar`** no pacote,
**`avatarData`** no arquivo da instalação. O Kikarinhas entende os dois nomes,
mas **ainda não abre `.zip` sozinho** (está no plano, ver
[07](07-testando-e-distribuindo.md)).

## Como o Kikarinhas acha cada arquivo

- **Imagem do avatar**: procura em `avatars/` um arquivo `NOME.png`, sem
  diferenciar maiúsculas, onde `NOME` é primeiro o campo `"name"` do avatar
  e, se não achar, a **chave** do avatar em `avatarData`. Espaços no nome
  do arquivo são aceitos (`agnes tachyon.png`).
- **Imagem da peça**: `gear/<conjunto>/<chave da peça>.png`, onde o
  conjunto é a chave em `gear` e a peça é a chave em `gearPiece`.
  Também sem diferenciar maiúsculas.
- Imagem faltando: o avatar fica "sem imagem" (aparece em
  `kikarinhas --list` e como erro em `--check`); a peça simplesmente não é
  desenhada.

## Nomes que valem no chat

Os nomes que os espectadores digitam vêm destes campos, sempre sem
diferenciar maiúsculas:

| Comando | Vem de |
|---|---|
| `!avatar NOME` / `!NOME` | chave em `avatarData` ou campo `name` |
| `!gear PEÇA` / `!PEÇA` | chave da peça, entre os conjuntos que o avatar pode usar |
| `!color COR` / `!COR` | chave em `swappablePalettes` |
| `!emote NOME` | `customName` de uma animação customizada |

Use nomes **curtos, sem espaço e únicos** (uma peça com o mesmo nome do
avatar ou de uma cor vira ambiguidade no atalho `!nome`).

## `userData` (opcional, só importação)

Quem tem um SA pode ter as escolhas dos espectadores importadas
na primeira execução. Isso lê só `userData`, e **não faz parte de um kit de
avatares**.
