# Checkpoint — Nó de Borda Inteligente
**FIAP · Graduação em Ciência da Computação · Edge Computing · Prof. Flavio M. Azevedo**
Checkpoint 1 · Aula 6 · Atividade avaliativa em dupla

| | |
|---|---|
| **Aluno 1:** | **RM:** |
| **Aluno 2:** | **RM:** |
| **Turma:** | **Data:** |

**Link do projeto (repositório):** https://github.com/lincoln743/cp01_edgecomputing

---

## Etapa 1 · Ambiente e montagem — 20 pontos

**Print de tela —** circuito montado + `platformio.ini`
`[ COLE AQUI O PRINT: Wokwi com o circuito à direita e o platformio.ini aberto à esquerda ]`

### Perguntas
**(a) Quais 2 sensores e 2 atuadores vocês escolheram?**

**Sensores (2):**
- **DHT22** — temperatura (°C) e umidade relativa (%), ligado ao **GPIO 15**.
- **LDR / fotorresistor** (módulo com saída analógica `AO`), ligado ao **GPIO 34**,
  usado como **sensor de porta aberta**: a câmara fechada é escura, então um salto
  de luminosidade significa porta aberta.

**Atuadores (3 — o mínimo pedido era 2):**
- **Módulo relé** (GPIO 26) — liga/desliga o **compressor** da câmara.
- **Buzzer** (GPIO 25) — **alarme sonoro local** para o operador.
- **LED RGB** (GPIO 27 / 14 / 13) — **semáforo visual** do estado do nó.

**(b) Como cada escolha se justifica diante do problema da FrioLog?**

O cenário diz que as perdas vieram de **oscilações de temperatura e umidade
percebidas tarde demais**. Cada peça ataca uma parte desse problema:

- **DHT22:** mede exatamente as duas grandezas citadas no cenário, em um único
  componente digital calibrado (±0,5 °C). Medir umidade não é detalhe: a câmara
  guarda medicamentos, e umidade alta junto com temperatura em elevação indica
  risco de **condensação** sobre a carga — dano que a temperatura sozinha não revela.
- **LDR:** a causa mais comum de oscilação térmica em câmara fria é **porta aberta
  ou mal fechada**. O LDR é barato, tem resposta imediata e, mais importante,
  fornece o **contexto** que explica *por que* a temperatura subiu. É ele que
  permite o nó agir **antes** de a temperatura sair da faixa.
- **Relé:** a resposta útil da câmara é ligar refrigeração/ventilação; o relé é a
  interface padrão entre um sinal de 3,3 V do ESP32 e uma carga de potência real.
- **Buzzer:** o prejuízo veio da **percepção tardia**. Um alarme sonoro dentro da
  câmara avisa quem está no local **no mesmo instante**, sem depender de rede,
  e-mail ou celular.
- **LED RGB:** dá diagnóstico visual instantâneo na porta da câmara — o operador
  sabe se está tudo normal (verde), resfriando (azul), porta aberta (amarelo),
  crítico (vermelho) ou com sensor falhando (magenta), sem abrir o Serial Monitor.

---

## Etapa 2 · Leitura dos sensores — 20 pontos

**Print de tela —** Serial Monitor com as leituras
`[ COLE AQUI O PRINT: Serial Monitor mostrando linhas "T= 5.0 C | UR= 60.0 % | LDR=3800 ..." ]`

Formato de cada linha impressa pelo firmware:

```
[     2s] T=  5.0 C | UR= 60.0 % | LDR=3800 (porta fechada) | ESTADO=NORMAL       | RELE=OFF BUZZER=OFF
```

### Pergunta
**Cada sensor é analógico ou digital? Como isso muda a forma de ler no código?**

| | **DHT22** | **LDR** |
|---|---|---|
| Tipo | **Digital** | **Analógico** |
| Como entrega o dado | Trem de pulsos em 1 fio (protocolo proprietário, tipo 1-wire) | Tensão contínua num divisor resistivo |
| Como se lê | Biblioteca `DHT.h` decodifica os bits: `dht.readTemperature()` / `dht.readHumidity()` | `analogRead(34)` no conversor A/D de 12 bits do ESP32 |
| O que volta | O **valor já na unidade de engenharia** (°C e %) | Um número **cru de 0 a 4095**, sem unidade |
| Cuidados | Máximo **1 leitura a cada 2 s**; devolve **`NaN`** quando falha — o código **precisa** checar com `isnan()` | Precisa de **calibração/limiar** para virar informação ("aberta"/"fechada") e de **histerese**, porque a leitura oscila com ruído |

