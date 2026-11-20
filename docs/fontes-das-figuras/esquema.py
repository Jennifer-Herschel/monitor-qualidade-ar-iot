import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Polygon, Circle
fig, ax = plt.subplots(figsize=(15, 9.5), dpi=200); ax.set_xlim(0, 150); ax.set_ylim(0, 95); ax.axis("off"); ax.set_aspect("equal")
K = "#111"; LW = 1.2
def line(pts, c=K, lw=LW): ax.plot([p[0] for p in pts], [p[1] for p in pts], color=c, lw=lw, solid_capstyle="round")
def txt(x, y, s, fs=8.5, ha="center", va="center", w="normal", c=K): ax.text(x, y, s, fontsize=fs, ha=ha, va=va, weight=w, color=c, family="DejaVu Sans")
def flag(x, y, net, side):  # rótulo de rede (net label)
    w = 1.15 * len(net) + 2.2; h = 2.4
    if side == "L":   # bandeira aponta para a direita, fica à esquerda do ponto
        pts = [(x, y), (x-1.2, y+h/2), (x-w, y+h/2), (x-w, y-h/2), (x-1.2, y-h/2)]; tx = x - w/2 - 0.5
    else:
        pts = [(x, y), (x+1.2, y+h/2), (x+w, y+h/2), (x+w, y-h/2), (x+1.2, y-h/2)]; tx = x + w/2 + 0.5
    ax.add_patch(Polygon(pts, closed=True, fc="#eef3f8", ec="#3b5b7d", lw=1)); txt(tx, y, net, 7.5, c="#1d3c5c", w="bold")
def gnd(x, y):
    line([(x, y), (x, y-1.2)])
    for i, wd in enumerate([2.4, 1.6, 0.8]): line([(x-wd/2, y-1.2-i*0.6), (x+wd/2, y-1.2-i*0.6)])
def block(x, y, w, h, title, left=(), right=(), sp=3.0, top_pad=3.0, fs=8.5):
    ax.add_patch(Rectangle((x, y), w, h, fc="white", ec=K, lw=1.4)); txt(x+w/2, y+h+0.6, title, fs, w="bold", va="bottom")
    P = {}
    for i, n in enumerate(left):
        yy = y + h - top_pad - i*sp; line([(x-3, yy), (x, yy)]); txt(x+0.6, yy, n, 7.5, ha="left"); P[n] = (x-3, yy)
    for i, n in enumerate(right):
        yy = y + h - top_pad - i*sp; line([(x+w, yy), (x+w+3, yy)]); txt(x+w-0.6, yy, n, 7.5, ha="right"); P[n] = (x+w+3, yy)
    return P
def resistor_v(x, y1, y2, lab):
    ym = (y1+y2)/2; line([(x, y1), (x, ym+2)]); line([(x, ym-2), (x, y2)])
    zz = [(x, ym+2)] + [(x + (0.8 if i % 2 else -0.8), ym+2-(i+0.5)*4/6) for i in range(6)] + [(x, ym-2)]; line(zz)
    txt(x+1.4, ym, lab, 7.5, ha="left")
def led_v(x, ytop, cor, lab):
    ax.add_patch(Polygon([(x-1.2, ytop), (x+1.2, ytop), (x, ytop-2)], fc=cor, ec=K, lw=1)); line([(x-1.2, ytop-2), (x+1.2, ytop-2)])
    for d in (0, 0.9): ax.annotate("", xy=(x+2.6, ytop-0.4-d), xytext=(x+1.4, ytop+0.6-d), arrowprops=dict(arrowstyle="->", lw=0.8))
    txt(x+3.2, ytop-1.2, lab, 7, ha="left")

# ---------------- ESP32 ----------------
L = ["VIN", "GND", "D13", "D14", "D27", "D26", "D33"]
R = ["3V3", "GND ", "D4", "RX2", "TX2", "D18", "D21", "D22"]
P = block(62, 30, 22, 28, "ESP32 DevKit V1 (ESP32-WROOM-32)\nWi-Fi 2,4 GHz + pilha TCP/IP", L, R, sp=3.2, top_pad=2.6)
netL = {"VIN": "+5V", "GND": "GND", "D13": "LED_VM", "D14": "LED_AM", "D27": "LED_VD", "D26": "RELE_IN", "D33": "DHT_DATA"}
netR = {"3V3": "+3V3", "GND ": "GND", "D4": "BUZZER", "RX2": "CO2_TX", "TX2": "CO2_RX", "D18": "BOTAO", "D21": "SDA", "D22": "SCL"}
for n, net in netL.items(): flag(*P[n], net, "L")
for n, net in netR.items(): flag(*P[n], net, "R")
txt(73, 26.5, "GPIO26 = D26, GPIO16 = RX2, GPIO17 = TX2 etc.", 7, c="#555")

