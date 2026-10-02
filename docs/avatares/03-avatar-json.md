# 03 — Descrição do avatar (JSON)

Cada avatar é uma entrada em `avatarData` (ou `avatar`, nos pacotes `.zip`).
A **chave** da entrada é o identificador do avatar; o programa a usa em
minúsculas.

```json
{
  "avatarData": {
    "bloco": {
      "name": "Bloco",
      "width": 32,
      "height": 32,
      "pixelsPerUnit": 1.0,
      "bilinearFilter": false,
      "moveSpeed": 1.0,
      "CanUseGear": ["hats", "chairs"],
      "mainPalette":       { "colors": [ ... ] },
      "swappablePalettes": { "azul": { "colors": [ ... ] } },
      "animationData": {
        "idle": { ... },
        "walk": { ... }
      }
    }
  }
}
```

## Campos do avatar

| Campo | Tipo | Padrão | Significado |
|---|---|---|---|
| `name` | texto | a chave | Nome de exibição. Também serve para achar `avatars/<name>.png` e no `!avatar` |
| `width`, `height` | inteiro | — (obrigatório) | Tamanho de **um quadro**, em pixels |
| `pixelsPerUnit` | número | 1 | Escala: tamanho na tela = quadro / `pixelsPerUnit` × escala da janela |
| `bilinearFilter` | booleano | `false` | `false` = pixel art (vizinho mais próximo); `true` = suavizado |
| `moveSpeed` | número | 1 | Multiplicador da velocidade de caminhada (30 px/s × escala × `moveSpeed`) |
| `CanUseGear` | lista de textos | nenhum | Nomes dos conjuntos de `gear` que o avatar pode vestir |
| `mainPalette` | objeto | — | Cores originais do desenho ([05](05-paletas.md)) |
| `swappablePalettes` | objeto | — | Variações de cor ([05](05-paletas.md)) |
| `animationData` | objeto | — | Animações e pivôs de acessórios (abaixo) |

Campos que o SA grava e o Kikarinhas **ignora**: `isEnabled`,
`colliderHeight`, `customColliderWidth/Height`, `detectedHeight`,
`detectedFlying`, `accelerationMovement`, `restriction`, `hasUniquePivot`,
`previewBase64`, `thumbnailBase64` e os `*Hash`, `IsActive*`,
`HasLoadedOnce`, `FileDateTracker`. Num kit novo, não precisa deles.

> No SA, o `previewBase64`/`thumbnailBase64` alimentam a lista de
> avatares da interface dele. O Kikarinhas não os usa.

## `animationData`

Um objeto cujas chaves são os **nomes das linhas**:

`idle`, `walk`, `sit`, `stand`, `jump`, `custom1`, `custom2`, ... `custom27`

A chave diz **qual linha da imagem** a animação ocupa ([02](02-spritesheet.md)).
Outras chaves (o SA grava `dance`, `hug`, `attack`, `fart`, `move` vazias
por compatibilidade) são ignoradas.

Cada animação:

```json
"walk": {
  "framesPerSecond": 9,
  "frameData": [
    { "gearPivot": { ... }, "uniquePivot": { ... } },
    { "gearPivot": { ... }, "uniquePivot": { ... } },
    { "gearPivot": { ... }, "uniquePivot": { ... } },
    { "gearPivot": { ... }, "uniquePivot": { ... } }
  ]
}
```

| Campo | Padrão | Significado |
|---|---|---|
| `frameData` | — | **Lista com um item por quadro.** O tamanho da lista é o número de quadros; lista vazia = animação inexistente |
| `framesPerSecond` | 9 | Velocidade (se 0 ou negativo, volta para 9) |
| `customName` | — | Só nas `customN`: o nome pelo qual o chat e o programa chamam a animação ([06](06-animacoes-e-interacoes.md)) |
| `animationLoops` | `false` | Nas `customN`: repetir `loopCount` vezes |
| `loopCount` | 1 | Quantas vezes toca (só vale com `animationLoops: true`) |
| `holdLastFrame` | 0 | Segundos que o último quadro fica parado ao terminar |

Cada item de `frameData` pode ser `{}` se o avatar não usa acessórios; os
pivôs de [04](04-gear.md) vão aqui.

Campos *ignorados* pelo Kikarinhas (o SA usa): `animationName`,
`isCustomAnimation`, `targetsUser`, `targetInterrupt`, `returnsToIdle`,
`targetDistance`, `restriction`, `uniqueData`, `overrideAnimationDelay`.
Eles controlam interações no SA; o Kikarinhas decide isso sozinho
([06](06-animacoes-e-interacoes.md)). Se você faz o kit também para o
SA, preencha-os (veja o exemplo).

## O que cada linha faz

| Linha | Quando toca | Comportamento |
|---|---|---|
| `idle` | parado | repete em laço; se não existir, usa o primeiro quadro de `walk` |
| `walk` | andando | repete em laço; se não existir, usa `idle` |
| `jump` | a cada mensagem do espectador e no `!jump` | toca uma vez durante o pulo (o programa move o avatar com gravidade); se não existir, usa `idle` |
| `sit` | `!sit` ou sorteio | toca uma vez e **fica no último quadro** (sentado) |
| `stand` | ao levantar | toca uma vez e volta ao idle; se não existir, levanta direto |
| `customN` | `!dance`, `!emote`, abraço, ataque... | toca como descrito acima |

Um avatar precisa de pelo menos `idle` **ou** `walk`. Sem nenhum dos dois,
`--check` avisa.

## Conferindo

```sh
build/kikarinhas --list            # lista nome, tamanho, ppu e nº de animações
build/kikarinhas --check           # lê todas as folhas e confere com o JSON
```

`--check` dá **ERRO** se a imagem não existe ou tem menos linhas do que a
última animação usa, e **aviso** se faltam colunas para a animação mais
longa ou se não há idle nem walk.
