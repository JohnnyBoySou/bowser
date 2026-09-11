// bowser: um mini navegador de linha de comando que usa a webview nativa do
// sistema (WebKitGTK) em vez de subir um Chromium completo.
//
//	bowser https://cloudflare.com
package main

/*
#cgo pkg-config: gtk+-3.0 webkit2gtk-4.1
#include <stdlib.h>
#include "bowser.h"
*/
import "C"

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"unsafe"
)

const version = "0.2.0"

// Presets de viewport: tamanho logico + user-agent correspondente, para
// conferir como a pagina responde em cada classe de aparelho.
type device struct {
	w, h int
	ua   string
	desc string
}

const (
	uaIPhone  = "Mozilla/5.0 (iPhone; CPU iPhone OS 17_5 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.5 Mobile/15E148 Safari/604.1"
	uaAndroid = "Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Mobile Safari/537.36"
	uaIPad    = "Mozilla/5.0 (iPad; CPU OS 17_5 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.5 Mobile/15E148 Safari/604.1"
)

var devices = map[string]device{
	"mobile":  {390, 844, uaIPhone, "iPhone 15 (390x844)"},
	"android": {412, 915, uaAndroid, "Pixel 8 (412x915)"},
	"tablet":  {820, 1180, uaIPad, "iPad Air retrato (820x1180)"},
	"laptop":  {1366, 768, "", "notebook comum (1366x768)"},
	"desktop": {1440, 900, "", "desktop (1440x900)"},
	"wide":    {1920, 1080, "", "monitor Full HD (1920x1080)"},
}

func deviceNames() string {
	names := make([]string, 0, len(devices))
	for n := range devices {
		names = append(names, n)
	}
	sort.Strings(names)
	return strings.Join(names, ", ")
}

type options struct {
	width      int
	height     int
	title      string
	userAgent  string
	script     string
	profile    string
	device     string
	screenshot string
	eval       string
	waitFor    string
	waitMS     int
	timeoutMS  int
	zoom       float64
	fullPage   bool
	floating   bool
	hidden     bool
	stay       bool
	fullscreen bool
	app        bool
	private    bool
	devtools   bool
	insecure   bool
	console    bool
	safeGPU    bool
}

