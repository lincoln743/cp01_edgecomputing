/* ===========================================================================
 *  FrioLog Solucoes em Logistica - No de Borda Inteligente (camara-piloto)
 *  Checkpoint 1 - Edge Computing - FIAP - Prof. Flavio M. Azevedo
 *
 *  O no percebe o ambiente com 2 sensores, decide LOCALMENTE (sem rede) e
 *  age sobre a camara com 3 atuadores.
 *
 *  SENSORES
 *    S1 - DHT22 (digital, protocolo 1-wire proprietario) -> temperatura e umidade
 *    S2 - LDR / fotorresistor (analogico, ADC 12 bits)   -> luminosidade interna,
 *         usada como deteccao de PORTA ABERTA (camara fechada = escura).
 *         Atencao: no modulo do Wokwi a leitura CAI quando a luz aumenta.
 *
 *  ATUADORES
 *    A1 - Modulo rele  -> liga/desliga o compressor (refrigeracao)
 *    A2 - Buzzer       -> alarme sonoro local
 *    A3 - LED RGB      -> sinalizacao visual do estado (semaforo operacional)
 *
 *  IMPORTANTE: nao ha Wi-Fi, HTTP, MQTT ou qualquer biblioteca de rede neste
 *  firmware. Toda a decisao acontece dentro do ESP32.
 * ========================================================================= */

#include <Arduino.h>
#include <DHT.h>

/* --------------------------------------------------------------------------
 * 1. MAPEAMENTO DE HARDWARE
 * ------------------------------------------------------------------------ */
#define PIN_DHT     15          // DHT22  - dado (digital)
#define PIN_LDR     34          // LDR    - AO  (ADC1_CH6, pino somente entrada)
#define PIN_RELE    26          // Rele   - IN
#define PIN_BUZZER  25          // Buzzer - terminal positivo
#define PIN_LED_R   27          // LED RGB - vermelho
#define PIN_LED_G   14          // LED RGB - verde
#define PIN_LED_B   13          // LED RGB - azul

#define DHTTYPE     DHT22
DHT dht(PIN_DHT, DHTTYPE);

// Modulo rele deste protótipo: IN em nivel alto energiza a bobina.
const uint8_t RELE_LIGADO    = HIGH;
const uint8_t RELE_DESLIGADO = LOW;

/* --------------------------------------------------------------------------
 * 2. LIMIARES DE DECISAO (faixa de conservacao: 2 C a 8 C)
 *    Todos os limiares de liga/desliga tem HISTERESE para evitar que o no
 *    fique oscilando entre dois estados na fronteira da decisao.
 * ------------------------------------------------------------------------ */
const float TEMP_CRITICA        = 8.0f;   // C  - acima disso a carga esta em risco
const float TEMP_SAI_CRITICA    = 7.0f;   // C  - histerese de 1.0 C para sair do critico
const float TEMP_LIGA_COMP      = 6.0f;   // C  - liga o compressor
const float TEMP_DESLIGA_COMP   = 4.0f;   // C  - desliga o compressor (histerese 2.0 C)
const float UMID_ALTA           = 75.0f;  // %  - risco de condensacao / mofo
const float UMID_SAI_ALTA       = 70.0f;  // %  - histerese de 5 %
const float TEMP_RISCO_COMBINADO = 6.0f;  // C  - usado nas regras combinadas

/* Sensor 2 (LDR): no modulo do Wokwi a tensao em AO CAI quando a luz aumenta.
   Logo:  camara fechada (escuro) -> leitura ALTA  (~3400 a 4000 contagens)
          porta aberta  (claro)   -> leitura BAIXA (~700 a 1300 contagens)      */
const int   LDR_PORTA_ABERTA_MAX  = 1500; // ADC <= 1500 (~acima de 200 lux) => claro
const int   LDR_PORTA_FECHADA_MIN = 2500; // ADC >= 2500 (~abaixo de 50 lux) => escuro

