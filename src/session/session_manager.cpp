#include "session/session_manager.h"

#include "config.h"
#include "relay/relay.h"

namespace session {
namespace {

Phase phase = Phase::IDLE;
unsigned long sessionStartMs = 0;   // millis() no instante do comando
unsigned long startDelayMs = 0;     // espera antes de apertar o botão
unsigned long durationMs = 0;       // quanto tempo a cadeira fica ligada
unsigned long cooldownStartMs = 0;  // millis() do pulso que desligou
long currentSessionId = -1;         // id da sessão no backend, para log

/**
 * Se a cadeira está ligada.
 *
 * Rastreado aqui porque o hardware não conta: o relé só encosta no botão por
 * RELAY_PULSE_MS e volta a abrir, então ler o pino não diz nada sobre a
 * cadeira. Um reset do ESP32 no meio da sessão zera esta variável e a cadeira
 * fica ligada até alguém apertar o botão na mão — o preço de comandar por
 * botão em vez de cortar a alimentação.
 */
bool chairOn = false;

/** Aperta o botão da cadeira. `turningOn` é o que o pulso significa desta vez. */
void pressButton(bool turningOn) {
  relay::pulse();
  chairOn = turningOn;
}

/** Abre a janela de estabilização; só faz sentido logo após o pulso de desligar. */
void enterCooldown() {
  phase = Phase::COOLDOWN;
  cooldownStartMs = millis();
  Serial.printf("cadeira estabilizando por %lus\n", CHAIR_COOLDOWN_MS / 1000);
}

/** Segundos restantes da estabilização; 0 fora da fase COOLDOWN. */
unsigned long cooldownRemainingSeconds() {
  if (phase != Phase::COOLDOWN) {
    return 0;
  }
  unsigned long elapsed = millis() - cooldownStartMs;
  if (elapsed >= CHAIR_COOLDOWN_MS) {
    return 0;
  }
  return (CHAIR_COOLDOWN_MS - elapsed) / 1000;
}

/**
 * Recusa comum a /start e /relay-test.
 *
 * Devolve ACCEPTED quando a cadeira está livre — quem chama segue em frente.
 */
StartResult checkAvailability(const char* command) {
  if (phase == Phase::COOLDOWN) {
    Serial.printf("comando %s recusado: estabilizando, faltam %lus\n", command,
                  cooldownRemainingSeconds());
    return StartResult::COOLING_DOWN;
  }
  if (phase != Phase::IDLE) {
    Serial.printf("comando %s recusado: sessao em andamento\n", command);
    return StartResult::BUSY;
  }
  return StartResult::ACCEPTED;
}

void printRelayTestBanner(long seconds) {
  Serial.println();
  Serial.println("**********************************************************");
  Serial.printf("  TESTE DE RELE - PULSO DE LIGAR, DESLIGA EM %lds\n", seconds);
  Serial.printf("  Pino %d  |  nivel ativo: %s  |  pulso: %lums\n", RELAY_PIN,
                RELAY_ON == LOW ? "LOW" : "HIGH", RELAY_PULSE_MS);
  Serial.println("  Se a cadeira nao ligou, confira VCC/GND/IN e a fonte");
  Serial.println("  Se ligou e apitou, o contato ficou fechado: reveja o pulso");
  Serial.println("**********************************************************");
}

void printRelayTestDone() {
  Serial.println();
  Serial.println("**********************************************************");
  Serial.println("  TESTE DE RELE - PULSO DE DESLIGAR");
  Serial.println("**********************************************************");
  Serial.println();
}

}  // namespace

void begin() {
  phase = Phase::IDLE;
  currentSessionId = -1;
  // Sem pulso no boot: um pulso aqui ligaria a cadeira sozinha depois de cada
  // reset. O contato já nasce aberto em relay::begin().
  chairOn = false;
}

StartResult start(long sessionId, long durationSeconds, long startDelaySeconds) {
  StartResult availability = checkAvailability("/start");
  if (availability != StartResult::ACCEPTED) {
    return availability;
  }

  if (durationSeconds <= 0) {
    return StartResult::INVALID_DURATION;
  }

  durationMs = (unsigned long)durationSeconds * 1000UL;
  if (durationMs > MAX_SESSION_MS) {
    durationMs = MAX_SESSION_MS;
    Serial.println("duracao acima do teto; limitada a MAX_SESSION_MS");
  }

  startDelayMs = (unsigned long)max(0L, startDelaySeconds) * 1000UL;
  currentSessionId = sessionId;
  sessionStartMs = millis();
  phase = startDelayMs > 0 ? Phase::STARTING : Phase::RUNNING;

  // Sem espera configurada, o botão é apertado agora.
  if (phase == Phase::RUNNING) {
    pressButton(true);
    Serial.printf("[sessao %ld] pulso de ligar\n", sessionId);
  }

  Serial.printf("[sessao %ld] aceita: %lus de espera, %lus de duracao\n",
                sessionId, startDelayMs / 1000, durationMs / 1000);
  return StartResult::ACCEPTED;
}

StartResult startRelayTest(long durationSeconds) {
  StartResult availability = checkAvailability("/relay-test");
  if (availability != StartResult::ACCEPTED) {
    return availability;
  }

  long seconds = durationSeconds;
  if (seconds <= 0 || seconds > RELAY_TEST_MAX_SECONDS) {
    seconds = RELAY_TEST_SECONDS;
  }

  durationMs = (unsigned long)seconds * 1000UL;
  startDelayMs = 0;
  currentSessionId = -1;
  sessionStartMs = millis();
  phase = Phase::TESTING;
  pressButton(true);

  printRelayTestBanner(seconds);
  return StartResult::ACCEPTED;
}

void stop(const char* reason) {
  if (phase == Phase::IDLE || phase == Phase::COOLDOWN) {
    return;
  }

  long endedSessionId = currentSessionId;
  currentSessionId = -1;

  // Parar durante a espera do início: a cadeira nunca chegou a ser ligada, um
  // pulso aqui a ligaria em vez de desligar, e não há o que estabilizar.
  if (!chairOn) {
    phase = Phase::IDLE;
    Serial.printf("[sessao %ld] encerrada antes de ligar: %s\n", endedSessionId, reason);
    return;
  }

  pressButton(false);
  Serial.printf("[sessao %ld] pulso de desligar\n", endedSessionId);
  Serial.printf("[sessao %ld] encerrada: %s\n", endedSessionId, reason);
  enterCooldown();
}



void loop() {
  relay::loop();

  unsigned long now = millis();

  if (phase == Phase::STARTING && now - sessionStartMs >= startDelayMs) {
    phase = Phase::RUNNING;
    pressButton(true);
    Serial.printf("[sessao %ld] pulso de ligar\n", currentSessionId);
  }

  if (phase == Phase::RUNNING && now - sessionStartMs >= startDelayMs + durationMs) {
    stop("tempo concluido");
  }

  // O teste manual usa o mesmo relógio, mas encerra com log próprio: quem está
  // com o monitor aberto precisa ver o desligamento sem confundir com sessão.
  if (phase == Phase::TESTING && now - sessionStartMs >= durationMs) {
    pressButton(false);
    printRelayTestDone();
    // Estabiliza igual: o teste liga a cadeira de verdade.
    enterCooldown();
  }

  if (phase == Phase::COOLDOWN && now - cooldownStartMs >= CHAIR_COOLDOWN_MS) {
    phase = Phase::IDLE;
    Serial.println("cadeira estabilizada; pronta para a proxima sessao");
  }
}

unsigned long plannedDurationSeconds() {
  return durationMs / 1000;
}

Snapshot snapshot() {
  Snapshot state{};
  state.phase = phase;
  state.phaseName = "idle";
  state.remainingSeconds = 0;
  state.sessionId = currentSessionId;
  state.chairOn = chairOn;

  if (phase == Phase::IDLE) {
    return state;
  }

  if (phase == Phase::COOLDOWN) {
    state.phaseName = "cooldown";
    state.remainingSeconds = cooldownRemainingSeconds();
    return state;
  }

  unsigned long elapsed = millis() - sessionStartMs;

  if (elapsed < startDelayMs) {
    state.phaseName = "starting";
    state.remainingSeconds = (startDelayMs - elapsed) / 1000;
    return state;
  }

  state.phaseName = phase == Phase::TESTING ? "testing" : "running";
  unsigned long usedMs = elapsed - startDelayMs;
  state.remainingSeconds = usedMs >= durationMs ? 0 : (durationMs - usedMs) / 1000;
  return state;
}

bool isIdle() {
  return phase == Phase::IDLE;
}

}  // namespace session
