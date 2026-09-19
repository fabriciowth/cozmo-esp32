#!/usr/bin/env python3
"""Ponte Cozmo -> Mac. So executa acoes de uma lista fixa; nunca roda comando livre.

Uso:  python3 cozmo_mac.py
"""
import json
import secrets
import subprocess
import time
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, quote_plus, urlparse

PORT = 8765
COZMO_ORIGIN = "http://192.168.15.201"   # so a pagina do Cozmo pode pedir acoes

APPS = {
    "whatsapp": "WhatsApp",
    "claude": "Claude",
    "vscode": "Visual Studio Code",
    "arduino": "Arduino IDE",
}
SITES = {
    "agenda": "https://calendar.google.com",
    "gmail": "https://mail.google.com",
    "drive": "https://drive.google.com",
    "instagram": "https://www.instagram.com/mrfab.dev/",
    "youtube": "https://www.youtube.com",
}
# A pagina que o Cozmo escreve: um arquivo so, sempre o mesmo
PAGE = Path(__file__).resolve().parent / "pagina" / "index.html"
PAGE_URL = f"http://127.0.0.1:{PORT}/pagina"
MAX_HTML = 200_000
version = 0          # muda a cada gravacao; o Chrome recarrega quando muda

RELOAD = (b"<script>setInterval(async()=>{try{const v=await(await fetch('/pagina/v')).text();"
          b"if(window._v&&v!==window._v)location.reload();window._v=v}catch(e){}},700)</script>")

EMPTY = "<!doctype html>\n<html lang=\"pt-BR\">\n<head><meta charset=\"utf-8\"><title>Pagina</title></head>\n<body></body>\n</html>\n"


def page_write(html):
    global version
    if not isinstance(html, str) or not html.strip():
        raise ValueError("html vazio")
    if len(html) > MAX_HTML:
        raise ValueError("html grande demais")
    PAGE.parent.mkdir(exist_ok=True)
    PAGE.write_text(html, encoding="utf-8")
    version += 1


def page_read():
    return PAGE.read_text(encoding="utf-8") if PAGE.exists() else EMPTY


# ---- WhatsApp: escreve no chat aberto. Envio so com rascunho valido. ----
WA_MAX = 1000
WA_TTL = 120                      # rascunho vale 2 min
draft = {"id": None, "at": 0.0}

# o texto nunca entra no AppleScript: vai pela area de transferencia (sem injecao, aceita acento)
OSA_PASTE = """
tell application "WhatsApp" to activate
delay 0.7
tell application "System Events" to keystroke "v" using command down
"""
OSA_SEND = """
tell application "WhatsApp" to activate
delay 0.4
tell application "System Events" to key code 36
"""
OSA_CLEAR = """
tell application "WhatsApp" to activate
delay 0.4
tell application "System Events"
  keystroke "a" using command down
  key code 51
end tell
"""


def osa(script):
    r = subprocess.run(["osascript", "-e", script], capture_output=True, text=True, timeout=15)
    if r.returncode:
        err = r.stderr.strip()
        if "1002" in err or "not allowed" in err.lower() or "assistive" in err.lower():
            err = ("o Mac bloqueou: libere o Terminal em Ajustes do Sistema > Privacidade e "
                   "Seguranca > Acessibilidade")
        raise RuntimeError(err or "osascript falhou")


def clip_get():
    return subprocess.run(["pbpaste"], capture_output=True, text=True).stdout


def clip_set(text):
    subprocess.run(["pbcopy"], input=text, text=True, check=True)


def wa_draft(text):
    if not isinstance(text, str) or not text.strip():
        raise ValueError("mensagem vazia")
    if len(text) > WA_MAX:
        raise ValueError("mensagem longa demais")
    old = clip_get()
    clip_set(text)
    try:
        osa(OSA_PASTE)
    finally:
        time.sleep(0.3)
        clip_set(old)             # devolve o que voce tinha copiado
    draft["id"], draft["at"] = secrets.token_hex(4), time.time()
    return draft["id"]


def wa_send(draft_id):
    if not draft["id"] or draft_id != draft["id"]:
        raise ValueError("nao ha rascunho com esse id: escreva o rascunho antes")
    if time.time() - draft["at"] > WA_TTL:
        draft["id"] = None
        raise ValueError("rascunho expirou: escreva de novo")
    draft["id"] = None            # um rascunho envia uma vez so
    osa(OSA_SEND)


def wa_cancel():
    draft["id"] = None
    osa(OSA_CLEAR)


SEARCH = {
    "google": "https://www.google.com/search?q=",
    "youtube": "https://www.youtube.com/results?search_query=",
}


