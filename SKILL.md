---
name: bowser
description: Abre páginas web pela webview nativa do sistema (WebKitGTK) a partir da linha de comando — captura screenshots em viewports de celular, tablet e desktop, extrai dados do DOM com JavaScript e confere layout responsivo, sem Chrome headless, Puppeteer ou Playwright. Use quando precisar ver uma página renderizada, validar responsividade, tirar print de um site ou de um servidor de desenvolvimento local, ou ler conteúdo que só existe depois do JavaScript rodar.
---

# bowser

CLI que renderiza páginas no WebKitGTK e devolve screenshot ou dados do DOM.
Uma captura completa leva cerca de 1 segundo.

## Verificar disponibilidade

```bash
command -v bowser || echo "instale: https://github.com/JohnnyBoySou/bowser"
```

Precisa de uma sessão gráfica (`WAYLAND_DISPLAY` ou `DISPLAY`). Sem ela, o
comando sai com código 3.

## Regra principal

Para qualquer uso não interativo, sempre passe `--hidden`. Sem essa flag uma
janela abre na tela do usuário e interrompe o que ele está fazendo.

```bash
bowser <url> --hidden --screenshot /tmp/pagina.png
```

## Receitas

### Ver como a página está renderizando

```bash
bowser exemplo.com --hidden --screenshot /tmp/p.png
```

Depois leia `/tmp/p.png` com a ferramenta de leitura de arquivos — a imagem é
exibida e pode ser analisada.

### Conferir layout responsivo

```bash
for d in mobile tablet desktop; do
  bowser exemplo.com --device $d --hidden --screenshot /tmp/$d.png
done
```

Presets: `mobile` (390×844, UA iPhone), `android` (412×915, UA Pixel),
`tablet` (820×1180, UA iPad), `laptop` (1366×768), `desktop` (1440×900),
`wide` (1920×1080). Os presets de celular e tablet também trocam o user-agent,
então media queries e detecção por UA se comportam como no aparelho real.

### Página inteira, não só a dobra

```bash
bowser exemplo.com --hidden --full-page --screenshot /tmp/completa.png
```

### Conteúdo que só aparece depois do JS

```bash
bowser exemplo.com --hidden --wait-for "[data-loaded]" --screenshot /tmp/p.png
```

`--wait-for` faz polling do seletor CSS até ele existir, respeitando
`--timeout` (padrão 20 s). Use sempre que a página busca dados por rede —
`--eval` não aguarda Promises.

### Extrair dados do DOM

```bash
bowser exemplo.com --hidden --eval 'document.title' 2>/dev/null

bowser exemplo.com --hidden --eval 'JSON.parse(JSON.stringify(
  [...document.querySelectorAll("article h2")].map(e => e.textContent.trim())))' 2>/dev/null
```

O stdout traz só o resultado do JS: strings cruas, qualquer outro valor em
JSON. Todo o resto vai para o stderr, então `2>/dev/null` deixa a saída pronta
para `jq` ou para um pipe.

### Testar o servidor de desenvolvimento local

```bash
bowser localhost:5173 --hidden --wait-for "#root > *" --screenshot /tmp/dev.png
bowser localhost:5173 --hidden --eval 'document.body.innerText.slice(0, 500)' 2>/dev/null
```

### Checar erros de console e requisições quebradas

```bash
bowser localhost:5173 --hidden --console --screenshot /tmp/dev.png 2>&1 >/dev/null | grep -i error
```

### Página autenticada

Perfis guardam cookies e sessão entre execuções:

```bash
bowser https://app.exemplo.com --profile trabalho --hidden --screenshot /tmp/app.png
```

Se a sessão ainda não existe, peça ao usuário que faça o login uma vez em
janela visível — `bowser https://app.exemplo.com --profile trabalho` — e depois
reutilize o mesmo perfil em modo `--hidden`.

## Interpretar a saída

| código | significado | o que fazer |
| --- | --- | --- |
| `0` | sucesso | seguir |
| `1` | falha na captura ou erro no JS | revisar o seletor ou o script |
| `2` | uso incorreto | conferir a flag ou o nome do device |
| `3` | sem display gráfico | não insistir; avisar o usuário |
| `4` | tempo esgotado | aumentar `--timeout` ou revisar `--wait-for` |

## Limites

- Sem WebGL em `--hidden` (o backend offscreen não tem contexto GL). Para
  páginas com canvas 3D, rode em janela visível.
- `--eval` não aguarda Promises; combine com `--wait-for`.
- Não há automação de cliques ou preenchimento de formulário. Dá para
  contornar com `--eval` disparando eventos no DOM, mas para fluxos longos um
  driver de browser completo é mais adequado.
- Downloads não são tratados.

## Flags de referência

```
--device NOME     mobile | android | tablet | laptop | desktop | wide
--screenshot ARQ  salva PNG e encerra
--full-page       documento inteiro em vez do viewport
--eval JS         avalia JS após o load e imprime no stdout
--wait-for SEL    espera o seletor CSS aparecer
--wait MS         espera fixa após o load (padrão 300)
--timeout MS      limite total da tarefa (padrão 20000)
--hidden          renderiza offscreen, sem abrir janela
--stay            mantém a janela aberta após a tarefa
--profile NOME    perfil persistente (cookies, login)
--private         sessão efêmera
--console         console.log da página para o stdout
--insecure        ignora erros de TLS (útil em dev local com cert self-signed)
```