# ---------------- MH-Z19B ----------------
Q = block(118, 64, 18, 15, "MH-Z19B - CO₂ (NDIR)\nUART 9600 bps", ["Vin", "GND", "TX", "RX"], sp=3.2, top_pad=2.6, fs=8)
for n, net in [("Vin", "+5V"), ("GND", "GND"), ("TX", "CO2_TX"), ("RX", "CO2_RX")]: flag(*Q[n], net, "L")
# ---------------- OLED ----------------
Q = block(118, 40, 18, 15, "OLED SSD1306 128x64\nI²C 0x3C", ["VCC", "GND", "SCL", "SDA"], sp=3.2, top_pad=2.6, fs=8)
for n, net in [("VCC", "+3V3"), ("GND", "GND"), ("SCL", "SCL"), ("SDA", "SDA")]: flag(*Q[n], net, "L")
# ---------------- buzzer ----------------
flag(112, 26, "BUZZER", "L"); line([(112, 26), (118, 26)])
ax.add_patch(Rectangle((118, 24.5), 1.6, 3, fc="white", ec=K)); ax.add_patch(Polygon([(119.6, 24.5), (122.5, 22.5), (122.5, 29.5), (119.6, 27.5)], fc="white", ec=K))
txt(124, 26, "Buzzer (GPIO4)", 8, ha="left"); line([(118.8, 24.5), (118.8, 22)]); gnd(118.8, 22)
# ---------------- botão ----------------
flag(112, 12, "BOTAO", "L"); line([(112, 12), (120, 12), (120, 10.5)])
ax.add_patch(Circle((120, 10), 0.5, fc="white", ec=K)); ax.add_patch(Circle((120, 6.5), 0.5, fc="white", ec=K))
line([(121.4, 10.8), (121.4, 5.7)]); line([(121.4, 8.25), (123.2, 8.25)]); line([(123.2, 7.4), (123.2, 9.1)])
line([(120, 6), (120, 4.5)]); gnd(120, 4.5); txt(125, 8.2, "Botão (GPIO18)\npull-up interno", 7.5, ha="left")

# ---------------- DHT22 + pull-up ----------------
Q = block(8, 64, 15, 12, "DHT22\ntemp. e umidade", right=["VCC", "DATA", "GND"], sp=3.2, top_pad=2.8, fs=8)
flag(*Q["VCC"], "+3V3", "R"); flag(*Q["GND"], "GND", "R")
x, y = Q["DATA"]; line([(x, y), (x+14, y)]); ax.add_patch(Circle((x+8, y), 0.35, fc=K)); flag(x+14, y, "DHT_DATA", "R")
resistor_v(x+8, y, y+11, "10 kΩ\npull-up"); flag(x+8, y+11, "+3V3", "R")
# ---------------- LEDs ----------------
for i, (net, cor, lab) in enumerate([("LED_VD", "#33cc33", "verde\n(ar bom)"), ("LED_AM", "#f2d600", "amarelo\n(atenção)"), ("LED_VM", "#e53935", "vermelho\n(saturado)")]):
    x = 8 + i * 13; ytop = 52
    ax.add_patch(Polygon([(x-1.2, ytop+3), (x+1.2, ytop+3), (x+1.2, ytop+0.8), (x, ytop), (x-1.2, ytop+0.8)], fc="#eef3f8", ec="#3b5b7d"))
    txt(x, ytop+4.3, net, 7, c="#1d3c5c", w="bold")
    resistor_v(x, ytop, ytop-7, "220 Ω"); line([(x, ytop-7), (x, ytop-8)]); led_v(x, ytop-8, cor, lab); line([(x, ytop-10), (x, ytop-11.5)]); gnd(x, ytop-11.5)
# ---------------- relé + ventoinha ----------------
Q = block(26, 8, 17, 14, "Módulo relé 1 canal\n(bobina 5 V)", left=["COM", "NO"], right=["VCC", "IN", "GND"], sp=3.2, top_pad=3, fs=8)
for n, net in [("VCC", "+5V"), ("IN", "RELE_IN"), ("GND", "GND")]: flag(*Q[n], net, "R")
flag(*Q["COM"], "+5V", "L")
x, y = Q["NO"]; line([(x, y), (x-8, y), (x-8, y-2)])
ax.add_patch(Circle((x-8, y-4.2), 2.2, fc="white", ec=K, lw=1.2)); txt(x-8, y-4.2, "M", 9, w="bold")
line([(x-8, y-6.4), (x-8, y-7.5)]); gnd(x-8, y-7.5); txt(x-13.5, y-4.2, "Ventoinha\n5 V", 7.5, ha="right")
txt(4, 1.5, "Rótulos iguais indicam o mesmo nó elétrico (ex.: todos os \"GND\" estão ligados entre si; \"+5V\" vem do pino VIN do ESP32).", 7.5, ha="left", c="#444")
plt.savefig("../imagens/figura_esquema.png", bbox_inches="tight", facecolor="white")