def plan(path, qs):
    """Traduz o pedido num comando `open`. Retorna (argv, descricao) ou levanta ValueError."""
    if path == "/open":
        t = qs.get("t", [""])[0]
        if t in APPS:
            return ["open", "-a", APPS[t]], f"app {APPS[t]}"
        if t in SITES:
            return ["open", SITES[t]], f"site {t}"
        raise ValueError(f"alvo desconhecido: {t}")
    if path == "/page/open":
        where = qs.get("where", [""])[0]
        if not PAGE.exists():
            page_write(EMPTY)
        if where == "vscode":
            return ["open", "-a", "Visual Studio Code", str(PAGE)], "pagina no VS Code"
        if where == "chrome":
            return ["open", "-a", "Google Chrome", PAGE_URL], "pagina no Chrome"
        raise ValueError(f"onde abrir? {where}")
    if path == "/search":
        site, q = qs.get("site", [""])[0], qs.get("q", [""])[0].strip()
        if site not in SEARCH:
            raise ValueError(f"busca desconhecida: {site}")
        if not q or len(q) > 200:
            raise ValueError("busca vazia ou longa demais")
        return ["open", SEARCH[site] + quote_plus(q)], f"busca {site}: {q}"
    raise ValueError("rota desconhecida")


class Handler(BaseHTTPRequestHandler):
    def cors(self):
        self.send_header("Access-Control-Allow-Origin", COZMO_ORIGIN)
        # Chrome pede isso quando um IP da rede chama o localhost
        self.send_header("Access-Control-Allow-Private-Network", "true")
        self.send_header("Access-Control-Allow-Methods", "GET, POST")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")

    def reply(self, code, body):
        data = json.dumps(body, ensure_ascii=False).encode()
        self.send_response(code)
        self.cors()
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_OPTIONS(self):
        self.send_response(204)
        self.cors()
        self.end_headers()

    def do_GET(self):
        u = urlparse(self.path)
        # a propria pagina de preview (so leitura, so neste Mac) nao manda Origin
        if u.path == "/pagina":
            data = page_read().encode()
            data = data.replace(b"</body>", RELOAD + b"</body>") if b"</body>" in data else data + RELOAD
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            return self.wfile.write(data)
        if u.path == "/pagina/v":
            self.send_response(200)
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            return self.wfile.write(str(version).encode())
        if self.headers.get("Origin") != COZMO_ORIGIN:
            return self.reply(403, {"erro": "origem nao autorizada"})
        if u.path == "/page/read":
            return self.reply(200, {"html": page_read()})
        if u.path == "/ping":
            return self.reply(200, {"ok": True})
        try:
            argv, what = plan(u.path, parse_qs(u.query))
        except ValueError as e:
            return self.reply(400, {"erro": str(e)})
        r = subprocess.run(argv, capture_output=True, text=True, timeout=10)
        if r.returncode != 0:
            return self.reply(500, {"erro": r.stderr.strip() or "falhou"})
        print("->", what)
        self.reply(200, {"ok": True, "abriu": what})

    def do_POST(self):
        if self.headers.get("Origin") != COZMO_ORIGIN:
            return self.reply(403, {"erro": "origem nao autorizada"})
        path = urlparse(self.path).path
        n = int(self.headers.get("Content-Length") or 0)
        if n > MAX_HTML * 2:
            return self.reply(413, {"erro": "grande demais"})
        try:
            body = json.loads(self.rfile.read(n) or b"{}")
            if path == "/page/write":
                page_write(body.get("html"))
                print(f"-> pagina gravada ({PAGE.stat().st_size} bytes)")
                return self.reply(200, {"ok": True})
            if path == "/wa/draft":
                did = wa_draft(body.get("text"))
                print("-> WhatsApp: rascunho escrito")
                return self.reply(200, {"ok": True, "draft_id": did})
            if path == "/wa/send":
                wa_send(body.get("draft_id"))
                print("-> WhatsApp: ENVIADO")
                return self.reply(200, {"ok": True, "enviado": True})
            if path == "/wa/cancel":
                wa_cancel()
                print("-> WhatsApp: rascunho apagado")
                return self.reply(200, {"ok": True})
            return self.reply(404, {"erro": "rota desconhecida"})
        except (ValueError, json.JSONDecodeError) as e:
            return self.reply(400, {"erro": str(e)})
        except RuntimeError as e:
            return self.reply(500, {"erro": str(e)})

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    # 127.0.0.1: ninguem da rede acessa, so este Mac
    print(f"Cozmo Mac ouvindo em http://127.0.0.1:{PORT}  (Ctrl+C para parar)")
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