func main() {
	var o options

	fs := flag.NewFlagSet("bowser", flag.ContinueOnError)
	fs.SetOutput(os.Stdout)
	fs.IntVar(&o.width, "width", 1280, "largura da janela")
	fs.IntVar(&o.height, "height", 820, "altura da janela")
	fs.StringVar(&o.title, "title", "", "título fixo da janela (padrão: título da página)")
	fs.StringVar(&o.userAgent, "ua", "", "user-agent customizado")
	fs.StringVar(&o.script, "script", "", "JS injetado em toda página (código inline ou caminho de arquivo)")
	fs.StringVar(&o.profile, "profile", "default", "perfil persistente (cookies, localStorage, logins)")
	fs.StringVar(&o.device, "device", "", "preset de viewport: "+deviceNames())
	fs.StringVar(&o.screenshot, "screenshot", "", "salva um PNG da página e sai")
	fs.StringVar(&o.eval, "eval", "", "avalia JS após o load e imprime o resultado no stdout")
	fs.StringVar(&o.waitFor, "wait-for", "", "espera um seletor CSS aparecer antes de capturar/avaliar")
	fs.IntVar(&o.waitMS, "wait", 300, "espera fixa em ms após o load, antes de capturar/avaliar")
	fs.IntVar(&o.timeoutMS, "timeout", 20000, "limite total da tarefa em ms")
	fs.BoolVar(&o.fullPage, "full-page", false, "captura a página inteira, não só o viewport")
	fs.BoolVar(&o.floating, "float", false, "pede janela flutuante (útil em compositores tiling)")
	fs.BoolVar(&o.hidden, "hidden", false, "renderiza offscreen, sem abrir janela")
	fs.BoolVar(&o.stay, "stay", false, "mantém a janela aberta após screenshot/eval")
	fs.Float64Var(&o.zoom, "zoom", 1.0, "nível de zoom inicial")
	fs.BoolVar(&o.fullscreen, "fullscreen", false, "abre em tela cheia")
	fs.BoolVar(&o.app, "app", false, "modo app: sem barra de navegação")
	fs.BoolVar(&o.private, "private", false, "sessão efêmera, nada gravado em disco")
	fs.BoolVar(&o.devtools, "devtools", false, "abre o inspetor ao iniciar")
	fs.BoolVar(&o.insecure, "insecure", false, "ignora erros de certificado TLS")
	fs.BoolVar(&o.console, "console", false, "encaminha console.log da página para o stdout")
	fs.BoolVar(&o.safeGPU, "safe-gpu", false, "desativa aceleração/DMA-BUF (use se a janela ficar preta)")
	showVersion := fs.Bool("version", false, "mostra a versão e sai")

	fs.Usage = func() {
		fmt.Fprintf(os.Stdout, `bowser %s — webview nativa do sistema, direto do terminal

Uso:
  bowser [opções] <url|domínio|arquivo|busca>

Exemplos:
  bowser cloudflare.com
  bowser localhost:5173 --devtools
  bowser ./relatorio.html --app --title "Relatório"
  bowser "clima em salvador"

Para agentes:
  bowser exemplo.com --device mobile --screenshot /tmp/m.png --hidden
  bowser exemplo.com --screenshot /tmp/full.png --full-page --wait-for "main"
  bowser exemplo.com --eval "document.title" --hidden

Atalhos:
  Ctrl+L foco na URL   Ctrl+R recarrega   Ctrl+Shift+R ignora cache
  Alt+setas histórico  Ctrl+/- zoom       Ctrl+0 zoom padrão
  F11 tela cheia       F12 inspetor       Ctrl+W fecha   Ctrl+Q sai

Opções:
`, version)
		fs.PrintDefaults()
	}

	flagArgs, positional := splitArgs(fs, os.Args[1:])

	if err := fs.Parse(flagArgs); err != nil {
		if err == flag.ErrHelp {
			return
		}
		os.Exit(2)
	}

	if *showVersion {
		fmt.Println("bowser", version)
		return
	}

	args := append(positional, fs.Args()...)
	if len(args) == 0 {
		fs.Usage()
		os.Exit(2)
	}

	// Varios argumentos soltos viram uma busca unica: bowser clima em salvador
	target := strings.Join(args, " ")

	if err := applyDevice(fs, &o); err != nil {
		fmt.Fprintln(os.Stderr, "bowser:", err)
		os.Exit(2)
	}

	// Em captura/avaliacao a barra de navegacao roubaria altura do viewport.
	if (o.screenshot != "" || o.eval != "") && !isSet(fs, "app") {
		o.app = true
	}

	script, err := loadScript(o.script)
	if err != nil {
		fmt.Fprintln(os.Stderr, "bowser:", err)
		os.Exit(1)
	}

	// O backend offscreen do GDK nao fornece contexto GL: sem desligar o
	// compositing acelerado o WebKit aborta ao renderizar sem janela.
	if o.hidden {
		os.Setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1")
		os.Setenv("WEBKIT_DISABLE_DMABUF_RENDERER", "1")
	}

	if o.safeGPU {
		os.Setenv("WEBKIT_DISABLE_DMABUF_RENDERER", "1")
		os.Setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1")
		os.Setenv("LIBGL_ALWAYS_SOFTWARE", "1")
	}

	os.Exit(run(target, script, &o))
}

func isSet(fs *flag.FlagSet, name string) bool {
	set := false
	fs.Visit(func(f *flag.Flag) {
		if f.Name == name {
			set = true
		}
	})
	return set
}

