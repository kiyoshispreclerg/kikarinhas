# Exemplo completo: o kit "Bloco"

A pasta [`kit-exemplo/`](kit-exemplo/) é um kit pequeno e **completo**, que
passa no `--check`. Serve de modelo para copiar e alterar.

```
kit-exemplo/
├── streamavatars_json.txt
├── avatars/bloco.png        64×128: 4 colunas × 8 linhas de 16×16
└── gear/
    ├── hats/chapeu.png      chapéu amarelo 16×8
    └── chairs/banco.png     banco cinza 14×6, desenhado atrás do avatar
```

Rodar:

```sh
build/kikarinhas --sa-dir docs/avatares/kit-exemplo --check
build/kikarinhas --sa-dir docs/avatares/kit-exemplo -a bloco --scale 6
```

E no chat (ou pelo socket): `!avatar bloco`, `!color azul`, `!color verde`,
`!gear chapeu`, `!gear banco` + `!sit`, `!dance`, `!emote attack`.

## A spritesheet

| Linha | Animação | Quadros | Notas |
|---|---|---|---|
| 0 | `idle` | 2 | respira (altura 10 ↔ 9) |
| 1 | `walk` | 4 | quica 1 px |
| 2 | `sit` | 2 | mais baixo; **fica no último quadro** |
| 3 | `stand` | 1 | |
| 4 | `jump` | 1 | corpo 2 px acima |
| 5 | `custom1` = `dance` | 4 | 3 repetições |
| 6 | `custom2` = `attack` | 3 | avança |
| 7 | `custom3` = `hurt` | 2 | recua |

Cores usadas (e que estão na `mainPalette`): vermelho `#E03D23`
(corpo), cinza-escuro `#1E1E28` (contorno) e branco (olhos). Nada de
antialiasing: é por isso que `!color azul` troca o corpo inteiro.

## O JSON, comentado

```jsonc
{
  "avatarData": {
    "bloco": {                                  // chave = identificador
      "name": "Bloco",                          // nome de exibição, e nome do PNG
      "width": 16, "height": 16,                // um quadro
      "pixelsPerUnit": 1.0,                     // sem redução
      "bilinearFilter": false,                  // pixel art
      "moveSpeed": 1.0,
      "CanUseGear": ["hats", "chairs"],         // conjuntos que pode vestir

      "mainPalette": { "colors": [ /* vermelho, contorno, branco */ ] },
      "swappablePalettes": {
        "azul":  { "colors": [ /* azul,  contorno, branco */ ] },
        "verde": { "colors": [ /* verde, contorno, branco */ ] }
      },

      "animationData": {
        "idle": {                               // linha 0
          "framesPerSecond": 3,
          "frameData": [                        // 2 itens = 2 quadros
            { "gearPivot": {
                "hats":   { "x": 0, "y": 10 },  // topo da cabeça neste quadro
                "chairs": { "y": -1000 }        // cadeira escondida
              },
              "uniquePivot": {} },
            { "gearPivot": { "hats": { "x": 0, "y": 9 },
                             "chairs": { "y": -1000 } },
              "uniquePivot": {} }
          ]
        },
        "sit": {                                // linha 2
          "framesPerSecond": 4,
          "frameData": [
            { "gearPivot": { "hats":   { "x": 0, "y": 8 },
                             "chairs": { "x": 0, "y": 0 } },   // cadeira aparece
              "uniquePivot": {} },
            { "gearPivot": { "hats":   { "x": 0, "y": 7 },
                             "chairs": { "x": 0, "y": 0 } },
              "uniquePivot": {} }
          ]
        },
        "custom1": {                            // linha 5
          "customName": "dance",
          "animationLoops": true, "loopCount": 3,
          "framesPerSecond": 8,
          "frameData": [ /* 4 quadros */ ]
        }
        // walk, stand, jump, custom2 (attack), custom3 (hurt)...
      }
    }
  },

  "gear": {
    "hats": {
      "globalZIndex": 3,                        // na frente do corpo
      "gearPiece": {
        "chapeu": { "pieceName": "chapeu", "PPU": 1.0,
                    "width": 16, "height": 8, "flipsWithAvatarX": true }
      }
    },
    "chairs": {
      "globalZIndex": -1,                       // atrás do corpo
      "gearPiece": {
        "banco": { "pieceName": "banco", "PPU": 1.0,
                   "width": 14, "height": 6, "flipsWithAvatarX": false }
      }
    }
  }
}
```

O arquivo real, em [`kit-exemplo/streamavatars_json.txt`](kit-exemplo/streamavatars_json.txt),
tem todas as animações e todos os pivôs escritos por extenso.

## Como o chapéu fica no lugar

O pivô `hats.y` de cada quadro é **a altura do corpo naquele quadro mais o
quanto ele está levantado** (pulo, quique). O chapéu é desenhado com a base
da imagem nessa altura, acima dos pés. Assim ele acompanha cada pose
sem ajuste manual.

## Regerando o kit

As imagens e o JSON são gerados por um script, o que também mostra uma
boa forma de montar kits maiores (calcular os pivôs a partir dos mesmos números
que posicionam o corpo, em vez de digitar à mão):

```sh
python3 tools/gen_kit_exemplo.py     # precisa do Pillow
```