const uint32_t INTERVALO_AMOSTRA_MS = 2000;  // DHT22 aceita no maximo 1 leitura / 2 s
const uint32_t TEMPO_PORTA_MS       = 3000;  // porta precisa ficar aberta 3 s p/ alarmar
const uint32_t TEMPO_MIN_ESTADO_MS  = 4000;  // permanencia minima (anti-flapping)

/* --------------------------------------------------------------------------
 * 3. ESTADOS OPERACIONAIS
 *    A ORDEM DO ENUM E A ORDEM DE PRIORIDADE: o menor valor vence quando mais
 *    de uma condicao for verdadeira ao mesmo tempo.
 * ------------------------------------------------------------------------ */
enum EstadoNo : uint8_t {
  ST_FALHA_SENSOR = 0,   // prioridade 1 - sem leitura confiavel (fail-safe)
  ST_CRITICO      = 1,   // prioridade 2 - carga em risco
  ST_PORTA_ABERTA = 2,   // prioridade 3 - perda de frio pela porta
  ST_RESFRIANDO   = 3,   // prioridade 4 - puxando a temperatura para baixo
  ST_NORMAL       = 4    // prioridade 5 - tudo dentro da faixa
};

const char* nomeEstado(EstadoNo e) {
  switch (e) {
    case ST_FALHA_SENSOR: return "FALHA_SENSOR";
    case ST_CRITICO:      return "CRITICO";
    case ST_PORTA_ABERTA: return "PORTA_ABERTA";
    case ST_RESFRIANDO:   return "RESFRIANDO";
    default:              return "NORMAL";
  }
}

/* --------------------------------------------------------------------------
 * 4. VARIAVEIS DE ESTADO DO NO
 * ------------------------------------------------------------------------ */
EstadoNo estadoAtual    = ST_NORMAL;   // <- variavel que representa o estado do sistema
EstadoNo estadoAnterior = ST_NORMAL;

float    temperatura = 0.0f;   // C   (sensor 1 - DHT22)
float    umidade     = 0.0f;   // %   (sensor 1 - DHT22)
int      luzBruta    = 0;      // 0..4095 (sensor 2 - LDR)
bool     leituraValida = false;

bool     portaAberta   = false;  // saida do sensor 2 apos histerese
bool     compressorOn  = false;  // memoria da histerese do compressor
bool     tempCritica   = false;  // memoria da histerese do critico
bool     umidCritica   = false;  // memoria da histerese da umidade

uint32_t tPortaClara      = 0;   // instante em que a luz passou do limiar
uint32_t tUltimaAmostra   = 0;
uint32_t tEntradaEstado   = 0;   // instante da ultima mudanca de estado
uint32_t tPadraoAlarme    = 0;   // temporizador do padrao do buzzer / LED
bool     pulsoAlarmeOn    = false;
uint32_t contadorCiclos   = 0;

/* --------------------------------------------------------------------------
 * 5. LEITURA DOS SENSORES
 * ------------------------------------------------------------------------ */
void lerSensores() {
  // --- Sensor 1: DHT22 (DIGITAL) - a biblioteca decodifica o protocolo de bits
  float t = dht.readTemperature();   // C
  float h = dht.readHumidity();      // %

  leituraValida = !isnan(t) && !isnan(h);
  if (leituraValida) {
    temperatura = t;
    umidade     = h;
  }

  // --- Sensor 2: LDR (ANALOGICO) - conversor A/D de 12 bits do ESP32
  luzBruta = analogRead(PIN_LDR);    // 0..4095

  // Histerese + tempo minimo: a leitura precisa cruzar limiares diferentes
  // (1500 / 2500) para a porta mudar de estado, e a luz precisa se manter.
  if (!portaAberta && luzBruta <= LDR_PORTA_ABERTA_MAX) {
    if (tPortaClara == 0) tPortaClara = millis();
    // so considera porta aberta apos TEMPO_PORTA_MS de luz sustentada, assim
    // um clarao rapido (alguem passando com lanterna) nao gera alarme falso
    if (millis() - tPortaClara >= TEMPO_PORTA_MS) portaAberta = true;
  } else if (portaAberta && luzBruta >= LDR_PORTA_FECHADA_MIN) {
    portaAberta = false;
    tPortaClara = 0;
  } else if (!portaAberta && luzBruta > LDR_PORTA_ABERTA_MAX) {
    tPortaClara = 0;   // escureceu antes de completar o tempo: reinicia a contagem
  }
}

