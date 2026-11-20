// Emulação do sensor de CO2 MH-Z19B para o simulador Wokwi.
//
// Responde ao mesmo protocolo serial do sensor real (UART 9600 bps, 8N1,
// quadros de 9 bytes), descrito no manual do fabricante (Winsen, 2019):
//   pedido   FF 01 86 00 00 00 00 00 79
//   resposta FF 86 HH LL TT SS U1 U2 CS   ->  CO2 = HH*256 + LL (ppm)
// CS = 0xFF - (soma dos bytes 1 a 7) + 1.
// A concentração é ajustada no controle deslizante "CO2 no ambiente (ppm)".
//
// Autora: Jennifer Herschel Maia - 2026. Licença MIT.

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  uart_dev_t uart;
  uint32_t   attr_co2;
  uint8_t    rx[9];
  int        rx_len;
  uint8_t    tx[9];
  uint32_t   ruido;
} chip_state_t;

static void on_rx_data(void *user_data, uint8_t byte);
static void on_write_done(void *user_data);

static uint8_t checksum(const uint8_t *q) {
  uint8_t soma = 0;
  for (int i = 1; i < 8; i++) soma += q[i];
  return (uint8_t)(0xFF - soma + 1);
}

void chip_init(void) {
  chip_state_t *chip = malloc(sizeof(chip_state_t));
  chip->attr_co2 = attr_init("co2", 450);
  chip->rx_len = 0;
  chip->ruido = 12345;

  const uart_config_t cfg = {
    .tx = pin_init("TX", INPUT_PULLUP),
    .rx = pin_init("RX", INPUT),
    .baud_rate = 9600,
    .rx_data = on_rx_data,
    .write_done = on_write_done,
    .user_data = chip,
  };
  chip->uart = uart_init(&cfg);
  printf("MH-Z19B (emulado) pronto\n");
}

static void responder_co2(chip_state_t *chip) {
  // pequena variação de +/- 5 ppm, como numa leitura real
  chip->ruido = chip->ruido * 1103515245u + 12345u;
  int variacao = (int)((chip->ruido >> 16) % 11) - 5;
  int ppm = (int)attr_read(chip->attr_co2) + variacao;
  if (ppm < 0) ppm = 0;
  if (ppm > 5000) ppm = 5000;

  chip->tx[0] = 0xFF;
  chip->tx[1] = 0x86;
  chip->tx[2] = (uint8_t)(ppm / 256);
  chip->tx[3] = (uint8_t)(ppm % 256);
  chip->tx[4] = 25 + 40;   // temperatura interna aproximada (+40)
  chip->tx[5] = 0;
  chip->tx[6] = 0;
  chip->tx[7] = 0;
  chip->tx[8] = checksum(chip->tx);
  uart_write(chip->uart, chip->tx, 9);
}

static void on_rx_data(void *user_data, uint8_t byte) {
  chip_state_t *chip = (chip_state_t *)user_data;
  if (chip->rx_len == 0 && byte != 0xFF) return;   // espera o início do quadro
  chip->rx[chip->rx_len++] = byte;
  if (chip->rx_len < 9) return;
  chip->rx_len = 0;
  if (chip->rx[8] != checksum(chip->rx)) return;   // quadro inválido: ignora
  if (chip->rx[2] == 0x86) responder_co2(chip);     // leitura de CO2
  // 0x79 (autocalibração) e 0x87 (calibração de zero) são aceitos sem resposta
}

static void on_write_done(void *user_data) {
  (void)user_data;
}
