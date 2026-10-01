# Kikarinhas

Avatares do chat andando na sua live: cada pessoa que fala no chat ganha um
bonequinho que passeia, pula e reage numa janela transparente que o OBS
captura. Parecido com o Stream Avatars e o Desktop Ponies, mas nativo para
Linux (X11) e leve: C, Cairo e Pango.

> **Fase 2.** Lê o chat público de uma live do YouTube (sem login nem chave):
> cada pessoa que fala ganha um avatar do Stream Avatars, que pula e mostra
> a mensagem num balão. Veja o [PLANO.md](PLANO.md).

## Compilar

Dependências (Debian/Ubuntu):

```sh
sudo apt install build-essential pkg-config libx11-dev libxext-dev \
    libcairo2-dev libpango1.0-dev libcurl4-openssl-dev
```

```sh
make            # gera build/kikarinhas
make test       # testes de unidade
make asan       # testes com AddressSanitizer/UBSan
make asan-run   # o programa com AddressSanitizer/UBSan
```

## Usar

Os avatares vêm da sua instalação do Stream Avatars (a pasta `data` dentro do
prefixo do Proton). Ela é procurada nas bibliotecas do Steam; se não for
achada, indique com `--sa-dir`. Nada é copiado nem alterado lá.

```sh
build/kikarinhas -y https://www.youtube.com/watch?v=ID   # chat de uma live
build/kikarinhas -y @SeuCanal           # espera o canal entrar ao vivo
build/kikarinhas --demo-chat            # chat de mentira, para testar
build/kikarinhas                        # 6 avatares sorteados, janela 1280x720
build/kikarinhas -n 20 -s 1920x1080     # 20 avatares em 1080p
build/kikarinhas -a pikachu -a crewmate # avatares escolhidos
build/kikarinhas -m desktop             # sobre o desktop, o clique atravessa
build/kikarinhas --list                 # lista os avatares encontrados
build/kikarinhas --check                # confere todas as spritesheets
```

| Opção | |
|---|---|
| `-m, --mode obs\|desktop` | `obs` (padrão): janela comum para capturar; `desktop`: sobreposição em tela cheia, sem receber cliques |
| `-s, --size LxA` | tamanho da janela no modo `obs` (padrão `1280x720`) |
| `-f, --fps N` | quadros por segundo (padrão 30) |
| `-y, --youtube ALVO` | link da live ou do canal, `@handle` ou id do vídeo |
| `--demo-chat` | chat de mentira com gente inventada |
| `--max N` | avatares do chat ao mesmo tempo (padrão 30); quem está calado há mais tempo sai primeiro |
| `--despawn S` | segundos em silêncio até o avatar sair (padrão 300) |
| `-d, --default-avatar NOME` | todos usam este avatar (padrão: um sorteado por pessoa, sempre o mesmo) |
| `-v, --verbose` | mostra as mensagens no terminal |
| `--sa-dir PASTA` | pasta `data` do Stream Avatars |
| `-n, --count N` | avatares sorteados fora do chat (padrão 6, ou 0 com chat ou `-a`) |
| `-a, --avatar NOME` | mostra este avatar; pode repetir |
| `--scale X` | escala dos avatares (padrão 2) |
| `--ground N` | pixels entre o chão e a borda de baixo (padrão: espaço do nome) |
| `--seed N` | semente do sorteio, para repetir a mesma cena |

Para sair: feche a janela ou use Ctrl+C. No modo `desktop`, que não recebe
cliques, use `pkill kikarinhas`.

### No OBS

1. Adicione uma fonte **Captura de janela (Xcomposite)**.
2. Escolha a janela **Kikarinhas** e marque **Permitir transparência**.
3. O fundo deve sumir, ficando só os avatares e os nomes.

Precisa de um compositor ativo (o do KWin serve) para a janela parecer
transparente também na tela.

## Previsto

- Comandos do chat (`!avatar`, `!dance`...), Twitch e Odysee.
- Acessórios (gear), paletas e pacotes .zip do Stream Avatars.
- Configurador em GTK2.
- Camadas HTML opcionais (WPE WebKit) para substituir alguns obs-browser.

## Licença

GPL-3.0-or-later. Veja [LICENSE](LICENSE). Inclui o
[cJSON](vendor/cjson) (MIT).

O chat do YouTube é lido pelos mesmos endereços que o chat em janela
separada do navegador usa; não é uma API oficial e pode mudar.

As artes dos avatares pertencem aos seus autores e não fazem parte do
Kikarinhas.
