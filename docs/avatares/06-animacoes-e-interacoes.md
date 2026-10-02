# 06 — Animações customizadas e interações

Além de idle/walk/sit/stand/jump, o avatar pode ter até **27 animações
customizadas** (`custom1` a `custom27`, linhas 5 em diante da spritesheet).
Cada uma tem um `customName` que a identifica.

```json
"custom1": {
  "customName": "dance",
  "framesPerSecond": 9,
  "animationLoops": true,
  "loopCount": 3,
  "holdLastFrame": 0.0,
  "frameData": [ {}, {}, {}, {}, {}, {}, {}, {} ]
}
```

- `customName` é comparado **sem diferenciar maiúsculas**.
- Sem `animationLoops` a animação toca **uma vez**; com `true`, `loopCount`
  vezes. Depois fica `holdLastFrame` segundos no último quadro e o avatar
  volta ao normal.
- Uma customizada **sem** `customName` também é válida: ela entra no
  sorteio do `!emote` sem nome.

## Nomes que o Kikarinhas reconhece

| `customName` | Para que serve |
|---|---|
| `dance` | `!dance`. Sem ela, o `!dance` toca uma customizada qualquer |
| `attack` | Quem **ataca** (`!attack @pessoa`) toca esta animação |
| `hurt` | Quem **apanha** toca esta; se não tiver, tenta `death` |
| `death` | Fallback do `hurt` |
| qualquer outro | `!emote NOME` toca exatamente ela (`!emote sleep`) |

Ficam **de fora do sorteio** (`!emote` sem nome, ou `!dance` sem `dance`)
os nomes que contêm `attack`, `hit`, `hurt`, `damage`, `death`, `die` ou
`dead`, para ninguém "morrer" quando só queria dançar.

## Interações entre avatares

### `!hug [@pessoa]`

O avatar **anda até o outro**, para ao lado (do lado de onde veio) e
então os dois **pulam** e aparece um balão com um coração. Não precisa de
nenhuma animação customizada; `jump` basta. Sem `@pessoa`, o alvo é
sorteado.

### `!attack [@pessoa]`

O atacante anda até o alvo, vira para ele e toca `attack`; o alvo vira e toca
`hurt` (ou `death`). Quem não tem a animação **pula** no lugar. Para um
kit divertido de batalha: faça `attack`, `hurt` e `death`.

### Outros comandos

| Comando | Usa |
|---|---|
| `!jump` | linha `jump` (ou `idle`) |
| `!sit` | linha `sit`; precisa existir. `stand` toca ao levantar |
| `!dance` | `dance` |
| `!emote [nome]` | customizada pelo nome, ou sorteada |

Os espectadores só veem o que existe: se o avatar não tem `sit`,
`!sit` simplesmente não faz nada. Por isso **documente no seu kit** quais
animações cada avatar tem.

## Campos do SA que o Kikarinhas não usa

O SA usa `targetsUser`, `targetDistance`, `returnsToIdle` e
`targetInterrupt` para decidir como uma animação se relaciona com outro
avatar (por exemplo, a distância de um ataque). O Kikarinhas ignora esses
campos e decide sozinho: **se quiser que o kit funcione bem nos dois,
preencha-os**:

| Campo | Para animações com alvo (`attack`, `hug`...) | Outras |
|---|---|---|
| `targetsUser` | `true` | omitir |
| `targetDistance` | distância em pixels (o SA grava 16–24) | 16 |
| `returnsToIdle` | `true` | `true` |
| `targetInterrupt` | `true` | `true` |
| `isCustomAnimation` | `true` | — |
| `animationName` | o mesmo da chave (`custom1`) | — |

## Ideias de animações customizadas

`dance`, `wave`, `sleep`, `laugh`, `cry`, `angry`, `eat`, `attack`, `hurt`,
`death`, `cheer`, `clap`. Cada uma é só mais uma linha na imagem com seu
`customName`.