/* --------------------------------------------------------------------------
 * 6. DECISAO LOCAL  (roda 100% dentro do ESP32, sem rede)
 *
 *    Regras, na ordem de prioridade:
 *      P1 FALHA_SENSOR : leitura do DHT22 invalida (NaN)
 *      P2 CRITICO      : temp >= 8.0 C
 *                        OU (temp >= 6.0 C E umidade >= 75 %)      <- 1 sensor
 *                        OU (temp >= 6.0 C E porta aberta)   <- REGRA COMBINADA
 *                                                               (DHT22 + LDR)
 *      P3 PORTA_ABERTA : porta aberta ha mais de 3 s, sem risco termico
 *      P4 RESFRIANDO   : temp >= 6.0 C (desliga so em 4.0 C - histerese)
 *      P5 NORMAL       : demais casos
 * ------------------------------------------------------------------------ */
EstadoNo decidirEstado() {

  /* P1 - sem leitura confiavel nao ha decisao confiavel: modo seguro. */
  if (!leituraValida) {
    return ST_FALHA_SENSOR;
  }

  /* Histerese das condicoes criticas (evita bater/soltar no limiar). */
  if (!tempCritica && temperatura >= TEMP_CRITICA)     tempCritica = true;
  if ( tempCritica && temperatura <  TEMP_SAI_CRITICA) tempCritica = false;

  if (!umidCritica && umidade >= UMID_ALTA)            umidCritica = true;
  if ( umidCritica && umidade <  UMID_SAI_ALTA)        umidCritica = false;

  /* Regra que combina os DOIS sensores: calor + porta aberta = perda rapida
     de frio, entao o no ja trata como critico antes de chegar aos 8 C. */
  bool regraCombinada = (temperatura >= TEMP_RISCO_COMBINADO) && portaAberta;

  /* Regra que combina as duas grandezas do DHT22: calor + umidade alta =
     risco de condensacao sobre a carga. */
  bool regraCondensacao = (temperatura >= TEMP_RISCO_COMBINADO) && umidCritica;

  /* P2 */
  if (tempCritica || regraCombinada || regraCondensacao) {
    return ST_CRITICO;
  }

  /* P3 */
  if (portaAberta) {
    return ST_PORTA_ABERTA;
  }

  /* P4 - histerese do compressor: liga em 6 C, so desliga em 4 C. */
  if (!compressorOn && temperatura >= TEMP_LIGA_COMP)     compressorOn = true;
  if ( compressorOn && temperatura <= TEMP_DESLIGA_COMP)  compressorOn = false;

  if (compressorOn) {
    return ST_RESFRIANDO;
  }

  /* P5 */
  return ST_NORMAL;
}

/* --------------------------------------------------------------------------
 * 7. ACIONAMENTO DOS ATUADORES CONFORME O ESTADO DECIDIDO
 * ------------------------------------------------------------------------ */
void corLed(bool r, bool g, bool b) {
  digitalWrite(PIN_LED_R, r);
  digitalWrite(PIN_LED_G, g);
  digitalWrite(PIN_LED_B, b);
}