Na prática isso muda três coisas no código:

1. **Temporização:** o `loop()` amostra a cada 2000 ms (`INTERVALO_AMOSTRA_MS`)
   por causa do limite do DHT22 — o LDR aceitaria leitura muito mais rápida.
2. **Validação:** só o sensor digital tem "leitura inválida" explícita (`NaN`),
   e é isso que alimenta o estado `FALHA_SENSOR`.
3. **Interpretação:** o DHT22 já entrega grandeza física; o LDR exige que o
   código transforme contagens de ADC em significado, com dois limiares
   (1500 e 2500) mais um tempo mínimo de 3 s.

> **Observação importante da montagem:** no módulo LDR do Wokwi a tensão de `AO`
> **diminui** quando a luz aumenta. Portanto **escuro (porta fechada) ≈ 3400–4000**
> contagens e **claro (porta aberta) ≈ 700–1300** contagens.

---

## Etapa 3 · Tomada de decisão local no embarcado — 20 pontos

**Trecho de código — função responsável pela decisão local**

```cpp
/* --------------------------------------------------------------------------
 * DECISAO LOCAL  (roda 100% dentro do ESP32, sem rede)
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
```

**Print de tela —** sistema mudando de estado por decisão local
`[ COLE AQUI O PRINT: Serial Monitor exibindo o bloco ">>> MUDANCA DE ESTADO: NORMAL -> RESFRIANDO" ]`

### Perguntas

**(a) Quais estados operacionais foram definidos e quais regras levam o sistema a cada estado?**

Foram definidos **5 estados** (o mínimo pedido era 3), na variável `estadoAtual`:

| Prioridade | Estado | Regra de entrada | Atuadores |
|---|---|---|---|
| 1 | `FALHA_SENSOR` | `dht.readTemperature()` ou `readHumidity()` devolve `NaN` | relé **OFF** (fail-safe), buzzer 1000 Hz, LED magenta piscando |
| 2 | `CRITICO` | `T ≥ 8,0 °C` **ou** (`T ≥ 6,0 °C` **e** `UR ≥ 75 %`) **ou** (`T ≥ 6,0 °C` **e** porta aberta) | relé **ON** (frio máximo), buzzer 2000 Hz rápido, LED vermelho piscando |
| 3 | `PORTA_ABERTA` | `LDR ≤ 1500` de forma sustentada por mais de 3 s | relé **OFF** (não desperdiça compressor com a porta aberta), buzzer 1500 Hz lento, LED amarelo |
| 4 | `RESFRIANDO` | `T ≥ 6,0 °C` — e só sai quando `T ≤ 4,0 °C` | relé **ON**, buzzer mudo, LED azul |
| 5 | `NORMAL` | nenhuma das anteriores: 2–8 °C, UR < 75 %, porta fechada | relé **OFF**, buzzer mudo, LED verde |

**(b) Como o código resolve condições simultâneas e determina qual decisão deve prevalecer?**

Por **três mecanismos explícitos**:

1. **Prioridade codificada na ordem do `enum`.** `ST_FALHA_SENSOR = 0`,
   `ST_CRITICO = 1`, `ST_PORTA_ABERTA = 2`, `ST_RESFRIANDO = 3`, `ST_NORMAL = 4`.
   A função `decidirEstado()` é uma cadeia de `if` com `return` imediato: a
   **primeira** condição verdadeira encerra a avaliação, então o estado de
   menor valor sempre vence. Não existe "empate" possível — a função tem
   um único ponto de decisão e sempre devolve exatamente um estado.
2. **Segurança antes de conforto.** `FALHA_SENSOR` vem antes de tudo porque,
   sem leitura confiável, qualquer decisão sobre a carga seria um chute — o nó
   desliga o compressor e alarma. Depois vem `CRITICO`, porque risco de perda
   da carga é mais urgente do que economia de energia.
