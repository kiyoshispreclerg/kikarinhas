# 02 — Spritesheet do avatar

Um avatar é **uma única imagem PNG** dividida numa grade de quadros do
mesmo tamanho.

```
            coluna 0   coluna 1   coluna 2   coluna 3
linha 0  │  idle 1  │  idle 2  │          │          │   idle
linha 1  │  walk 1  │  walk 2  │  walk 3  │  walk 4  │   walk
linha 2  │  sit 1   │  sit 2   │          │          │   sit
linha 3  │  stand 1 │          │          │          │   stand
linha 4  │  jump 1  │          │          │          │   jump
linha 5  │  dance 1 │  dance 2 │  dance 3 │  dance 4 │   custom1
linha 6  │  ...                                       │   custom2
```

## Regras da grade

- **Tamanho do quadro**: `width` × `height` do JSON ([03](03-avatar-json.md)).
  Todos os quadros têm o mesmo tamanho; a imagem tem `colunas × width` por
  `linhas × height` pixels.
- **A linha é a posição fixa da animação**, não a ordem das que existem:

| Linha | Animação |
|---|---|
| 0 | `idle` |
| 1 | `walk` |
| 2 | `sit` |
| 3 | `stand` |
| 4 | `jump` |
| 4 + N | `customN` (`custom1` = linha 5, `custom2` = linha 6...) |

  Se o avatar não tem `sit`, **a linha 2 continua existindo** (pode ficar
  vazia) e `jump` continua na 4. Pular uma linha é a causa mais comum de
  avatar com animação trocada.
- **Cada coluna é um quadro**, da esquerda para a direita. Quantos
  quadros valem é decidido pelo JSON (`frameData` com N itens = N quadros),
  não pela imagem: o que sobrar à direita é ignorado.
- Pixels sobrando à direita ou embaixo (largura não múltipla do quadro) são
  ignorados.
- Máximo de **32 linhas** (idle...jump + 27 customizadas).
- O desenho **olha para a direita**. Quando o avatar anda para a
  esquerda, o programa espelha.
- Fundo **transparente** (PNG com canal alfa).

## Onde ficam os pés

O avatar fica em pé no chão pelo **fundo do quadro**. O Kikarinhas mede
quantas linhas transparentes sobram embaixo nos quadros de `idle` e `walk`
(as poses que ficam no chão) e compensa, então não precisa encostar o pé na
última linha do quadro. Mas:

- mantenha o pé **na mesma altura em todos os quadros de idle e walk**,
  senão o avatar "flutua" ou afunda enquanto anda;
- o ponto de referência dos acessórios é o **centro da base do quadro**
  (ver [04](04-gear.md)): centralize o corpo na horizontal.

## Tamanho e escala

- Pixel art costuma ter quadros pequenos (32×32, 64×64). O campo
  `pixelsPerUnit` define a escala: tamanho na tela =
  `tamanho do quadro / pixelsPerUnit × escala da janela`.
- Com `bilinearFilter` **desligado** (padrão do Kikarinhas e o certo para
  pixel art), a imagem é ampliada com **vizinho mais próximo**: pixels
  nítidos, sem borrar. Com ele ligado, a imagem é suavizada: use para arte
  vetorial/HD.
- Para pixel art use um quadro pequeno e `pixelsPerUnit` = 1; deixe a
  ampliação para a escala da janela (`scale` no `.ini`).
- Arte HD (centenas de pixels): ponha `pixelsPerUnit` maior que 1 para
  reduzir, e `bilinearFilter: true`.
- Memória: o Kikarinhas guarda a folha no tamanho original e amplia ao
  desenhar. Imagens enormes custam RAM mesmo com poucos quadros.

## Dicas de arte

- **Formato**: PNG RGBA de 8 bits. O recolor por paleta só funciona em
  ARGB de 32 bits.
- **Sem antialiasing nas cores que vão ter paleta** (ver [05](05-paletas.md)):
  a troca de cor compara o valor exato do pixel.
- Deixe uma folga transparente ao redor se uma animação balança ou
  gira: nada é desenhado fora do quadro.
- Animações com quadro único (idle de 1 quadro) são válidas.
- Teste com o fundo escuro e claro: bordas semitransparentes aparecem no OBS.