void aplicarAtuadores(EstadoNo e) {
  // --- Padrao de pulso nao bloqueante usado pelos estados de alarme ---
  uint32_t periodo = 0;
  switch (e) {
    case ST_CRITICO:      periodo = 250;  break;  // bipe rapido
    case ST_PORTA_ABERTA: periodo = 700;  break;  // bipe lento
    case ST_FALHA_SENSOR: periodo = 500;  break;  // bipe medio
    default:              periodo = 0;    break;
  }
  if (periodo == 0) {
    pulsoAlarmeOn = false;
  } else if (millis() - tPadraoAlarme >= periodo) {
    tPadraoAlarme = millis();
    pulsoAlarmeOn = !pulsoAlarmeOn;
  }

  switch (e) {
    case ST_FALHA_SENSOR:
      // Fail-safe: sem leitura confiavel o compressor nao e comandado.
      digitalWrite(PIN_RELE, RELE_DESLIGADO);
      corLed(pulsoAlarmeOn, 0, pulsoAlarmeOn);              // magenta piscando
      if (pulsoAlarmeOn) tone(PIN_BUZZER, 1000); else noTone(PIN_BUZZER);
      break;

    case ST_CRITICO:
      digitalWrite(PIN_RELE, RELE_LIGADO);                  // refrigeracao maxima
      corLed(pulsoAlarmeOn, 0, 0);                          // vermelho piscando
      if (pulsoAlarmeOn) tone(PIN_BUZZER, 2000); else noTone(PIN_BUZZER);
      break;

    case ST_PORTA_ABERTA:
      // Decisao de eficiencia: com a porta aberta nao adianta gastar compressor.
      digitalWrite(PIN_RELE, RELE_DESLIGADO);
      corLed(1, 1, 0);                                      // amarelo fixo
      if (pulsoAlarmeOn) tone(PIN_BUZZER, 1500); else noTone(PIN_BUZZER);
      break;

    case ST_RESFRIANDO:
      digitalWrite(PIN_RELE, RELE_LIGADO);                  // compressor ligado
      corLed(0, 0, 1);                                      // azul fixo
      noTone(PIN_BUZZER);
      break;

    case ST_NORMAL:
    default:
      digitalWrite(PIN_RELE, RELE_DESLIGADO);
      corLed(0, 1, 0);                                      // verde fixo
      noTone(PIN_BUZZER);
      break;
  }
}

/* --------------------------------------------------------------------------
 * 8. REGISTRO NO SERIAL MONITOR
 * ------------------------------------------------------------------------ */
void logLeitura(EstadoNo e) {
  char linha[160];
  snprintf(linha, sizeof(linha),
           "[%6lus] T=%5.1f C | UR=%5.1f %% | LDR=%4d (%s) | ESTADO=%-12s | "
           "RELE=%s BUZZER=%s",
           (unsigned long)(millis() / 1000),
           temperatura, umidade, luzBruta,
           portaAberta ? "PORTA ABERTA " : "porta fechada",
           nomeEstado(e),
           (e == ST_CRITICO || e == ST_RESFRIANDO) ? "ON " : "OFF",
           (e == ST_CRITICO || e == ST_PORTA_ABERTA || e == ST_FALHA_SENSOR) ? "ON" : "OFF");
  Serial.println(linha);
}

void logMudancaEstado(EstadoNo de, EstadoNo para, const char* motivo) {
  Serial.println(F("--------------------------------------------------------------"));
  Serial.printf(">>> MUDANCA DE ESTADO: %s -> %s\n", nomeEstado(de), nomeEstado(para));
  Serial.printf("    Motivo .....: %s\n", motivo);
  Serial.printf("    Leituras ...: T=%.1f C | UR=%.1f %% | LDR=%d\n",
                temperatura, umidade, luzBruta);
  Serial.printf("    Decisao ....: tomada localmente no ESP32 (sem rede)\n");
  Serial.println(F("--------------------------------------------------------------"));
}

