from cozmo_mac import SITES as _S
def cozmo_mac_sites(): return _S
from cozmo_mac import plan

assert plan("/open", {"t": ["vscode"]})[0] == ["open", "-a", "Visual Studio Code"]
assert plan("/open", {"t": ["instagram"]})[0] == ["open", cozmo_mac_sites()["instagram"]]
assert plan("/search", {"site": ["google"], "q": ["bolo de cenoura"]})[0][1].endswith("bolo+de+cenoura")
for path, qs in [("/open", {"t": ["rm -rf /"]}), ("/search", {"site": ["x"], "q": ["a"]}),
                 ("/search", {"site": ["google"], "q": [""]}), ("/shell", {})]:
    try:
        plan(path, qs); raise SystemExit(f"deveria recusar {path} {qs}")
    except ValueError:
        pass
print("testes OK")

import cozmo_mac, tempfile, pathlib
cozmo_mac.PAGE = pathlib.Path(tempfile.mkdtemp()) / "pagina" / "index.html"
assert "<body></body>" in cozmo_mac.page_read()           # sem arquivo: pagina em branco
v = cozmo_mac.version
cozmo_mac.page_write("<h1>oi</h1>")
assert cozmo_mac.page_read() == "<h1>oi</h1>" and cozmo_mac.version == v + 1
for bad in ["", "   ", None, "x" * (cozmo_mac.MAX_HTML + 1)]:
    try:
        cozmo_mac.page_write(bad); raise SystemExit("deveria recusar html invalido")
    except ValueError:
        pass
print("testes da pagina OK")

# ---- WhatsApp: sem tocar no WhatsApp de verdade ----
chamadas, clip = [], {"v": "copiado antes"}
cozmo_mac.osa = lambda script: chamadas.append(script)
cozmo_mac.clip_get = lambda: clip["v"]
cozmo_mac.clip_set = lambda t: clip.__setitem__("v", t)

for bad in ["", "  ", None, "x" * (cozmo_mac.WA_MAX + 1)]:
    try:
        cozmo_mac.wa_draft(bad); raise SystemExit("deveria recusar mensagem invalida")
    except ValueError:
        pass
try:
    cozmo_mac.wa_send("qualquer"); raise SystemExit("enviou sem rascunho")
except ValueError:
    pass
assert chamadas == []                                   # nada chegou no WhatsApp

did = cozmo_mac.wa_draft("Olá! Tudo certo?")
assert chamadas == [cozmo_mac.OSA_PASTE] and clip["v"] == "copiado antes"   # clipboard devolvido
try:
    cozmo_mac.wa_send("id-errado"); raise SystemExit("enviou com id errado")
except ValueError:
    pass
cozmo_mac.wa_send(did)
assert chamadas[-1] == cozmo_mac.OSA_SEND
try:
    cozmo_mac.wa_send(did); raise SystemExit("enviou o mesmo rascunho duas vezes")
except ValueError:
    pass

did = cozmo_mac.wa_draft("velho")
cozmo_mac.draft["at"] -= cozmo_mac.WA_TTL + 1           # simula rascunho de 2 min atras
try:
    cozmo_mac.wa_send(did); raise SystemExit("enviou rascunho expirado")
except ValueError:
    pass
assert chamadas[-1] == cozmo_mac.OSA_PASTE              # expirado nao chegou a enviar
print("testes do WhatsApp OK")