3. **Estabilidade na fronteira.** Cada limiar tem **histerese** (compressor liga
   em 6,0 °C e só desliga em 4,0 °C; crítico entra em 8,0 e só sai em 7,0;
   umidade entra em 75 % e sai em 70 %; porta abre em 1500 e fecha em 2500) e
   há um **tempo mínimo de permanência** de 4 s em cada estado
   (`TEMPO_MIN_ESTADO_MS`) — com exceção de `CRITICO` e `FALHA_SENSOR`, que
   entram imediatamente por serem condições de segurança.

**(c) Qual parte do código comprova que a decisão ocorre localmente no ESP32, sem depender de comunicação externa?**

- **O que existe:** todo o ciclo *perceber → decidir → agir* está no `loop()`:
  `lerSensores()` → `decidirEstado()` → `aplicarAtuadores(estadoAtual)`. Os
  atuadores são comandados por `digitalWrite()`/`tone()` diretamente pelo
  valor devolvido por `decidirEstado()`.
- **O que NÃO existe:** o firmware não inclui `WiFi.h`, `HTTPClient.h`,
  `PubSubClient` (MQTT), `BluetoothSerial` nem qualquer outra biblioteca de
  comunicação — as únicas dependências em `platformio.ini` são a `DHT sensor
  library` e a `Adafruit Unified Sensor`. Não há SSID, senha, broker, URL ou
  token em lugar nenhum do código.
- **Verificação prática:** `grep -i "wifi\|http\|mqtt\|bluetooth" src/main.cpp`
  não retorna nenhuma linha de código.
- O `Serial.print` é apenas **observação para o avaliador**: se o cabo USB for
  retirado, as decisões e os acionamentos continuam acontecendo igual.

---

## Etapa 4 · Validação do comportamento do nó — 30 pontos

> Os resultados abaixo foram obtidos executando o firmware e registrando o Serial
> Monitor. O log completo dos seis experimentos está em
> [`docs/serial_experimentos.txt`](serial_experimentos.txt).

| Experimento | O que foi feito | **Resultado observado** |
|---|---|---|
| **1. Condição de referência** | DHT22 em **5,0 °C / 60 %**, LDR no escuro (**3800**) | Estado **`NORMAL`**. Serial: `T= 5.0 C \| UR= 60.0 % \| LDR=3800 (porta fechada) \| ESTADO=NORMAL \| RELE=OFF BUZZER=OFF`. Atuadores: relé **desligado**, buzzer mudo, **LED verde** fixo. Estado estável durante todo o intervalo, sem nenhuma troca. |
| **2. Variação do sensor 1 (DHT22)** | LDR fixo em 3800; temperatura elevada de **5,0 → 9,0 °C** em passos de 0,5 °C | **Duas** mudanças de estado, exatamente nos limiares projetados: em **6,0 °C** → `NORMAL → RESFRIANDO` (motivo registrado: *"temperatura >= 6.0 C"*), relé **liga** e LED fica **azul**; em **8,0 °C** → `RESFRIANDO → CRITICO` (motivo: *"temperatura >= 8.0 C"*), relé **continua ligado**, buzzer passa a **bipar a 2000 Hz** e LED fica **vermelho piscando**. Entre 6,5 e 7,5 °C o estado permaneceu `RESFRIANDO`, sem oscilar. |
| **3. Variação do sensor 2 (LDR)** | DHT22 fixo em **3,0 °C / 60 %**; LDR variado de 3800 → 900 (escurecendo → clareando) | Nada mudou em 3400, 3000, 2600, 2200 e 1800 — a leitura ainda estava acima do limiar. A mudança ocorreu com **LDR = 1400** (≤ 1500), e **somente depois de ~3 s de luz sustentada**: `NORMAL → PORTA_ABERTA` (motivo: *"LDR <= 1500 (muita luz) por mais de 3 s"*). Atuadores: relé **desligado** (economia — não adianta comprimir com a porta aberta), **buzzer intermitente lento** a 1500 Hz, **LED amarelo**. Ao voltar a 3800, retornou a `NORMAL`. |
| **4. Condição combinada** | **T = 7,0 °C**, **UR = 80 %** e **porta aberta (LDR = 900)** ao mesmo tempo | **Três regras verdadeiras simultaneamente**: `RESFRIANDO` (T ≥ 6,0), `PORTA_ABERTA` (LDR ≤ 1500) e `CRITICO` (T ≥ 6,0 com UR ≥ 75 % **e** T ≥ 6,0 com porta aberta). **Prevaleceu `CRITICO`**, o estado de menor valor no `enum`. O Serial deixa isso visível: a linha mostra `LDR= 900 (PORTA ABERTA) \| ESTADO=CRITICO` — ou seja, o nó reconhece a porta aberta mas mantém o estado mais grave. Atuadores conforme `CRITICO`: relé **ON**, buzzer rápido, LED vermelho piscando. |
| **5. Região próxima à decisão** | T oscilando em torno do limiar de 6,0 °C: 5,8 → 5,9 → **6,0** → 5,9 → 6,1 → 5,8 → 5,5 → 4,5 → **4,0** → 3,8 °C | O estado entrou em `RESFRIANDO` **uma única vez**, ao tocar 6,0 °C, e **permaneceu** durante 5,9 / 6,1 / 5,8 / 5,5 / 4,5 °C — **nenhum chaveamento repetido**, graças à histerese de 2 °C. Só voltou para `NORMAL` ao atingir **4,0 °C**. O intervalo entre leitura e decisão é de **2 s** (uma amostra do DHT22 por ciclo), e a decisão é aplicada aos atuadores na mesma volta do `loop()`. |
| **6. (extra) Falha de sensor** | DHT22 devolvendo `NaN` | `NORMAL → FALHA_SENSOR` imediatamente: relé **desligado por segurança**, buzzer 1000 Hz, LED magenta piscando. Ao restabelecer o sensor, voltou a `NORMAL` na leitura seguinte. |