/* Explica em texto qual regra levou ao estado - ajuda na validacao da Etapa 4. */
const char* motivoDoEstado(EstadoNo e) {
  switch (e) {
    case ST_FALHA_SENSOR: return "leitura do DHT22 invalida (NaN) - modo seguro";
    case ST_CRITICO:
      if (tempCritica)                                   return "temperatura >= 8.0 C";
      if (portaAberta)                                   return "REGRA COMBINADA: temp >= 6.0 C E porta aberta (LDR)";
      return "regra combinada: temp >= 6.0 C E umidade >= 75 %";
    case ST_PORTA_ABERTA: return "LDR <= 1500 (muita luz) por mais de 3 s = porta aberta";
    case ST_RESFRIANDO:   return "temperatura >= 6.0 C (compressor ligado ate 4.0 C)";
    default:              return "todas as grandezas dentro da faixa de conservacao";
  }
}

/* --------------------------------------------------------------------------
 * 9. SETUP
 * ------------------------------------------------------------------------ */
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_RELE,   OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_R,  OUTPUT);
  pinMode(PIN_LED_G,  OUTPUT);
  pinMode(PIN_LED_B,  OUTPUT);
  pinMode(PIN_LDR,    INPUT);

  digitalWrite(PIN_RELE, RELE_DESLIGADO);
  corLed(0, 0, 0);
  noTone(PIN_BUZZER);

  analogReadResolution(12);           // ADC 0..4095
  analogSetPinAttenuation(PIN_LDR, ADC_11db);   // faixa util ate ~3.3 V

  dht.begin();

  Serial.println();
  Serial.println(F("=============================================================="));
  Serial.println(F(" FrioLog - No de Borda Inteligente | Camara-piloto 01"));
  Serial.println(F(" Checkpoint 1 - Edge Computing - FIAP"));
  Serial.println(F("=============================================================="));
  Serial.println(F(" Sensores : DHT22 (T/UR, digital) + LDR (luz, analogico)"));
  Serial.println(F(" Atuadores: rele (compressor) + buzzer (alarme) + LED RGB"));
  Serial.println(F(" Faixa alvo: 2.0 C a 8.0 C | UR < 75 %"));
  Serial.println(F(" Prioridade: FALHA > CRITICO > PORTA_ABERTA > RESFRIANDO > NORMAL"));
  Serial.println(F(" Operacao 100% local: nenhuma conexao de rede e utilizada."));
  Serial.println(F("=============================================================="));

  tEntradaEstado = millis();
}

/* --------------------------------------------------------------------------
 * 10. LOOP - ciclo perceber -> decidir -> agir
 * ------------------------------------------------------------------------ */
void loop() {

  // (1) PERCEBER + (2) DECIDIR: a cada 2 s (limite do DHT22)
  if (millis() - tUltimaAmostra >= INTERVALO_AMOSTRA_MS) {
    tUltimaAmostra = millis();
    contadorCiclos++;

    lerSensores();
    EstadoNo novoEstado = decidirEstado();

    /* Amortecimento anti-flapping: uma vez decidido, o estado permanece por um
       tempo minimo. Excecao: FALHA_SENSOR e CRITICO entram na hora, porque sao
       condicoes de seguranca e nao podem esperar. */
    bool prioridadeAlta = (novoEstado == ST_CRITICO || novoEstado == ST_FALHA_SENSOR);
    bool podeTrocar     = prioridadeAlta ||
                          (millis() - tEntradaEstado >= TEMPO_MIN_ESTADO_MS);

    if (novoEstado != estadoAtual && podeTrocar) {
      estadoAnterior = estadoAtual;
      estadoAtual    = novoEstado;
      tEntradaEstado = millis();
      logMudancaEstado(estadoAnterior, estadoAtual, motivoDoEstado(estadoAtual));
    }

    logLeitura(estadoAtual);
  }

  // (3) AGIR: atuadores atualizados a cada volta do loop, para que os padroes
  //     de bipe e de pisca fiquem suaves sem usar delay().
  aplicarAtuadores(estadoAtual);
}
