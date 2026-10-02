# 05 — Paletas (variações de cor)

Uma paleta troca as cores do avatar por outras, sem precisar de outra
imagem. O espectador usa `!color NOME` (ou só `!NOME`); `!color random`
sorteia uma; `!color none` volta às cores originais.

## Como funciona

O JSON lista:

- `mainPalette.colors`: as cores **originais** que existem no desenho,
  numa ordem qualquer (mas fixa);
- `swappablePalettes.<nome>.colors`: para cada variação, as cores
  **novas, na mesma ordem**. A cor `i` da variação substitui a cor `i`
  da principal.

```json
"mainPalette": {
  "colors": [
    { "r": 1.0, "g": 0.0, "b": 0.0, "a": 1.0 },
    { "r": 0.0, "g": 0.0, "b": 0.0, "a": 1.0 }
  ]
},
"swappablePalettes": {
  "azul": {
    "colors": [
      { "r": 0.0, "g": 0.0, "b": 1.0, "a": 1.0 },
      { "r": 0.0, "g": 0.0, "b": 0.0, "a": 1.0 }
    ]
  },
  "verde": {
    "colors": [
      { "r": 0.0, "g": 0.6, "b": 0.0, "a": 1.0 },
      { "r": 0.0, "g": 0.0, "b": 0.0, "a": 1.0 }
    ]
  }
}
```

Neste exemplo, todo pixel vermelho puro vira azul em `azul`; o preto fica
preto.

## Regras

- Cada canal é um **decimal de 0 a 1** (`r`, `g`, `b`, `a`); o programa
  multiplica por 255 e arredonda. Para a cor `#E03D23`, use
  `r = 224/255 = 0.8784`, `g = 61/255 = 0.2392`, `b = 35/255 = 0.1373`.
  Escrever com 6 a 9 casas decimais evita erro de arredondamento
  (o SA grava algo como `0.8745098`).
- A troca é por **valor exato do pixel**. Um pixel que não bate com
  nenhuma cor da `mainPalette` **não muda**. Por isso:
  - o desenho **não pode ter antialiasing** nas áreas recoloríveis
    (nem `#E03D23` quase igual a `#E03D24`);
  - desenhe com **poucas cores bem definidas**, e anote os valores
    exatos para a `mainPalette`;
  - a transparência `a` conta: pixels semitransparentes não casam com
    uma cor opaca.
- **No máximo 32 cores** por paleta.
- Todas as paletas devem ter **o mesmo número de cores** da principal.
  Cor que deve ficar igual (contorno preto) é repetida na variação.
- O nome da paleta é a chave em `swappablePalettes` (sem
  diferenciar maiúsculas); use um nome curto, sem espaços.
- A paleta vale para **a spritesheet do avatar**. Peças de gear **não**
  herdam a paleta ainda (`paletteSwap` é ignorado): se a peça precisa
  combinar, entregue-a em cores neutras.

## Fazendo a lista de cores

Um comando para listar as cores distintas (ImageMagick) do PNG:

```sh
convert avatars/bloco.png -format %c -depth 8 histogram:info:- | sort -rn | head -20
```

Cada linha traz a contagem e o valor da cor (`srgba(224,61,35,1)`).
Divida cada canal por 255 para chegar nos decimais do JSON. Se aparecem
**dezenas de cores quase iguais**, o desenho tem antialiasing ou
degradê: reduza as cores antes de montar as paletas.