**Print de tela —** um dos experimentos, com o Serial Monitor visível
`[ COLE AQUI O PRINT: recomendado o Experimento 4, que mostra a prioridade funcionando ]`

### Análise

**Os resultados correspondem ao que o código deveria decidir?** Sim — nos seis
experimentos o estado observado no Serial Monitor bateu com a regra prevista, e
as transições ocorreram exatamente nos limiares escritos no código (6,0 °C,
8,0 °C, 1500 contagens, 4,0 °C na volta).

**Comportamento satisfatório:** o tratamento de **condições simultâneas**
(Experimento 4). Com três regras verdadeiras ao mesmo tempo, o nó escolheu
`CRITICO` sem ambiguidade e continuou registrando a porta aberta na mesma linha
de log — provando que ele **percebe tudo, mas age pela regra mais grave**.
Igualmente satisfatória foi a **estabilidade na fronteira** (Experimento 5): com
a temperatura oscilando em torno de 6,0 °C, um controle sem histerese ligaria e
desligaria o compressor a cada 2 s, o que destrói o equipamento na vida real;
aqui houve **uma única** troca de estado, e o relé só desligou em 4,0 °C.

**Aspecto que ainda merece ajuste:** o **tempo de reação está preso ao período
de amostragem de 2 s do DHT22** — como o LDR é lido no mesmo ciclo, uma porta
aberta pode demorar até 2 s a mais para ser percebida do que o necessário. O
ajuste natural é desacoplar as duas taxas: ler o LDR a cada 200 ms (ele não tem
limite de taxa) e manter o DHT22 em 2 s, o que reduziria o atraso de detecção da
porta sem violar o datasheet do sensor. Um segundo ponto é que os limiares do
LDR (1500 / 2500) foram definidos para a iluminação da simulação — em campo eles
precisariam de uma **calibração com a porta fechada**, de preferência
automática na inicialização.

---

## Etapa 5 · Integração e reflexão — 10 pontos

**Print de tela —** visão geral do sistema em funcionamento
`[ COLE AQUI O PRINT: Wokwi rodando — circuito com LED aceso + Serial Monitor + código, tudo na mesma tela ]`

### Perguntas

**(a) Por que este nó é um exemplo de Edge Computing? Que decisões ele toma sem depender de internet?**

