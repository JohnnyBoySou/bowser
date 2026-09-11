# bowser

Mini navegador de linha de comando para Linux. Usa a **webview nativa do
sistema** (WebKitGTK 4.1 + GTK3) em vez de subir um Chromium inteiro: binário
de 1,8 MB, janela na tela em ~100 ms, captura de página em ~1 s.

```console
$ bowser cloudflare.com
```

Foi feito para duas coisas: abrir uma página rápido no terminal, e servir de
olhos para agentes de IA — screenshot, extração de dados e presets de viewport
sem depender de Puppeteer, Playwright ou de um Chrome headless.

```console
$ bowser loja.exemplo.com --device mobile --hidden --screenshot /tmp/m.png
bowser: screenshot salvo em /tmp/m.png (390x844)

$ bowser loja.exemplo.com --hidden --eval 'document.title'
Loja Exemplo — Ofertas do dia
```

---

## Instalação

### Arch Linux (recomendado)

```bash
sudo pacman -S --needed webkit2gtk-4.1 gtk3
git clone https://github.com/JohnnyBoySou/bowser.git
cd bowser
makepkg -si          # usa o PKGBUILD do repositório
```

### Binário do release

O binário é dinâmico: precisa do WebKitGTK instalado, o que em Arch é um
`pacman -S` de duas linhas.

```bash
sudo pacman -S --needed webkit2gtk-4.1 gtk3
curl -L https://github.com/JohnnyBoySou/bowser/releases/latest/download/bowser-x86_64-linux.tar.gz | tar xz
install -Dm755 bowser ~/.local/bin/bowser
```

### Do código

```bash
sudo pacman -S --needed webkit2gtk-4.1 gtk3 go
make            # gera ./bowser
make install    # copia para ~/.local/bin/bowser
```

Em outras distros, instale os equivalentes de `webkit2gtk-4.1` e `gtk3`
(Debian/Ubuntu: `libwebkit2gtk-4.1-dev`, `libgtk-3-dev`; Fedora:
`webkit2gtk4.1-devel`, `gtk3-devel`).

---

## Uso no dia a dia

```bash
bowser cloudflare.com                 # completa o https:// sozinho
bowser localhost:5173 --devtools      # dev local com o inspetor aberto
bowser ./relatorio.html --app         # arquivo local, sem barra de navegação
bowser "clima em salvador"            # texto sem domínio vira busca
```

O argumento aceita, nessa ordem: URL completa, caminho de arquivo existente,
domínio sem esquema (`exemplo.com`, `localhost:5173`) e, se nada disso bater,
um termo de busca.

### Atalhos

| tecla | ação |
| --- | --- |
| `Ctrl+L` | foco na barra de endereço |
| `Ctrl+R` / `F5` | recarrega |
| `Ctrl+Shift+R` | recarrega ignorando o cache |
| `Alt+←` / `Alt+→` | histórico |
| `Ctrl++` / `Ctrl+-` / `Ctrl+0` | zoom |
| `F11` | tela cheia |
| `F12` / `Ctrl+Shift+I` | inspetor |
| `Esc` | cancela o carregamento |
| `Ctrl+W` / `Ctrl+Q` | fecha a janela / sai |

### Opções da janela

| flag | efeito |
| --- | --- |
| `--width N` / `--height N` | tamanho da janela (padrão 1280×820) |
| `--title TEXTO` | título fixo, em vez do título da página |
| `--app` | modo app: sem barra de navegação |
| `--fullscreen` | abre em tela cheia |
| `--zoom N` | zoom inicial |
| `--float` | pede janela flutuante (app_id `bowser-float`) |
| `--devtools` | abre o inspetor ao iniciar |
| `--console` | encaminha `console.log` da página para o stdout |
| `--ua STRING` | user-agent customizado |
| `--script JS\|arquivo.js` | injeta JS em toda página, em `document-start` |
| `--insecure` | ignora erros de certificado TLS |
| `--safe-gpu` | desliga DMA-BUF/compositing (janela preta em alguns drivers) |

---

## Modo agente

Tudo que um agente precisa, sem um Chromium headless no meio.

```bash
# captura em cada classe de aparelho, sem abrir janela
bowser exemplo.com --device mobile  --hidden --screenshot /tmp/m.png
bowser exemplo.com --device tablet  --hidden --screenshot /tmp/t.png
bowser exemplo.com --device desktop --hidden --screenshot /tmp/d.png

# página inteira, esperando o conteúdo real aparecer
bowser exemplo.com --hidden --wait-for "main article" --full-page \
       --screenshot /tmp/pagina.png

# extrair dados: o resultado do JS vai para o stdout
bowser exemplo.com --hidden --eval 'JSON.parse(JSON.stringify(
    [...document.querySelectorAll("h2")].map(e => e.textContent.trim())))'
```

