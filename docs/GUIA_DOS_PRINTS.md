# Guia dos prints de tela

São **5 prints** obrigatórios. Todos saem da simulação rodando no VS Code.

## Antes de começar

```bash
pio run                     # compila (deve terminar com [SUCCESS])
```
Depois abra `diagram.json` no VS Code e clique em **▶ Start Simulation**
(extensão *Wokwi for VS Code*; é preciso ter a licença gratuita ativada com
`Wokwi: Request a New License`).

Durante a simulação:
- **clique no DHT22** → abre os sliders de **temperatura** e **umidade**;
- **clique no LDR** → abre o slider de **lux** (luminosidade).

Referência de luminosidade (o valor que aparece no Serial é a contagem do ADC):

| Slider do LDR | Leitura esperada | Significado |
|---|---|---|
| ~1 lux (escuro) | ≈ 3900 | porta fechada |
| ~10 lux | ≈ 3400 | porta fechada |
| ~50 lux | ≈ 2500 | limiar de fechamento |
| ~220 lux | ≈ 1500 | **limiar de abertura** |
| ~1000 lux (claro) | ≈ 700 | porta aberta |

---

## Print 1 — Etapa 1: ambiente e montagem (20 pts)
**O que precisa aparecer:** o **circuito completo** no Wokwi (ESP32 + DHT22 + LDR
+ relé + buzzer + LED RGB, com os fios ligados) **e** o arquivo
`platformio.ini` aberto.

> Deixe o VS Code dividido: `platformio.ini` de um lado, simulação do outro, e
> capture a tela inteira.

## Print 2 — Etapa 2: leitura dos sensores (20 pts)
**O que precisa aparecer:** o **Serial Monitor** com várias linhas de leitura
seguidas, mostrando as duas grandezas do DHT22 e o valor do LDR:

```
[     2s] T=  5.0 C | UR= 60.0 % | LDR=3800 (porta fechada) | ESTADO=NORMAL       | RELE=OFF BUZZER=OFF
[     4s] T=  5.0 C | UR= 60.0 % | LDR=3800 (porta fechada) | ESTADO=NORMAL       | RELE=OFF BUZZER=OFF
```

> Deixe rodar uns 10 s para ter uma boa sequência de linhas.

## Print 3 — Etapa 3: mudança de estado por decisão local (20 pts)
**Como provocar:** com a simulação rodando, arraste a temperatura do DHT22 de
**5 °C para 6 °C ou mais**.
**O que precisa aparecer:** o bloco de mudança de estado no Serial Monitor:

```
--------------------------------------------------------------
>>> MUDANCA DE ESTADO: NORMAL -> RESFRIANDO
    Motivo .....: temperatura >= 6.0 C (compressor ligado ate 4.0 C)
    Leituras ...: T=6.0 C | UR=60.0 % | LDR=3800
    Decisao ....: tomada localmente no ESP32 (sem rede)
--------------------------------------------------------------
```

> Se der, capture com o **LED azul aceso** e o relé ligado no circuito, ao lado.

## Print 4 — Etapa 4: um dos experimentos (30 pts)
**Recomendado: o Experimento 4 (condição combinada)** — é o que mais mostra
domínio da lógica de prioridade.
**Como provocar:** ajuste ao mesmo tempo **T = 7 °C**, **UR = 80 %** e o
**LDR em ~1000 lux** (porta aberta). Espere uns 5 s.
**O que precisa aparecer:** o Serial mostrando `ESTADO=CRITICO` **junto com**
`(PORTA ABERTA)` na mesma linha — ou seja, três regras verdadeiras e o nó
escolhendo a mais grave:

```
>>> MUDANCA DE ESTADO: NORMAL -> CRITICO
    Motivo .....: regra combinada: temp >= 6.0 C E umidade >= 75 %
[   108s] T=  7.0 C | UR= 80.0 % | LDR= 900 (PORTA ABERTA ) | ESTADO=CRITICO      | RELE=ON  BUZZER=ON
```

## Print 5 — Etapa 5: visão geral do sistema (10 pts)
**O que precisa aparecer:** tudo junto em uma tela só — **código**, **circuito
com os atuadores reagindo** (LED colorido, relé acionado) e **Serial Monitor**.

> Melhor momento: com o sistema em `CRITICO` (LED vermelho piscando + relé ON)
> ou em `PORTA_ABERTA` (LED amarelo). Dá para ver o nó agindo sozinho.

---

## Roteiro completo de 2 minutos (cobre todos os prints)

| Tempo | Ação no simulador | Estado esperado | Print |
|---|---|---|---|
| 0:00 | Iniciar com T=5 °C, UR=60 %, LDR escuro | `NORMAL` (LED verde) | 1 e 2 |
| 0:20 | Subir T para 6 °C | `RESFRIANDO` (LED azul, relé ON) | 3 |
| 0:40 | Subir T para 8,5 °C | `CRITICO` (LED vermelho, buzzer) | — |
| 1:00 | Voltar T para 3 °C | `NORMAL` | — |
| 1:15 | Aumentar o LDR para ~1000 lux e esperar 3 s | `PORTA_ABERTA` (LED amarelo) | — |
| 1:35 | Manter porta aberta e pôr T=7 °C, UR=80 % | `CRITICO` (prioridade) | 4 e 5 |
| 1:55 | Oscilar T entre 5,8 e 6,1 °C | continua `RESFRIANDO`, sem oscilar | — |

O log completo dessa sequência, gerado pelo mesmo firmware, está em
[`serial_experimentos.txt`](serial_experimentos.txt) — use para conferir se a
sua simulação está reagindo igual.
