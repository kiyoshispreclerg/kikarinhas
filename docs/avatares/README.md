# Criando avatares para o Kikarinhas

O Kikarinhas lê os avatares no **mesmo formato do Stream Avatars (SA)**: uma
imagem PNG por avatar (a "spritesheet") mais uma descrição em JSON. Estes
documentos explicam esse formato do ponto de vista de quem quer **desenhar e
montar** avatares, peças (gear) e paletas. Servem para o Kikarinhas e,
como o formato é o mesmo, também para quem usa o SA.

> **Sobre a origem do formato.** Ele não tem documentação oficial publicada.
> Tudo aqui foi **observado** nos dados de uma instalação do SA e conferido
> no código do Kikarinhas (`src/sa.c`, `src/sprite.c`, `src/avatar.c`), e
> foi testado em 194 avatares instalados. Campos que o SA grava mas o
> Kikarinhas ignora aparecem marcados como *ignorado*; podem ter efeito
> no SA.

## Por onde começar

| Quero... | Leia |
|---|---|
| Ver o conjunto de pastas e arquivos | [01 — Estrutura de pastas](01-estrutura.md) |
| Desenhar a spritesheet do avatar | [02 — Spritesheet](02-spritesheet.md) |
| Escrever o JSON do avatar (animações, tamanho) | [03 — Descrição do avatar](03-avatar-json.md) |
| Criar chapéus, armas, cadeiras e outros acessórios | [04 — Gear](04-gear.md) |
| Criar variações de cor | [05 — Paletas](05-paletas.md) |
| Fazer dança, abraço, ataque e outras animações | [06 — Animações e interações](06-animacoes-e-interacoes.md) |
| Conferir se deu certo e empacotar | [07 — Testando e distribuindo](07-testando-e-distribuindo.md) |
| Copiar um exemplo mínimo que funciona | [Exemplo completo](exemplo-minimo.md) |

## Resumo em um minuto

1. Cada avatar é **uma imagem PNG em grade**: cada **linha** é uma animação
   (idle, walk, sit, stand, jump, depois as customizadas) e cada **coluna** é
   um quadro dela.
2. O avatar **olha sempre para a direita**; o programa espelha quando ele anda
   para a esquerda.
3. O JSON diz o tamanho de cada quadro, quantos quadros cada animação tem,
   a velocidade, a escala e quais conjuntos de acessórios ele pode usar.
4. Acessórios (gear) são PNGs em `gear/<conjunto>/<peça>.png`, presos ao
   avatar por **pivôs** (um deslocamento por quadro de animação).
5. Paletas são trocas de cor **exatas**: o JSON lista as cores originais
   do desenho e, para cada paleta, as cores que as substituem.
6. `kikarinhas --check` confere se imagens e JSON batem.

## Convenções destes documentos

- Coordenadas de pivô são em **pixels do avatar**, com **y para cima**.
- "Quadro" é uma célula da grade; "linha" e "coluna" começam em 0.
- Nomes de arquivos e chaves não diferenciam maiúsculas de minúsculas no
  Kikarinhas (`ABRA.png` serve para a chave `abra`), mas use sempre o mesmo
  uso de caixa para o kit funcionar também no SA em Windows/Linux.
