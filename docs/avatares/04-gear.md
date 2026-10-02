# 04 — Gear: chapéus, armas, cadeiras e outros acessórios

"Gear" são peças desenhadas **em cima ou atrás do avatar**: chapéus,
cabelos, armas na mão, balões, cadeiras, efeitos. Elas se organizam em
**conjuntos** (sets: `hats`, `chairs`, `mainhand`...). Um avatar veste **uma
peça por conjunto** e só pode usar os conjuntos listados em `CanUseGear`.

## Três lugares onde o gear aparece

1. **Arquivos**: `gear/<conjunto>/<peça>.png`.
2. **Raiz `gear`** do JSON: define cada conjunto e cada peça.
3. **No avatar**: `CanUseGear` (quais conjuntos) e os **pivôs** em cada
   quadro de `animationData` (onde a peça fica).

## Raiz `gear`

```json
"gear": {
  "hats": {
    "globalZIndex": 3,
    "gearPiece": {
      "cap": {
        "pieceName": "cap",
        "PPU": 1.0,
        "width": 32,
        "height": 32,
        "flipsWithAvatarX": true
      },
      "crown": {
        "pieceName": "crown",
        "PPU": 2.0,
        "width": 20,
        "height": 10,
        "zIndex": 7,
        "isUniqueZindex": true
      }
    }
  }
}
```

### Conjunto

| Campo | Padrão | Significado |
|---|---|---|
| `globalZIndex` | 1 | Ordem de desenho de todas as peças do conjunto (ver abaixo) |
| `gearPiece` | — | Objeto: **a chave** é o nome da peça (o mesmo do arquivo e do `!gear`) |

O nome do conjunto (chave de `gear`) é o nome da pasta em `gear/` e o que
vai em `CanUseGear`.

### Peça

| Campo | Padrão | Significado |
|---|---|---|
| `pieceName` | — | Repete o nome. O Kikarinhas usa **a chave**, não este campo |
| `width`, `height` | — | Tamanho de **um quadro** da peça, em pixels |
| `PPU` | 1 | Escala da peça (igual a `pixelsPerUnit` do avatar) |
| `zIndex` + `isUniqueZindex` | — | Ordem própria da peça, só se `isUniqueZindex` for `true` |
| `flipsWithAvatarX` | `false` | Espelha a peça quando o avatar olha para a esquerda |
| `isAnimated` | `false` | A imagem tem vários quadros lado a lado, tocados em `FPS` |
| `isAnimatedAligned` | `false` | A imagem tem a **mesma grade da spritesheet do avatar** e o quadro da peça acompanha o do avatar |
| `FPS` | 9 | Velocidade se `isAnimated` |
| `paletteSwap` | — | *ignorado* pelo Kikarinhas (a peça não herda a paleta do avatar) |
| `restriction`, `previewHash`, `thumbnailHash`... | — | *ignorados* (loja/prévia do SA) |

#### Tipos de imagem da peça

| Tipo | Arquivo | Uso |
|---|---|---|
| Estática | um quadro `width`×`height` | chapéu simples |
| `isAnimated` | quadros lado a lado, cada um `width`×`height` | brilho, chama, balão balançando |
| `isAnimatedAligned` | mesma grade e mesmo tamanho de quadro do avatar | cabeças/trajes inteiros que mudam junto com cada pose (andar, sentar, dançar) |

### Ordem de desenho (z)

- Peça com `isUniqueZindex: true` usa o **seu** `zIndex`; as outras usam o
  `globalZIndex` do conjunto.
- **Valor negativo** = **atrás do corpo** (capas, asas, cadeiras, aura).
  Positivo = na frente. Maior = mais na frente.
- Dentro do mesmo lado (atrás ou na frente), as peças são desenhadas da
  menor para a maior ordem.

## Pivôs: onde a peça fica

O quadro do avatar é o ponto de partida. Cada peça é presa assim:

- **Referência**: o **centro da base** do quadro do avatar (onde ficam os
  pés).
- A peça é desenhada de modo que o **centro da base da própria peça**
  fique nesse ponto, **mais o deslocamento do pivô**.
- O deslocamento é em **pixels do avatar** (não da peça), com **x para a
  direita e y para cima**. Valores negativos descem/vão para a esquerda.
- Se o avatar olha para a esquerda, o **x do pivô sempre espelha** (a peça
  acompanha o lado do corpo); já a **imagem** da peça só é espelhada se ela
  tiver `flipsWithAvatarX: true`. Peças com texto ou assimetria que não
  devem inverter ficam sem essa marca.

Os pivôs ficam **em cada quadro** de `frameData`, porque o chapéu sobe e
desce com o andar:

```json
"frameData": [
  {
    "gearPivot": {
      "hats":   { "x": 1.0, "y": 22.0 },
      "chairs": { "y": -1000.0 }
    },
    "uniquePivot": {
      "hats": { "crown": { "x": -3.0, "y": 24.0 } }
    }
  }
]
```

### `gearPivot` — um por conjunto

`gearPivot.<conjunto> = {x, y}` vale **para todas as peças** do conjunto
naquele quadro. Valores ausentes contam como 0 (`{}` = no ponto de
referência).

### `uniquePivot` — exceção por peça

`uniquePivot.<conjunto>.<peça> = {x, y}` **substitui** o `gearPivot` do
conjunto para aquela peça (por exemplo, uma coroa mais alta que o chapéu
comum).

### Esconder um conjunto em uma pose

Um `y` muito baixo (**`-1000`**) esconde o conjunto naquele quadro. Foi
assim que o SA faz cadeiras aparecerem só quando o avatar está **sentado**:
`"chairs": {"y": -1000}` em idle, walk, jump...; e a posição real nos
quadros de `sit`. O mesmo para efeitos de emote (`emote` conjunto aparece
só em `customN`).

## Passo a passo: um chapéu novo

1. Desenhe `chapeu.png`, só o chapéu, num quadro pequeno (por exemplo
   32×32). O programa prende o **centro da base** da imagem ao pivô, então
   desenhe o chapéu encostado na parte de baixo do quadro e centrado.
2. Salve em `gear/hats/chapeu.png`.
3. Em `gear.hats.gearPiece` crie a chave `chapeu` com `width`, `height` e
   `flipsWithAvatarX: true`.
4. Se o avatar ainda não tem o conjunto, ponha `"hats"` em `CanUseGear`.
5. Em **todos os quadros** de **todas as animações**, dê um
   `gearPivot.hats` com o deslocamento até a cabeça (muda com pose: sentado
   é mais baixo, pulando mais alto).
6. Rode `kikarinhas --demo-chat` e use `!gear chapeu`; ajuste os pivôs olhando.

> Dá trabalho: são N quadros × N conjuntos de pivôs por avatar. Um script
> que gere o JSON é uma boa ideia para kits grandes.

## Quais peças um avatar pode vestir

- `CanUseGear` lista os **conjuntos**; nomes que não existem em `gear` são
  descartados.
- `!gear NOME` procura a peça nos conjuntos permitidos. Vestir uma segunda
  peça do mesmo conjunto **troca** a primeira; `!gear none` tira tudo.