// applyDevice aplica um preset sem sobrescrever o que o usuario pediu na mao.
func applyDevice(fs *flag.FlagSet, o *options) error {
	if o.device == "" {
		return nil
	}
	d, ok := devices[strings.ToLower(o.device)]
	if !ok {
		return fmt.Errorf("device desconhecido %q; use um de: %s", o.device, deviceNames())
	}
	if !isSet(fs, "width") {
		o.width = d.w
	}
	if !isSet(fs, "height") {
		o.height = d.h
	}
	if !isSet(fs, "ua") && d.ua != "" {
		o.userAgent = d.ua
	}
	return nil
}

// splitArgs separa flags de argumentos posicionais para que a ordem nao
// importe: "bowser url --devtools" funciona igual a "bowser --devtools url".
// O flag padrao do Go para de parsear no primeiro argumento nao-flag.
func splitArgs(fs *flag.FlagSet, argv []string) (flags, positional []string) {
	for i := 0; i < len(argv); i++ {
		a := argv[i]

		if a == "--" {
			positional = append(positional, argv[i+1:]...)
			break
		}
		if len(a) < 2 || a[0] != '-' {
			positional = append(positional, a)
			continue
		}

		flags = append(flags, a)
		name := strings.TrimLeft(a, "-")
		if strings.Contains(name, "=") {
			continue
		}
		// Flags nao-booleanas consomem o proximo argumento como valor.
		f := fs.Lookup(name)
		if f == nil {
			continue
		}
		if bf, ok := f.Value.(interface{ IsBoolFlag() bool }); ok && bf.IsBoolFlag() {
			continue
		}
		if i+1 < len(argv) {
			i++
			flags = append(flags, argv[i])
		}
	}
	return flags, positional
}

// loadScript aceita tanto JS inline quanto um caminho de arquivo.
func loadScript(s string) (string, error) {
	if s == "" {
		return "", nil
	}
	if fi, err := os.Stat(s); err == nil && !fi.IsDir() {
		b, err := os.ReadFile(s)
		if err != nil {
			return "", fmt.Errorf("lendo %s: %w", s, err)
		}
		return string(b), nil
	}
	if strings.HasSuffix(s, ".js") && !strings.Contains(s, "\n") {
		abs, _ := filepath.Abs(s)
		return "", fmt.Errorf("script não encontrado: %s", abs)
	}
	return s, nil
}

func run(target, script string, o *options) int {
	cs := func(s string) *C.char {
		if s == "" {
			return nil
		}
		return C.CString(s)
	}

	opts := C.BowserOpts{
		url:        cs(target),
		screenshot: cs(o.screenshot),
		eval_js:    cs(o.eval),
		wait_sel:   cs(o.waitFor),
		wait_ms:    C.int(o.waitMS),
		timeout_ms: C.int(o.timeoutMS),
		full_page:  cbool(o.fullPage),
		hidden:     cbool(o.hidden),
		stay:       cbool(o.stay),
		floating:   cbool(o.floating),
		// Offscreen a janela ja tem o tamanho exato; na tela, o viewport fixo
		// protege o preset de compositores em tiling.
		fixed_viewport: cbool(o.device != "" && !o.hidden),
		title:          cs(o.title),
		user_agent:     cs(o.userAgent),
		script:         cs(script),
		profile:        cs(o.profile),
		width:          C.int(o.width),
		height:         C.int(o.height),
		fullscreen:     cbool(o.fullscreen),
		chrome:         cbool(!o.app),
		private_mode:   cbool(o.private),
		devtools:       cbool(o.devtools),
		insecure:       cbool(o.insecure),
		console:        cbool(o.console),
		zoom:           C.double(o.zoom),
	}
	defer func() {
		for _, p := range []*C.char{opts.url, opts.title, opts.user_agent, opts.script,
			opts.profile, opts.screenshot, opts.eval_js, opts.wait_sel} {
			if p != nil {
				C.free(unsafe.Pointer(p))
			}
		}
	}()

	return int(C.bowser_run(&opts))
}

func cbool(b bool) C.int {
	if b {
		return 1
	}
	return 0
}
