/* ===========================================================================
 *  Harness de validacao do no de borda - roda o MESMO firmware de src/main.cpp
 *  no PC, injetando leituras de sensores controladas.
 *
 *  Serve para reproduzir os 5 experimentos da Etapa 4 de forma repetivel e
 *  conferir, linha a linha, o que o Serial Monitor do Wokwi deve mostrar.
 *
 *  Compilar e rodar:
 *      g++ -std=c++17 -I tools/sim tools/sim/sim_main.cpp -o /tmp/no_borda_sim
 *      /tmp/no_borda_sim
 * ========================================================================= */
#include <Arduino.h>
#include "../../src/main.cpp"

static const int PASSO_MS = 50;   // granularidade do tempo simulado

static const char* nivelPino(int p) { return sim_pin[p] ? "ON" : "off"; }

/* Avanca o tempo simulado executando o loop() real do firmware. */
static void rodar(uint32_t duracao_ms) {
  uint32_t alvo = sim_millis + duracao_ms;
  while (sim_millis < alvo) {
    loop();
    sim_millis += PASSO_MS;
  }
}

static void cabecalho(const char* titulo) {
  std::printf("\n\n##############################################################\n");
  std::printf("# %s\n", titulo);
  std::printf("##############################################################\n");
}

static void estimulo(const char* descricao, float t, float h, int ldr, bool ok) {
  sim_temp = t; sim_hum = h; sim_ldr = ldr; sim_dht_ok = ok;
  std::printf("\n--- ESTIMULO: %s  (T=%.1f C, UR=%.0f %%, LDR=%d) ---\n",
              descricao, t, h, ldr);
}

static void resumoAtuadores() {
  std::printf("    >> ATUADORES: rele(D26)=%s | buzzer(D25)=%uHz | "
              "LED R=%s G=%s B=%s | estado=%s\n",
              nivelPino(PIN_RELE), sim_tone_hz,
              nivelPino(PIN_LED_R), nivelPino(PIN_LED_G), nivelPino(PIN_LED_B),
              nomeEstado(estadoAtual));
}

int main() {
  sim_temp = 5.0f; sim_hum = 60.0f; sim_ldr = 3800; sim_dht_ok = true;
  setup();

  /* ------------------------------------------------------------------ */
  cabecalho("EXPERIMENTO 1 - CONDICAO DE REFERENCIA (funcionamento esperado)");
  estimulo("camara fechada e na faixa", 5.0f, 60.0f, 3800, true);
  rodar(8000);
  resumoAtuadores();

  /* ------------------------------------------------------------------ */
  cabecalho("EXPERIMENTO 2 - VARIACAO DO SENSOR 1 (DHT22) COM LDR ESTAVEL");
  for (float t = 5.0f; t <= 9.01f; t += 0.5f) {
    char d[64]; std::snprintf(d, sizeof(d), "aquecendo a camara: %.1f C", t);
    estimulo(d, t, 60.0f, 3800, true);
    rodar(5000);
    resumoAtuadores();
  }

  /* ------------------------------------------------------------------ */
  cabecalho("EXPERIMENTO 3 - VARIACAO DO SENSOR 2 (LDR) COM DHT22 ESTAVEL");
  estimulo("volta para a faixa normal", 3.0f, 60.0f, 3800, true);
  rodar(8000);
  const int rampa[] = { 3400, 3000, 2600, 2200, 1800, 1400, 900 };
  for (int v : rampa) {
    char d[64]; std::snprintf(d, sizeof(d), "abrindo a porta: LDR=%d", v);
    estimulo(d, 3.0f, 60.0f, v, true);
    rodar(5000);
    resumoAtuadores();
  }
  estimulo("porta fechada novamente", 3.0f, 60.0f, 3800, true);
  rodar(8000);
  resumoAtuadores();

  /* ------------------------------------------------------------------ */
  cabecalho("EXPERIMENTO 4 - CONDICAO COMBINADA (varias regras verdadeiras)");
  estimulo("T=7.0 C (RESFRIANDO) + UR=80 % (condensacao) + porta aberta",
           7.0f, 80.0f, 900, true);
  rodar(10000);
  resumoAtuadores();

  /* ------------------------------------------------------------------ */
  cabecalho("EXPERIMENTO 5 - REGIAO PROXIMA A DECISAO (estabilidade/histerese)");
  estimulo("reset para condicao normal", 3.0f, 60.0f, 3800, true);
  rodar(8000);
  const float vizinhanca[] = { 5.8f, 5.9f, 6.0f, 5.9f, 6.1f, 5.8f, 5.5f, 4.5f, 4.0f, 3.8f };
  for (float t : vizinhanca) {
    char d[64]; std::snprintf(d, sizeof(d), "oscilando em torno de 6.0 C: %.1f C", t);
    estimulo(d, t, 60.0f, 3800, true);
    rodar(4000);
    resumoAtuadores();
  }

  /* ------------------------------------------------------------------ */
  cabecalho("EXPERIMENTO 6 (extra) - FALHA DO SENSOR (DHT22 desconectado)");
  estimulo("sensor devolvendo NaN", 0.0f, 0.0f, 3800, false);
  rodar(8000);
  resumoAtuadores();
  estimulo("sensor restabelecido", 3.0f, 60.0f, 3800, true);
  rodar(8000);
  resumoAtuadores();

  std::printf("\n== fim dos experimentos ==\n");
  return 0;
}