O stdout carrega **somente** o resultado de `--eval`. Mensagens de status e
ruído do WebKit vão para o stderr, então `bowser ... --eval ... 2>/dev/null`
sai limpo para um pipe ou para o `jq`.

Valores que não são string voltam como JSON; strings voltam cruas.

### Viewports

| preset | tamanho | user-agent |
| --- | --- | --- |
| `mobile` | 390×844 | iPhone 15 / Safari iOS |
| `android` | 412×915 | Pixel 8 / Chrome Android |
| `tablet` | 820×1180 | iPad Air / Safari iPadOS |
| `laptop` | 1366×768 | padrão do WebKitGTK |
| `desktop` | 1440×900 | padrão do WebKitGTK |
| `wide` | 1920×1080 | padrão do WebKitGTK |

`--width`, `--height` e `--ua` passados na mão têm prioridade sobre o preset.

Com `--device` e janela visível, a webview recebe o tamanho exato do preset e
fica centralizada na janela — o viewport continua correto mesmo em
compositores que fazem tiling (Hyprland, Sway) e ignoram o tamanho pedido.

### Flags de captura

| flag | efeito |
| --- | --- |
| `--device NOME` | preset de viewport + user-agent |
| `--screenshot ARQ.png` | salva a captura e encerra |
| `--full-page` | captura o documento inteiro, não só o viewport |
| `--eval JS` | avalia JS após o load e imprime o resultado no stdout |
| `--wait-for SELETOR` | espera o seletor CSS aparecer antes de capturar/avaliar |
| `--wait MS` | espera fixa após o load (padrão 300 ms) |
| `--timeout MS` | limite total da tarefa (padrão 20000 ms) |
| `--hidden` | renderiza offscreen, sem abrir janela |
| `--stay` | mantém a janela aberta depois da tarefa |

### Códigos de saída

| código | significado |
| --- | --- |
| `0` | sucesso |
| `1` | erro na captura ou no JS avaliado |
| `2` | uso incorreto (flag ou device inválido) |
| `3` | sem display gráfico disponível |
| `4` | tempo esgotado (`--timeout`) |

### Skill para Claude Code

O repositório traz uma skill pronta em [`SKILL.md`](SKILL.md), que ensina um
agente a usar o bowser para conferir layout responsivo, capturar telas e
extrair dados:

```bash
mkdir -p ~/.claude/skills/bowser
cp SKILL.md ~/.claude/skills/bowser/SKILL.md
```

---

## Perfis e sessão

Sem `--private`, cada perfil guarda cookies, localStorage e logins em
`~/.local/share/bowser/<perfil>` (cache em `~/.cache/bowser/<perfil>`). Logins
sobrevivem entre execuções, o que evita refazer autenticação a cada captura:

```bash
bowser --profile trabalho https://app.exemplo.com
bowser --profile pessoal  https://app.exemplo.com
bowser --private https://exemplo.com     # sessão efêmera, nada em disco
```

Cookies de terceiros são recusados por padrão.

---

## Como funciona

```
main.go      CLI: flags, presets de viewport, normalização do alvo
bowser.c     janela GTK, sinais do WebKit, pipeline de captura/eval
bowser.h     contrato entre os dois
```

O Go cuida da linha de comando e entrega uma struct para o C, que roda o laço
do GTK. Toda a renderização é do WebKitGTK, o mesmo motor do Epiphany e do
GNOME Web — é o WebKit do sistema, atualizado pelo `pacman`, sem Electron nem
Chromium embutido.

Depois do `load-changed → FINISHED`, o pipeline é: espera (`--wait-for` por
polling do seletor, ou `--wait` fixo) → `--eval` → `--screenshot` → sai, salvo
com `--stay`.

## Limitações conhecidas

- `--hidden` desliga o compositing acelerado automaticamente: o backend
  offscreen do GDK não oferece contexto GL. Páginas com WebGL não renderizam
  nesse modo; use janela visível para elas.
- `--eval` não aguarda Promises. Para conteúdo assíncrono, use `--wait-for`.
- Downloads ainda não são tratados.
- O WebKit imprime `Overriding existing handler for signal 10` no stderr por
  causa do runtime do Go. É inofensivo e não polui o stdout.

## Licença

MIT — veja [LICENSE](LICENSE).
