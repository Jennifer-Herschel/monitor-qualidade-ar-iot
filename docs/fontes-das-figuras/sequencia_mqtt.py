import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch
fig, ax = plt.subplots(figsize=(10, 9.2), dpi=200); ax.set_xlim(0, 100); ax.set_ylim(0, 100); ax.axis("off")
X = {"ESP32": 14, "Broker": 50, "Painel": 86}
for n, x in X.items():
    ax.add_patch(FancyBboxPatch((x-11, 94), 22, 5, boxstyle="round,pad=0.4", fc="#d7e6f5" if n!="Broker" else "#fff1d6", ec="#3b5b7d"))
    ax.text(x, 96.5, {"ESP32":"ESP32 (Wokwi)","Broker":"Broker MQTT","Painel":"Painel web"}[n], ha="center", va="center", fontsize=10.5, weight="bold")
    ax.plot([x, x], [2, 93.5], color="#8a99a8", lw=1, ls="--")
y = [90]
def msg(a, b, txt, cor="#1f4e79", dy=4.6, ls="-"):
    yy = y[0]; xa, xb = X[a], X[b]
    ax.annotate("", xy=(xb, yy), xytext=(xa, yy), arrowprops=dict(arrowstyle="-|>", color=cor, lw=1.3, ls=ls))
    ax.text((xa+xb)/2, yy+0.8, txt, ha="center", va="bottom", fontsize=8.3, color="#222", bbox=dict(fc="white", ec="none", pad=0.6))
    y[0] -= dy
def nota(x, txt):
    yy = y[0]
    ax.add_patch(FancyBboxPatch((x-12.5, yy-1.6), 25, 3.6, boxstyle="round,pad=0.3", fc="#f3f3f3", ec="#999"))
    ax.text(x, yy+0.2, txt, ha="center", va="center", fontsize=8); y[0] -= 5
def secao(txt):
    ax.text(1, y[0]+1, txt, fontsize=8.8, weight="bold", color="#6b4a00"); y[0] -= 3.2
secao("1. Conexão")
msg("ESP32","Broker","CONNECT (keep-alive 15 s, testamento: .../status = \"offline\", retido)")
msg("Broker","ESP32","CONNACK", cor="#666")
msg("ESP32","Broker","PUBLISH .../status = \"online\" (retida)")
msg("ESP32","Broker","SUBSCRIBE .../ventilacao/comando, .../alarme/comando, .../leds/comando, ... (QoS 1)")
msg("Painel","Broker","CONNECT (WebSocket TLS) + SUBSCRIBE mackenzie/jhm10720667/ar/#")
msg("Broker","Painel","mensagens retidas: status, ventilacao/estado, config/atual", cor="#666")
secao("2. Monitoramento (a cada 5 s)")
nota(14, "lê MH-Z19B (UART) e DHT22")
msg("ESP32","Broker","PUBLISH .../medicao {co2, temp, umid, nivel, t_co2, t_dht} (QoS 0)")
msg("Broker","Painel","entrega a medição → cartões, gráfico e log", cor="#666")
secao("3. Comando remoto")
msg("Painel","Broker","PUBLISH .../ventilacao/comando {\"cmd\":\"ON\",\"id\":\"rele-…\"} (QoS 1)", cor="#a5521f")
msg("Broker","ESP32","entrega o comando (QoS 1)", cor="#a5521f")
nota(14, "digitalWrite(GPIO26, HIGH)")
msg("ESP32","Broker","PUBLISH .../ventilacao/estado {rele:ON, id, ms} (retida)")
msg("Broker","Painel","confirmação da ação → tempo de resposta", cor="#666")
secao("4. Queda da conexão")
nota(14, "ESP32 some da rede")
msg("Broker","Painel","Broker publica o testamento: .../status = \"offline\"", cor="#b00020", ls="--")
plt.savefig("../imagens/figura_sequencia_mqtt.png", bbox_inches="tight", facecolor="white")
