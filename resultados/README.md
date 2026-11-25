# Resultados (25/11/2026)

Testes feitos com o ESP32 simulado no Wokwi, conectado ao broker público da HiveMQ, e o painel web no mesmo computador.

| Arquivo | Conteúdo |
|---|---|
| `wokwi_diagrama.png` | Circuito montado no Wokwi |
| `wokwi_funcionando.jpg` | Simulação rodando: Wi-Fi e MQTT conectados, ar bom, LED verde |
| `wokwi_ventilacao.png` | CO₂ em 1.300 ppm: LED vermelho, display "SATURADO" e relé ligando a ventoinha (LED azul) |
| `painel.jpg` | Painel web recebendo as medições |
| `log_mqtt.jpg` | Comandos do painel e confirmações do ESP32 |
| `tempos_de_resposta.csv` | 4 medidas por sensor e por atuador |
| `grafico_tempos.png`, `grafico_medias.png` | Gráficos das medidas |

## Tempos médios

| Sensor/atuador | Média (ms) |
|---|---|
| Sensor de CO₂ (MH-Z19B) | 730,5 |
| Sensor DHT22 | 719,0 |
| Relé / ventoinha | 741,2 |
| Buzzer | 768,7 |
| LEDs | 776,8 |
| Display OLED | 866,5 |

Atuadores: do envio do comando até a confirmação voltar ao painel. Sensores: da leitura no ESP32 até a chegada ao painel. Simulação a cerca de 52% da velocidade real.

## Testes de funcionamento

Todos passaram: conexão e status online; medições a cada 5 s; CO₂ > 1.120 ppm liga o relé após 15 s; CO₂ > 1.520 ppm gera alarme e o botão silencia; CO₂ de volta ao normal desliga o relé; umidade fora de 40 a 60% acende o LED amarelo; comandos de relé, buzzer, LEDs e display confirmados; ao parar a simulação o broker publica `offline` (testamento) em cerca de 90 s.
