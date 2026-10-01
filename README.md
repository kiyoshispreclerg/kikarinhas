# Kikarinhas

Avatares do chat andando na sua live: cada pessoa que fala no chat ganha um
bonequinho que passeia, pula e reage numa janela transparente que o OBS
captura. Parecido com o Stream Avatars e o Desktop Ponies, mas nativo para
Linux (X11) e leve: C, Cairo e Pango.

> **Fase 0 (prova de conceito).** Por enquanto a janela mostra só um cartão
> semitransparente quicando, para validar a transparência no OBS. Veja o
> [PLANO.md](PLANO.md).

## Compilar

Dependências (Debian/Ubuntu):

```sh
sudo apt install build-essential pkg-config libx11-dev libxext-dev \
    libcairo2-dev libpango1.0-dev
```

```sh
make            # gera build/kikarinhas
make asan-run   # versão com AddressSanitizer/UBSan
```

## Usar

```sh
build/kikarinhas                    # janela 1280x720 para o OBS
build/kikarinhas -s 1920x1080 -f 60
build/kikarinhas -m desktop         # sobre o desktop, o clique atravessa
```

| Opção | |
|---|---|
| `-m, --mode obs\|desktop` | `obs` (padrão): janela comum para capturar; `desktop`: sobreposição em tela cheia, sem receber cliques |
| `-s, --size LxA` | tamanho da janela no modo `obs` (padrão `1280x720`) |
| `-f, --fps N` | quadros por segundo (padrão 30) |

Para sair: feche a janela ou use Ctrl+C. No modo `desktop`, que não recebe
cliques, use `pkill kikarinhas`.

### No OBS

1. Adicione uma fonte **Captura de janela (Xcomposite)**.
2. Escolha a janela **Kikarinhas** e marque **Permitir transparência**.
3. O fundo deve sumir: o cartão rosa fica semitransparente e a barra
   embaixo vai de invisível a branco.

Precisa de um compositor ativo (o do KWin serve) para a janela parecer
transparente também na tela.

## Previsto

- Chat do YouTube primeiro; Twitch e Odysee depois.
- Leitura dos avatares do Stream Avatars já instalados no seu computador.
- Configurador em GTK2.
- Camadas HTML opcionais (WPE WebKit) para substituir alguns obs-browser.

## Licença

GPL-3.0-or-later. Veja [LICENSE](LICENSE).
