# Medição dos tempos de resposta

A medição é feita pelo próprio painel web, no cartão **Medição dos tempos de resposta** (botão **Iniciar medição**). São 4 medidas para cada sensor e cada atuador:

| Item | Tempo medido |
|---|---|
| Sensor de CO₂ (MH-Z19B) | leitura no ESP32 (`t_co2`) → chegada da medição ao painel |
| Sensor DHT22 | leitura no ESP32 (`t_dht`) → chegada da medição ao painel |
| Relé / ventoinha | envio do comando pelo painel → `digitalWrite` no ESP32 (`ms` da confirmação) |
| Buzzer | envio do comando → início do som |
| LEDs | envio do comando → LEDs acesos |
| Display OLED | envio do comando → tela redesenhada |

## Alinhamento dos relógios

O ESP32 (`millis()`) e o computador têm relógios diferentes. Antes e depois de cada medida o painel envia 5 mensagens em `/ping`; o ESP32 responde em `/pong` com o seu `millis()` no instante da chegada. O painel fica com a troca de menor tempo de ida e volta (RTT) e considera que esse `millis()` corresponde ao meio do intervalo, como no NTP. Também estima a velocidade relativa dos relógios (a simulação pode rodar abaixo de 100%) com dois alinhamentos separados por 6 s.

Com isso, um instante do ESP32 é convertido para o relógio do painel:

```
t_painel = t_meio_ping + (ms_esp32 - ms_ping) / taxa
```

Erro máximo: metade do RTT do ping usado. O painel também registra o tempo total de ida e volta de cada comando (envio → confirmação recebida).

## Resultado

O botão **Baixar CSV** gera `tempos_de_resposta.csv` (colunas: número, sensor/atuador, tempo de resposta, ida e volta, RTT do ping) e **Baixar gráfico** gera o PNG. Os arquivos da medição usada no artigo ficam em [`../resultados/`](../resultados/).
