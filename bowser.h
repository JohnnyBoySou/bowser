#ifndef BOWSER_H
#define BOWSER_H

typedef struct {
    const char *url;
    const char *title;
    const char *user_agent;
    const char *script;      /* JS injetado em document-start, ou NULL */
    const char *profile;     /* nome do perfil persistente */
    const char *screenshot;  /* caminho do PNG a salvar apos carregar */
    const char *eval_js;     /* JS avaliado apos carregar; resultado no stdout */
    const char *wait_sel;    /* espera este seletor CSS aparecer */
    int wait_ms;             /* espera fixa apos o load, em ms */
    int timeout_ms;          /* limite total da tarefa, em ms */
    int full_page;           /* 1 = captura o documento inteiro */
    int hidden;              /* 1 = renderiza offscreen, sem abrir janela */
    int stay;                /* 1 = mantem a janela aberta apos a tarefa */
    int floating;            /* 1 = pede ao compositor uma janela flutuante */
    int fixed_viewport;      /* 1 = viewport de tamanho fixo, centralizado */
    int width;
    int height;
    int fullscreen;
    int chrome;              /* 1 = mostra barra de navegacao */
    int private_mode;        /* 1 = sessao efemera, nada em disco */
    int devtools;            /* 1 = abre o inspetor ao iniciar */
    int insecure;            /* 1 = ignora erros de TLS */
    int console;             /* 1 = console.log da pagina vai para o stdout */
    double zoom;
} BowserOpts;

int bowser_run(BowserOpts *opts);

#endif