Porque o **processamento acontece onde o dado nasce** — dentro da câmara — e não
em um servidor remoto. O ESP32 não envia leituras para ninguém decidir por ele:
ele fecha o ciclo *perceber → decidir → agir* localmente, em cerca de 2 s, e o
firmware sequer contém biblioteca de rede.

Decisões tomadas de forma autônoma:
- **Ligar e desligar o compressor** (`RESFRIANDO`, com histerese 6,0 / 4,0 °C).
- **Declarar situação crítica** por temperatura, por temperatura + umidade
  (condensação) ou por temperatura + porta aberta, e **disparar o alarme sonoro**.
- **Reconhecer porta aberta** pela luminosidade e **cortar o compressor** para não
  desperdiçar energia resfriando o corredor.
- **Entrar em modo seguro** quando o sensor falha, desligando o compressor.
- **Resolver prioridades** quando várias condições são verdadeiras ao mesmo tempo.

Os três motivos clássicos de edge aparecem todos aqui: **latência** (a decisão
não pode esperar um round-trip), **autonomia** (dentro de uma câmara metálica a
conectividade é ruim — e é justamente aí que o sistema não pode parar) e
**volume de dados** (não faz sentido subir uma leitura a cada 2 s, 24 h por dia,
para tomar uma decisão que o próprio nó toma).

**(b) Se o protótipo virasse produto, o que faria sentido enviar para a nuvem — e o que deveria continuar sendo decidido na borda?**

| Continua **na borda** (tempo real / segurança) | Vai para a **nuvem** (visão histórica / negócio) |
|---|---|
| Liga/desliga do compressor | Série histórica de T e UR (amostrada ou agregada: média, mín, máx a cada 5–15 min) |
| Disparo do alarme sonoro local | **Eventos** de mudança de estado, com carimbo de tempo e a leitura que causou |
| Detecção de porta aberta e corte do compressor | Relatórios de **conformidade** para auditoria (Anvisa, cliente, seguradora) |
| Modo seguro em caso de falha de sensor | Notificação ao gestor (e-mail/app) quando a câmara entra em `CRITICO` |
| Arbitragem de prioridades entre regras | Indicadores de manutenção: nº de aberturas de porta, tempo com compressor ligado, deriva do sensor |
| Buffer local dos eventos enquanto a rede estiver fora | Ajuste remoto de limiares (que o nó **valida** antes de aplicar) |

O princípio: **o que protege a carga fica na borda; o que informa a decisão
humana vai para a nuvem.** Nenhuma ação de segurança pode depender de um pacote
que talvez não chegue — a nuvem entra como memória, visão de frota e alerta a
distância, nunca como caminho crítico.

**(c) Que vantagem o VS Code + PlatformIO trouxe em relação a editar direto no Wokwi web?**

- **Versionamento real:** o projeto inteiro (`src/`, `diagram.json`,
  `platformio.ini`) é um repositório Git — há histórico, branches, diff e
  trabalho em dupla sem sobrescrever o código do colega. No Wokwi web o
  compartilhamento é por link.
- **Dependências declaradas e reprodutíveis:** `platformio.ini` fixa placa,
  framework, versão da plataforma (`espressif32@6.9.0`), bibliotecas e
  `monitor_speed`. Qualquer pessoa clona e compila igual — não há "na minha
  máquina funciona".
- **Editor de verdade:** IntelliSense, ir para definição, refatorar, buscar no
  projeto e formatar — em um arquivo de ~370 linhas isso muda o ritmo de trabalho.
- **Compilação local e rápida:** `pio run` aponta o erro em segundos, sem
  depender de servidor; dá para checar uso de RAM/Flash
  (aqui: **RAM 6,6 %, Flash 21,3 %**).
- **O mesmo projeto vai para o hardware real:** trocar simulação por placa física
  é rodar `pio run -t upload` — nada precisa ser reescrito, o que importa muito
  quando a FrioLog aprovar a prova de conceito.
- **Ferramentas extras:** foi possível criar um **harness em `tools/sim/`** que roda
  o *mesmo* `src/main.cpp` no PC com leituras controladas, reproduzindo os cinco
  experimentos da Etapa 4 de forma repetível — algo impossível editando só no
  navegador.
