# 07 — Testando e distribuindo

## Testar sem live

Aponte o Kikarinhas para a pasta do kit com `--sa-dir`. Use um `.ini` e um
`users.tsv` descartáveis para não mexer nas suas configurações e nas
escolhas dos espectadores:

```sh
: > /tmp/users.tsv                 # vazio: nada é importado do SA
echo '[main]' > /tmp/teste.ini

build/kikarinhas --sa-dir meu-kit --list       # nome, tamanho, ppu, nº de animações
build/kikarinhas --sa-dir meu-kit --check      # confere imagens e JSON
build/kikarinhas -c /tmp/teste.ini --users /tmp/users.tsv \
    --sa-dir meu-kit -a bloco --scale 4        # mostra o avatar na janela
build/kikarinhas -c /tmp/teste.ini --users /tmp/users.tsv \
    --sa-dir meu-kit --demo-chat -v            # chat de mentira
```

Para experimentar comandos de verdade, mande mensagens pelo socket:

```sh
SOCK=$XDG_RUNTIME_DIR/kikarinhas.sock
echo '{"type":"message","platform":"youtube","user_id":"UCx","name":"Ana","text":"!avatar bloco"}' \
  | socat - UNIX-CONNECT:$SOCK
echo '{"type":"message","platform":"youtube","user_id":"UCx","name":"Ana","text":"!gear chapeu"}' \
  | socat - UNIX-CONNECT:$SOCK
```

Cada `user_id` é uma pessoa diferente, e cada uma tem o seu avatar.

## O que o `--check` confere

| Mensagem | Significa |
|---|---|
| `ERRO ... imagem não encontrada` | não existe `avatars/<name>.png` nem `avatars/<chave>.png` |
| `ERRO ... não carregou` | o PNG existe mas está corrompido ou é menor que um quadro |
| `ERRO ... N linhas na imagem, animações usam M` | a imagem tem menos linhas que a última animação do JSON (linha errada ou imagem cortada) |
| `aviso ... colunas, animação mais longa tem N quadros (cortada)` | a imagem é mais estreita que o `frameData` mais comprido |
| `aviso ... sem idle nem walk` | o avatar não tem como ficar em pé |

O `--check` **não** confere gear, pivôs nem paletas; esses você só vê
olhando (abaixo).

## Checklist antes de publicar

- [ ] `--check` sem erros e sem avisos.
- [ ] Cada avatar com `idle` e `walk`; pés na mesma altura em todos os quadros.
- [ ] A linha de cada animação bate com a chave (`sit` = linha 2 mesmo se
      não houver `idle`...).
- [ ] `!avatar`, `!jump`, `!sit` (se tiver), `!dance` e cada `!emote` testados.
- [ ] Cada paleta testada com `!color`, olhando se sobrou pixel sem trocar
      (sinal de cor que não é exatamente a da `mainPalette`).
- [ ] Cada peça testada com `!gear` em **cada pose** (andando, sentado,
      pulando, dançando) e **olhando para a esquerda**.
- [ ] Cadeiras: somem fora do `sit` (pivô `y: -1000`).
- [ ] Nomes curtos, sem espaços e sem repetir entre avatares, peças e
      paletas.
- [ ] `streamavatars_json.txt` do kit com **só** `avatarData` e `gear`
      (nenhum dado seu ou de espectadores, nenhum token).

## Distribuindo

Hoje, a forma que o Kikarinhas entende é a **pasta de dados**:

```
meu-kit/
├── streamavatars_json.txt
├── avatars/
└── gear/
```

Quem receber o kit coloca a pasta onde quiser e usa `--sa-dir`, ou
`sa_dir = ...` no `.ini`. Isso **substitui** a pasta do SA: o Kikarinhas
carrega uma pasta de dados só. Para misturar um kit com os avatares do
SA, junte os arquivos na mesma pasta (some as entradas de `avatarData` e
de `gear` no JSON e copie as imagens). Atenção para não ter chave
repetida.

Para o **Stream Avatars**, pacotes `.zip` (com `data.txt`, ver
[01](01-estrutura.md)) são a forma de distribuição dele (oficina do Steam
etc.). O jeito de importar está na documentação do próprio SA; se você
quiser testar o mesmo kit nos dois programas, deixe o JSON completo
(com os campos de interação de [06](06-animacoes-e-interacoes.md)) e confira
a importação do SA.

> O suporte do Kikarinhas a abrir `.zip` do SA diretamente está na lista
> de pendências (fase 7 do `PLANO.md`).

## Direitos

Desenho e licença são seus: deixe claro no kit quem fez a arte e em que
termos pode ser usada em live. Não redistribua avatares de outras pessoas
(nem os do SA) sem permissão.

## Problemas comuns

| Sintoma | Causa provável |
|---|---|
| Avatar fica "sem imagem" | nome do PNG não bate com `name` nem com a chave |
| A animação mostra quadros trocados | linha errada na imagem, ou `frameData` com mais/menos itens que quadros desenhados |
| Avatar flutua ou afunda ao andar | pés em alturas diferentes nos quadros de idle/walk |
| `!dance` não toca a dança escolhida | falta `customName: "dance"` (ou o nome escrito diferente) |
| `!sit` não faz nada | linha `sit` vazia ou sem `frameData` |
| Paleta não troca algumas partes | cor do desenho não é exatamente igual à da `mainPalette` (antialiasing, arredondamento) |
| Paleta não troca nada | `swappablePalettes` com número de cores diferente, ou `mainPalette` ausente |
| Chapéu "voa" em certos quadros | pivô não definido naquele quadro (conta como 0,0 = pé do avatar) |
| Chapéu fica no lado errado ao virar | falta/sobra de `flipsWithAvatarX` |
| Cadeira aparece sempre | falta `y: -1000` nos quadros de pé |
| Peça não aparece | conjunto fora de `CanUseGear`, nome da pasta diferente da chave do conjunto, ou imagem não encontrada |
