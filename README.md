# FrioLog — Nó de Borda Inteligente 🧊

**Checkpoint 1 · Edge Computing · FIAP — Graduação em Ciência da Computação**
Prof. Flavio M. Azevedo

Protótipo (prova de conceito) de um **nó de borda** para a câmara-piloto da
FrioLog Soluções em Logística: o ESP32 percebe o ambiente com 2 sensores,
**decide sozinho, sem internet**, e aciona os atuadores da câmara.

---

## 1. Arquitetura

```
  ┌──────────────┐        ┌───────────────────────────┐        ┌──────────────┐
  │  DHT22       │ GPIO15 │                           │ GPIO26 │  Relé        │
  │  T (°C) / UR │───────▶│                           │───────▶│  compressor  │
  └──────────────┘        │      ESP32 DevKit v1      │        └──────────────┘
                          │                           │ GPIO25 ┌──────────────┐
  ┌──────────────┐ GPIO34 │   decidirEstado()         │───────▶│  Buzzer      │
  │  LDR (luz)   │───────▶│   5 estados + prioridade  │        └──────────────┘
  │  porta aberta│        │   histerese + anti-flap   │ 27/14/13   ┌──────────┐
  └──────────────┘        │                           │──────────▶│ LED RGB  │
                          └───────────────────────────┘           └──────────┘
                              tudo local — sem Wi-Fi
```

## 2. Sensores e atuadores

| # | Componente | Tipo | Pino | Papel na câmara |
|---|------------|------|------|-----------------|
| S1 | DHT22 | **Digital** (1-wire proprietário) | GPIO 15 | Temperatura (°C) e umidade relativa (%) |
| S2 | LDR / fotorresistor | **Analógico** (ADC 12 bits) | GPIO 34 | Luminosidade interna → detecção de **porta aberta** |
| A1 | Módulo relé | Saída digital | GPIO 26 | Liga/desliga o **compressor** |
| A2 | Buzzer | Saída digital (`tone`) | GPIO 25 | **Alarme sonoro local** |
| A3 | LED RGB | 3 saídas digitais | GPIO 27 / 14 / 13 | Semáforo visual de estado |

> ⚠️ No módulo LDR do Wokwi a tensão em `AO` **cai** quando a luz aumenta.
> Portanto: **escuro (porta fechada) ≈ 3400–4000** contagens e
> **claro (porta aberta) ≈ 700–1300** contagens.

## 3. Estados operacionais e prioridade

A ordem do `enum` **é** a ordem de prioridade — vence sempre o menor valor:

| Prio | Estado | Regra de entrada | Relé | Buzzer | LED |
|------|--------|------------------|------|--------|-----|
| 1 | `FALHA_SENSOR` | leitura do DHT22 inválida (`NaN`) | OFF (fail-safe) | 1000 Hz | magenta piscando |
| 2 | `CRITICO` | `T ≥ 8.0 °C` **ou** (`T ≥ 6.0` **e** `UR ≥ 75%`) **ou** (`T ≥ 6.0` **e** porta aberta) | **ON** | 2000 Hz rápido | vermelho piscando |
| 3 | `PORTA_ABERTA` | LDR ≤ 1500 por mais de 3 s | OFF (economia) | 1500 Hz lento | amarelo |
| 4 | `RESFRIANDO` | `T ≥ 6.0 °C` (desliga só em 4.0 °C) | **ON** | — | azul |
| 5 | `NORMAL` | tudo dentro da faixa 2–8 °C, UR < 75% | OFF | — | verde |

**Regra que combina os dois sensores:** `T ≥ 6.0 °C` **E** porta aberta (LDR)
→ `CRITICO`. Com a porta aberta o calor entra rápido, então o nó não espera
chegar aos 8 °C para alarmar.

**Estabilidade:** todos os limiares têm histerese (compressor 6.0 → 4.0 °C;
crítico 8.0 → 7.0 °C; umidade 75 → 70 %; porta 1500 → 2500 contagens), há
tempo mínimo de 3 s de luz para declarar porta aberta e 4 s de permanência
mínima em cada estado — exceto `CRITICO` e `FALHA_SENSOR`, que entram na hora
por serem condições de segurança.

## 4. Como rodar

```bash
# 1. compilar (PlatformIO)
pio run

# 2. simular: abrir diagram.json no VS Code e clicar em "Start Simulation"
#    (extensão Wokwi for VS Code, com licença ativada)

# 3. variar os sensores: clicar no DHT22 (sliders de T e UR) e
#    no LDR (slider de lux) durante a simulação
```

### Harness de validação no PC (opcional)

Roda o **mesmo** `src/main.cpp` no computador com leituras controladas,
reproduzindo os 5 experimentos da Etapa 4 de forma repetível:

```bash
g++ -std=c++17 -I tools/sim tools/sim/sim_main.cpp -o /tmp/no_borda_sim
/tmp/no_borda_sim > docs/serial_experimentos.txt
```

## 5. Estrutura do repositório

```
├── platformio.ini              # placa, framework, libs, monitor_speed
├── diagram.json                # circuito Wokwi (ESP32 + 2 sensores + 3 atuadores)
├── wokwi.toml                  # aponta o simulador para o firmware compilado
├── src/main.cpp                # firmware: percepção → decisão local → ação
├── tools/sim/                  # harness que roda o firmware no PC (validação)
└── docs/
    ├── RESPOSTAS.md            # documento do checkpoint preenchido
    ├── GUIA_DOS_PRINTS.md      # como capturar cada print pedido
    └── serial_experimentos.txt # saída real dos 5 experimentos
```

## 6. Por que isto é Edge Computing

Não há `WiFi.h`, `HTTPClient`, MQTT ou qualquer biblioteca de rede no
firmware. O ciclo **perceber → decidir → agir** fecha inteiramente dentro do
ESP32, em ~2 s, dentro da câmara. Se a internet cair, o nó continua ligando o
compressor e alarmando — que é exatamente o que a FrioLog precisa.
