# Plan — Corregir y mejorar `onQuarterFrame` en mtcreceiver + integración en gradient-motion-engine

> **v2 — actualizado tras review crítico de Sonnet.** Cambios principales: (1) shim deprecated descartado, se reescriben tests del consumer; (2) refactor a single-fire-per-QF (elimina doble fire y doble lock); (3) `consumeFullFrameFlag()` descartado tras verificar el patrón split read/reset del driver; (4) clarificadas variables instance vs static; (5) destructor de `MtcTickSource` explicitado; (6) verificación de SHA del audioplayer añadida; (7) priority inversion documentada.

## Contexto

El submódulo [mtcreceiver](https://github.com/stagesoft/mtcreceiver) (compartido por cuems-videocomposer, cuems-dmxplayer, cuems-audioplayer y ahora gradient-motion-engine) acaba de mergear el [PR #3](https://github.com/stagesoft/mtcreceiver/pull/3) (commit `92ee66d`, autor Adrià Masip) que añade:

- `static std::function<void(long)> onQuarterFrame` que dispara desde el thread MIDI callback de RtMidi en dos sitios (Site 1: cada QF extrapolado; Site 2: cada frame completo decodificado).
- `unsigned int portIndex = 0` en el constructor (cambio limpio, sin pegas).
- `decodeFullFrame()` intencionadamente NO dispara el callback.

El **consumidor real** es [gradient-motion-engine](https://github.com/stagesoft/gradient-motion-engine) (rama `feat/base-code`, no clonado localmente). Su clase `MtcTickSource` ya implementada hace:

```cpp
void MtcTickSource::setTickCallback(std::function<void(long)> cb) {
    MtcReceiver::onQuarterFrame = std::move(cb);  // ← race condition
}
```

Propósito del consumer: generar curvas de fundido sincronizadas a MTC con resolución de quarter frame, latencia <1ms, output OSC.

### Bugs y races confirmados

**Críticos (introducidos o agravados por PR #3):**

| ID | Severidad | Archivo:línea | Descripción |
|----|-----------|---------------|-------------|
| C1 | Crítico | [mtcreceiver.h:156, mtcreceiver.cpp:43,285](src/cuems-videocomposer/src/mtcreceiver/mtcreceiver.cpp) | `std::function` no atómico → UB al asignarlo desde un thread mientras MIDI thread lo lee. `MtcTickSource::setTickCallback` lo hace. |
| C2 | Alto | [mtcreceiver.cpp:285,304](src/cuems-videocomposer/src/mtcreceiver/mtcreceiver.cpp#L285) | Doble disparo en QF #8 (Site 1 + Site 2 consecutivos en la misma invocación). |
| C3 | Medio | [mtcreceiver.cpp:285](src/cuems-videocomposer/src/mtcreceiver/mtcreceiver.cpp#L285) | Site 1 dispara antes de evaluar `reset` → ticks fantasma en secuencias QF inválidas. |

**Preexistentes (no del PR pero entran al mismo código):**

| ID | Severidad | Archivo:línea | Descripción |
|----|-----------|---------------|-------------|
| P1 | Crítico | [mtcreceiver.cpp:40,301,357](src/cuems-videocomposer/src/mtcreceiver/mtcreceiver.cpp#L40) | `wasLastUpdateFullFrame` es `bool` no atómico, escrito desde MIDI thread, **leído** en [MtcReceiverMIDIDriver.cpp:155](src/cuems-videocomposer/src/cuems_videocomposer/cpp/sync/MtcReceiverMIDIDriver.cpp#L155) y **reescrito a false** en [L282](src/cuems-videocomposer/src/cuems_videocomposer/cpp/sync/MtcReceiverMIDIDriver.cpp#L282). Race write+write. |
| P2 | Crítico | [mtcreceiver.cpp:41,221-258](src/cuems-videocomposer/src/mtcreceiver/mtcreceiver.cpp#L41) | `curFrame` ahora `static` no atómico, escrito campo a campo, leído por copia → torn read. |
| P3 | Alto | [mtcreceiver.cpp:311,392,403](src/cuems-videocomposer/src/mtcreceiver/mtcreceiver.cpp#L311) | `timecodeRunWeight`, `timecodeStartTimestamp`, `timecodeTimestamp` (miembros de **instancia**, no static) escritos en MIDI thread, leídos por dmxplayer en `isTimecodeActive()` / `estimatedCurrentHead()` desde el thread OLA → cross-thread sobre la misma instancia. |
| P4 | Medio | [mtcreceiver.cpp:132-133](src/cuems-videocomposer/src/mtcreceiver/mtcreceiver.cpp#L132) | `MtcFrame::fromSeconds`: `(1/60)` integer division → 0. Minutos y horas siempre quedan a 0. |
| P5 | Medio | [mtcreceiver.cpp:142](src/cuems-videocomposer/src/mtcreceiver/mtcreceiver.cpp#L142) | `MtcFrame::msToFrames`: `ms / (getFps()/1000.0)` invierte la conversión. 1000ms@25fps devuelve 40000 en lugar de 25. |
| P6 | Bajo | [mtcreceiver.cpp:348-365](src/cuems-videocomposer/src/mtcreceiver/mtcreceiver.cpp#L348) | `decodeFullFrame` no resetea `quarterFrame`/`qfCount`/flags. |
| P7 | Bajo | [mtcreceiver.cpp:325-336,363-364](src/cuems-videocomposer/src/mtcreceiver/mtcreceiver.cpp#L325) | `std::cout << ... << endl` en MIDI thread (bloqueante por flush). |
| P8 | Bajo | [mtcreceiver.cpp:69](src/cuems-videocomposer/src/mtcreceiver/mtcreceiver.cpp#L69) | Constructor llama `exit()` si no hay puertos MIDI. |

### Spec del consumer

[`specs/003-mtc-tick-source/`](https://github.com/stagesoft/gradient-motion-engine/tree/feat/base-code/specs/003-mtc-tick-source) en gradient-motion-engine documenta:
- FR-009: solo 1 `MtcTickSource` por proceso (statics globales en MtcReceiver).
- Story 4 (P3): "Callback fires after both position-update sites" — Adrià diseñó el doble fire intencionalmente.
- Performance: zero heap allocation per tick, latencia <1ms, 200–240 Hz.
- **Inconsistencia interna detectada en `tests/test_mtc_tick_source.cpp`**: el test `testSyntheticMtcStream` usa 1200 callbacks; comentario del propio test resuelve la ambigüedad como 6s (no 60s) porque 200Hz × 60s = 12000, no 1200. **Revisar con Adrià antes de Fase 1.**
- Tests actuales mockean llamando `MtcReceiver::onQuarterFrame()` directo como puntero estático. **Esto se romperá con cualquier cambio de API.** Plan: reescribir esos tests (ver Fase 3.3).

---

## Decisiones — Análisis y recomendación final

### Decisión 1 — Cadencia y firma del callback

Tres opciones evaluadas (mantener Site 1 200Hz / quitar Site 1 25Hz / Site 1 con guard).

**Recomendación final: refactor a single-fire-per-QF con flag `isCompleteFrame`.**

Esta variante (no contemplada explícitamente en el análisis inicial) emerge de combinar las correcciones:
- Una sola invocación del callback por cada QF procesado válidamente.
- El flag `isCompleteFrame=true` solo en QF #8 cuando se completa la decodificación; `false` en QFs 1-7 (extrapolados).
- Si `reset==true` (secuencia inválida): no dispara nada.

Ventajas sobre las opciones originales:
- **Mantiene la cadencia 200Hz** que pide la spec (1 tick por QF).
- **Elimina el doble fire en QF #8** (la spec actual de Adrià lo tiene como diseño intencional, pero el coste — confusión y dedupe — supera al beneficio).
- **Elimina ticks fantasma** (guard de `reset`).
- **Una sola adquisición de mutex por QF** (evita el escenario de double-lock que permitiría a otro thread cambiar el callback entre Site 1 y Site 2).
- **Consumer no necesita deduplicar.**

Cambio respecto a la spec FR-Story-4 de Adrià: él especifica explícitamente "fires after both position-update sites". Esta propuesta lo simplifica a "fires after position update, with a flag indicating whether the position is from a complete frame decode or an extrapolation". Cambio menor en la práctica, requiere alineación.

**Firma final**: `void(long mtcHeadMs, bool isCompleteFrame)`.

### Decisión 2 — API del callback (registro)

Revisión: el shim deprecated propuesto en v1 era **defectuoso** (su `operator()(long)` desregistraba el callback en lugar de invocarlo). Tras analizar las alternativas, no hay forma elegante de mantener compatibilidad binaria con tests que invocan `MtcReceiver::onQuarterFrame(value)` directo Y al mismo tiempo introducir thread safety.

**Recomendación final: static + `std::mutex` global, sin shim deprecated.**

- Mantener `MtcReceiver` con miembros estáticos (compatible con FR-009 del consumer).
- Reemplazar `static std::function<void(long)> onQuarterFrame` por `static void setTickCallback(TickCallback)` con mutex interno.
- **Reescribir los tests del consumer** (`test_mtc_tick_source.cpp`) para usar la nueva API. El consumer aún no está en producción, los tests se actualizan en una sola PR coordinada (Fase 3).

Pros:
- C++17 puro (no requiere `std::atomic<std::shared_ptr<>>`).
- Garantía no-call-after-unregister via destructor de `MtcTickSource` que llama `setTickCallback({})`.
- Diseño coherente sin shim frágil.

Cons:
- Breaking change de API: cualquier consumer que use `MtcReceiver::onQuarterFrame = ...` o lo invoque debe actualizarse. Hoy el único es `MtcTickSource` (y sus tests). Verificar con grep en todos los repos stagesoft antes de mergear PR1.

### Tabla resumen recomendaciones

| Decisión | Recomendación | Por qué |
|---|---|---|
| Cadencia | Single-fire-per-QF con flag `isCompleteFrame` | 200Hz, sin doble fire, sin ticks fantasma, sin double-lock, sin dedupe en consumer |
| API registro | Static + mutex; **sin shim**, reescribir tests del consumer | C++17, simple, garantía de unregister, evita complejidad de shim defectuoso |

---

## Plan de trabajo

Estrategia: **rama interna nuestra primero**, validar end-to-end con gradient-motion-engine, luego PR upstream a stagesoft/mtcreceiver con evidencia. **Dos PRs separados**: uno para callback + thread-safety, otro para los bugs preexistentes.

### Fase 0 — Setup y verificaciones previas

1. **Clonar gradient-motion-engine** localmente en `/home/stagelab/src/gradient-motion-engine`, rama `feat/base-code`. Submódulos NO se actualizan a versiones futuras (cycle chicken-and-egg): se inicializan al estado actual de la rama. Más tarde, cuando exista nueva rama de `mtcreceiver`, se hace `git submodule set-url` y `git submodule update --remote` apuntando al fork local.

2. **Verificar SHA del submódulo `mtcreceiver` en cada consumer**:
   ```bash
   cd /home/stagelab/src/cuems-videocomposer/src/mtcreceiver && git rev-parse HEAD
   cd /home/stagelab/src/cuems-dmxplayer/mtcreceiver && git rev-parse HEAD
   cd /home/stagelab/src/cuems-audioplayer/src/mtcreceiver && git rev-parse HEAD
   ```
   - Si audioplayer está en un SHA muy antiguo (sin `portIndex`, sin atomics añadidos), un bump romperá su build. **Determinar antes de mergear PR1**: bumpeamos audioplayer (con el riesgo correspondiente) o lo dejamos como deuda explícita.

3. **Grep de uso de la API actual en TODOS los repos stagesoft**:
   ```bash
   grep -r "MtcReceiver::onQuarterFrame" /home/stagelab/src/
   grep -r "wasLastUpdateFullFrame" /home/stagelab/src/
   grep -r "MtcReceiver::getCurFrame" /home/stagelab/src/
   grep -r "fromSeconds\|msToFrames" /home/stagelab/src/
   ```
   Identificar cualquier consumidor oculto que el plan esté ignorando.

4. **Sync con Adrià** sobre dos puntos concretos:
   - Inconsistencia 1200 callbacks = 6s vs 60s en `test_mtc_tick_source.cpp` SC-001/002.
   - Cambio de doble fire (Site 1 + Site 2) a single-fire-per-QF con flag.

5. **Crear branches**:
   - `fix/quarter-frame-callback-safety` en mtcreceiver (rama interna primero).
   - `fix/mtc-callback-thread-safety` en gradient-motion-engine (paralela).

### Fase 1 — Test harness y baseline

**Objetivo**: evidencia cuantitativa antes de tocar código y baseline para verificar el fix.

1. En `mtcreceiver/tests/test_callback_harness.cpp` (nuevo):
   - Consumer mínimo que registra el callback actual, loguea `(wall_ns, mtcMs, deltaFromPrev)` en buffer en memoria.
   - Lanza contra `libmtcmaster` (`/home/stagelab/src/libmtcmaster/python/mtcsender.py`) en escenarios:
     - 25fps reproducción normal 10s
     - Seek con full SYSEX en medio
     - Pausa y reanudación
     - Cambio de framerate (24/25/30)
   - Métricas a capturar: cadencia real, jitter, patrón doble fire, comportamiento ante secuencias inválidas.
2. **Baseline**: dump JSON en `tests/baselines/before_fix.json`.
3. (Opcional pero muy recomendado) Compilar el harness con `-fsanitize=thread` para captar las races C1, P1, P2, P3 con evidencia de TSan.

### Fase 2 — PR1: Callback fix + thread-safety

Branch: `fix/quarter-frame-callback-safety` en fork local de mtcreceiver.

#### 2.1 Cambios en `mtcreceiver.h`

```cpp
public:
    using TickCallback = std::function<void(long mtcHeadMs, bool isCompleteFrame)>;

    // Registro thread-safe; nullptr para desregistrar.
    // Bloquea hasta que cualquier llamada en vuelo retorne (no-call-after-unregister).
    static void setTickCallback(TickCallback cb);

    // === ELIMINADO ===
    // static std::function<void(long)> onQuarterFrame;  // REMOVED — breaking change

    // Atomicidad
    static std::atomic<bool> wasLastUpdateFullFrame;  // ← era plain bool

    // getCurFrame ahora documenta snapshot consistente (mutex interno)
    static MtcFrame getCurFrame();

private:
    static std::mutex callbackMutex_;
    static TickCallback tickCallback_;
    static std::mutex curFrameMutex_;          // protege curFrame
    // Para timecodeRunWeight et al. (instance members), añadir:
    mutable std::mutex activeStateMutex_;      // protege timecode{RunWeight,StartTimestamp,Timestamp}
```

**Sin shim deprecated.** Cualquier consumer que use la antigua `MtcReceiver::onQuarterFrame` falla en compilación, lo que es preferible a fallar en runtime con UB.

#### 2.2 Definición de los nuevos statics en `mtcreceiver.cpp`

Añadir junto a las otras definiciones (líneas ~37-46):

```cpp
std::atomic<bool> MtcReceiver::wasLastUpdateFullFrame(false);  // antes: plain bool
std::mutex MtcReceiver::callbackMutex_;
MtcReceiver::TickCallback MtcReceiver::tickCallback_;
std::mutex MtcReceiver::curFrameMutex_;
```

(El `activeStateMutex_` es `mutable` instance member, no requiere definición fuera de clase.)

#### 2.3 Lógica del callback — single-fire-per-QF

Refactor de `decodeQuarterFrame` (la pieza clave del PR):

```cpp
void MtcReceiver::decodeQuarterFrame(std::vector<unsigned char> &message) {
    bool complete = false;
    unsigned char dataByte = message[1];
    unsigned char msgType = dataByte & 0xF0;

    // ... [lógica de dirección sin cambios] ...

    int n = msgType >> 4;
    int expected_qf_count = (-1 == direction ? QF_LEN - n : 1 + n);

    switch(msgType) {
        // ... [TODOS los cases sin cambios; cada uno hace `qfCount += 1` y posiblemente `complete = true`]
        default:
            return;  // sale sin disparar callback ni actualizar state
    }

    // === A PARTIR DE AQUÍ: switch ya ejecutado, qfCount ya incrementado ===

    bool reset = (qfCount != expected_qf_count);
    lastDataByte = dataByte;

    // Actualizar mtcHead extrapolado (necesario para consumers que pollean)
    mtcHead.store(mtcHead.load() + static_cast<long int>(250 / curFrame.getFps()));

    long int callbackMs = mtcHead.load();  // valor extrapolado por defecto

    // Si frame completo, snap al valor real
    if (complete) {
        quarterFrame.frames += 2;
        {
            std::lock_guard<std::mutex> lk(curFrameMutex_);   // P2 fix
            curFrame = quarterFrame;
        }
        callbackMs = curFrame.toMilliseconds();
        mtcHead.store(callbackMs);
        curFrameRate.store(static_cast<unsigned char>(curFrame.getFps()));
        wasLastUpdateFullFrame.store(false);  // P1 fix (atomic store)
    }

    // SINGLE FIRE per QF: una invocación, con flag indicando si es complete o extrapolado
    if (!reset) {
        std::lock_guard<std::mutex> lk(callbackMutex_);
        if (tickCallback_) tickCallback_(callbackMs, complete);
    }

    // Reset state si frame completo
    if (complete) {
        quarterFrame = MtcFrame();
        direction = 0;
        qfCount = 0;
        lastQFlag = firstQFlag = false;
    }

    // Timestamp + averaging logic (con activeStateMutex_, ver 2.5)
    {
        std::lock_guard<std::mutex> lk(activeStateMutex_);
        timecodeTimestamp = ns_now();
        // ... [resto del averaging sin cambios funcionales] ...
    }
}
```

**Notas clave del refactor**:
- `reset` se calcula **después** del switch (cuando `qfCount` ya está incrementado). El cálculo es correcto.
- `mtcHead.store(...)` se hace siempre (consumers pollers como videocomposer/dmxplayer dependen de esto). El callback no.
- **Una sola invocación del callback por QF**, con flag `isCompleteFrame`. Sin doble fire, sin double-lock.
- Si `reset==true`: state se actualiza pero el callback NO dispara (consumer no recibe ticks fantasma).

#### 2.4 Atomicidad de `wasLastUpdateFullFrame` (P1)

- Cambio a `std::atomic<bool>`.
- **NO introducir `consumeFullFrameFlag()`**. Verificación: el driver del videocomposer ([MtcReceiverMIDIDriver.cpp:155-285](src/cuems-videocomposer/src/cuems_videocomposer/cpp/sync/MtcReceiverMIDIDriver.cpp#L155)) tiene patrón split read/reset entre `pollFrame()` (lee + decide seek) y `wasFullFrameReceived()` (resetea). Ambos están protegidos por el mutex interno del driver. Cambiar a `exchange(false)` rompería la lógica de detección seek/resync.
- Con `wasLastUpdateFullFrame` ahora atómico, el patrón actual del driver sigue funcionando (read implícito = atomic load, write = atomic store). Race resuelta.

#### 2.5 `curFrame` (P2) y variables de active state (P3)

- **`curFrame`**: protegido por `static std::mutex curFrameMutex_`. Tres puntos de toque:
  - `decodeQuarterFrame` (rama complete): `curFrame = quarterFrame;` bajo lock.
  - `decodeFullFrame` (5 escrituras de campos): bajo lock (block scope).
  - `getCurFrame()`: lock-copy-unlock-return.
- **`timecodeRunWeight`, `timecodeStartTimestamp`, `timecodeTimestamp`** (P3): son **miembros de instancia**, no static. Pero sí cross-thread (escritura en MIDI callback, lectura desde dmxplayer's `mtcReceiver.isTimecodeActive()` / `estimatedCurrentHead()`). Protección via `mutable std::mutex activeStateMutex_` instance member, adquirido en:
  - `decodeQuarterFrame` (block scope al final, ver 2.3).
  - `decodeFullFrame` (envuelve `timecodeStartTimestamp = ns_now() - ...; timecodeRunWeight = 0.0;`).
  - `isTimecodeActive()` y `estimatedCurrentHead()` (lecturas).
- Alternativa considerada: tres `std::atomic<>`. Descartada porque `std::atomic<double>` no garantiza lock-free en C++17 estándar (aunque en GCC x86_64 funciona). Mutex es más portable y `isTimecodeActive`/`estimatedCurrentHead` no son hot-path crítico.

#### 2.6 Eliminar cout en MIDI thread (P7)

Sustituir bloques `std::cout << "MTC Receiver: ..."` por `#ifdef MTCRECV_VERBOSE_DIAG` con `printf` a stderr (off por defecto). Activable con `-DMTCRECV_VERBOSE_DIAG` para bringup.

#### 2.7 Priority inversion — documentación

El `callbackMutex_` se adquiere en MIDI thread (potencialmente RT en JACK/ALSA) y en thread de aplicación (al registrar). En Linux sin `PTHREAD_PRIO_INHERIT` esto puede causar priority inversion. Mitigaciones:
- Documentar en el header: "TickCallback debe completar en <100µs y no debe llamar a `setTickCallback` desde el handler".
- El registro de callback ocurre típicamente una sola vez al inicio del proceso (`MtcTickSource::setTickCallback` en init de gradient-motiond), no en hot path. Contención del mutex es prácticamente cero.
- Si más adelante se observa, considerar `pthread_mutexattr_setprotocol(PTHREAD_PRIO_INHERIT)` en glibc (requiere migrar de `std::mutex` a `pthread_mutex_t` directo, fuera de scope).

#### 2.8 Tests del PR1 (`mtcreceiver/tests/test_callback.cpp`, nuevo)

- Reproducir el harness de Fase 1 contra el código nuevo.
- Verificar:
  - 0 ticks en secuencias inválidas (guard `!reset` funciona).
  - QF #8 dispara **una sola vez** con `isCompleteFrame=true` (no doble fire).
  - QFs 1-7 disparan con `isCompleteFrame=false`.
  - `setTickCallback({})` bloquea hasta que la llamada en vuelo retorne; ningún callback se invoca después.
  - TSan limpio bajo carga MIDI real (libmtcmaster).
  - `getCurFrame()` snapshot consistente: fuzz con 1000 escrituras y 10000 lecturas, ningún `seconds > 60` ni `frames > fps`.
- Comparar métricas con `baselines/before_fix.json`.

### Fase 3 — Update gradient-motion-engine (consumer)

Branch en gradient-motion-engine: `fix/mtc-callback-thread-safety`. Coordinada con la rama de mtcreceiver.

#### 3.1 `MtcTickSource.h` — destructor explícito

```cpp
class MtcTickSource {
public:
    MtcTickSource() = default;
    ~MtcTickSource();  // ← ya NO `= default`; debe desregistrar callback

    // ... (sin cambios en setTickCallback / start / getMtcMs / isRunning)
};
```

#### 3.2 `MtcTickSource.cpp` — usar nueva API + destructor

```cpp
void MtcTickSource::setTickCallback(std::function<void(long)> cb) {
    if (cb) {
        // Adapta firma void(long) → void(long, bool); ignora isCompleteFrame por ahora
        MtcReceiver::setTickCallback([cb = std::move(cb)](long ms, bool /*isComplete*/) {
            cb(ms);
        });
    } else {
        MtcReceiver::setTickCallback({});
    }
}

MtcTickSource::~MtcTickSource() {
    // Garantía no-call-after-destruction: bloquea hasta que cualquier callback en vuelo retorne
    MtcReceiver::setTickCallback({});
    // receiver_ se destruye automáticamente (unique_ptr); cierra el puerto MIDI.
}
```

**Decisión abierta para Adrià**: si `gradient-motion-engine` quiere exponer `isCompleteFrame` al usuario del consumer, ampliar la firma de `MtcTickSource::setTickCallback` y propagar. Por defecto se ignora.

#### 3.3 Reescribir tests `tests/test_mtc_tick_source.cpp`

Los tests actuales asumen `MtcReceiver::onQuarterFrame(value)` directo. Con la nueva API hay que invocar el callback registrado de otra forma. Dos enfoques:

a) **Helper de testing** en mtcreceiver: añadir `static void invokeTickForTesting(long ms, bool complete)` envuelto en `#ifdef MTCRECV_TESTING`. Llama internamente a `tickCallback_` bajo lock. Tests del consumer lo usan para mockear ticks.

b) **Mock con MIDI sender real**: usar `libmtcmaster` para generar QFs reales. Más realista pero más lento; lo dejamos para tests de integración.

Recomendación: (a) para los unit tests existentes (Scenarios A, B, integración SC-001/002); (b) como complemento opcional en CI con label "integration".

#### 3.4 Bump submódulo

Coordinado con merge de PR1 en mtcreceiver. Apuntar `gradient-motion-engine/mtcreceiver` al SHA del fix. Build + tests.

### Fase 4 — Validación end-to-end

Antes de PR upstream:

1. Build completo: mtcreceiver (rama nuestra) → gradient-motion-engine (con submódulo bumpeado) → ejecutar daemon contra libmtcmaster.
2. Medir phase jitter real del callback en gradient-motion-engine bajo carga (varias curvas, OSC output activo).
3. **Verificar cuems-videocomposer**: build + smoke test del playback. Cambios para él:
   - `wasLastUpdateFullFrame` ahora atómico — accesos `bool fullFrameReceived = MtcReceiver::wasLastUpdateFullFrame;` y `MtcReceiver::wasLastUpdateFullFrame = false;` siguen compilando (atomic-to-bool conversion + atomic store implícito). No requiere cambio de código en el driver.
4. **Verificar cuems-dmxplayer**: build + smoke test. Impacto del PR1 en dmxplayer:
   - **No registra callback**, no afectado por la API del callback.
   - Sí afectado por el mutex en `isTimecodeActive()` / `estimatedCurrentHead()` (P3 fix). Adquisición de mutex cada poll OLA (10ms). Coste insignificante.
5. **cuems-audioplayer**: depende del SHA actual (verificar en Fase 0). Si está al día, bump y verificar. Si está atrasado, queda como deuda documentada.
6. Comparar métricas con `baselines/before_fix.json` y verificar mejora (0 ticks fantasma, 0 races TSan, jitter ≤ baseline).

### Fase 5 — PR2: Bugs preexistentes (separado)

Branch: `fix/mtcframe-conversions-and-fullframe-reset` en mtcreceiver.

- **P4**: `MtcFrame::fromSeconds` reescrito:
  ```cpp
  void MtcFrame::fromSeconds(long int s) {
      seconds = (int)(s % 60);
      minutes = (int)((s / 60) % 60);
      hours   = (int)((s / 3600) % 24);
      frames  = 0;  // s es entero, sin parte fraccional
  }
  ```
  Tests: `fromSeconds(3661)` → h=1, m=1, s=1.
- **P5**: `MtcFrame::msToFrames` corregido:
  ```cpp
  long int MtcFrame::msToFrames(long int ms) {
      return (long int)(ms * (getFps() / 1000.0));
  }
  ```
  Tests: `msToFrames(1000)` a 25fps → 25.
- **P6**: `decodeFullFrame` resetea estado QF al final:
  ```cpp
  quarterFrame = MtcFrame();
  direction = 0;
  qfCount = 0;
  lastQFlag = firstQFlag = false;
  lastDataByte = 0x00;
  ```
- **P8**: `exit()` en constructor → `throw std::runtime_error(...)`. Auditar callers para asegurar que capturan.

Antes de mergear PR2: grep `fromSeconds` / `msToFrames` en todos los repos stagesoft (ya hecho en Fase 0). Si nadie los usa (probable), riesgo bajo.

### Fase 6 — PR upstream + bump

1. PR1 a `stagesoft/mtcreceiver`: branch `fix/quarter-frame-callback-safety`. Adjuntar:
   - Resumen del análisis (este plan resumido).
   - Métricas before/after del harness (Fase 1 vs Fase 2.8).
   - Resultados TSan.
   - Lista de consumers verificados.
   - **Nota explícita de breaking change** + lista de consumers actualizados en PRs paralelos.
2. PR a `stagesoft/gradient-motion-engine`: branch `fix/mtc-callback-thread-safety`. Coordinado con merge de PR1.
3. Bump submódulo en cuems-videocomposer y cuems-dmxplayer cuando PR1 esté merged + tagged.
4. cuems-audioplayer: ticket separado para sincronizar el fork (si Fase 0 reveló SHA antiguo).
5. PR2 (bugs preexistentes) sigue cuando PR1 esté mergeado.

#### Versionado

mtcreceiver no tiene `project(... VERSION x.y.z)` en `CMakeLists.txt`. Aprovechar PR1 para añadir `project(mtcreceiver VERSION 2.0.0 LANGUAGES CXX)` y bumpear a 2.0.0 (major, por breaking change). Los consumers pueden checkear `find_package(mtcreceiver 2.0)` o pinear SHA. Se documenta en CHANGELOG.md (nuevo).

---

## Archivos críticos a modificar

### mtcreceiver (Fase 2, PR1)
- [`mtcreceiver.h`](src/cuems-videocomposer/src/mtcreceiver/mtcreceiver.h) — añadir `setTickCallback` y `TickCallback` typedef, eliminar `static onQuarterFrame`, atomizar `wasLastUpdateFullFrame`, declarar `callbackMutex_` / `tickCallback_` / `curFrameMutex_` / `activeStateMutex_`.
- [`mtcreceiver.cpp`](src/cuems-videocomposer/src/mtcreceiver/mtcreceiver.cpp) — refactor `decodeQuarterFrame` a single-fire-per-QF (sección 2.3), proteger `curFrame` y `decodeFullFrame` writes con mutex, atomizar `wasLastUpdateFullFrame`, mutex en `isTimecodeActive`/`estimatedCurrentHead`/`decodeFullFrame`, eliminar `cout` en MIDI thread, **definir los nuevos statics** en el bloque inicial.
- `mtcreceiver/CMakeLists.txt` — `project(... VERSION 2.0.0)`, opcionalmente `option(MTCRECV_VERBOSE_DIAG OFF)` y `option(MTCRECV_TESTING OFF)`.
- `mtcreceiver/tests/test_callback_harness.cpp` (nuevo, Fase 1).
- `mtcreceiver/tests/test_callback.cpp` (nuevo, Fase 2.8).
- `mtcreceiver/tests/baselines/before_fix.json` (nuevo, Fase 1).
- `mtcreceiver/CHANGELOG.md` (nuevo).

### mtcreceiver (Fase 5, PR2)
- [`mtcreceiver.cpp`](src/cuems-videocomposer/src/mtcreceiver/mtcreceiver.cpp) — fixes P4, P5, P6, P8.

### gradient-motion-engine (Fase 3)
- `src/time/MtcTickSource.h` — destructor explícito (no `= default`).
- `src/time/MtcTickSource.cpp` — adaptar a nueva API `MtcReceiver::setTickCallback`, implementar destructor que desregistra.
- `tests/test_mtc_tick_source.cpp` — reescribir Scenarios A, B y SC-001/002 para usar `MtcReceiver::invokeTickForTesting()` (Fase 3.3 opción a).
- `.gitmodules` (no cambia) y bump del submódulo `mtcreceiver`.

### cuems-videocomposer (Fase 4)
- Bump submódulo `mtcreceiver`. **No esperamos cambios en código** del driver — los accesos `bool x = MtcReceiver::wasLastUpdateFullFrame;` y `MtcReceiver::wasLastUpdateFullFrame = false;` siguen funcionando con `std::atomic<bool>`.

### cuems-dmxplayer (Fase 4)
- Bump submódulo `mtcreceiver`. No requiere cambios en código.

### cuems-audioplayer (Fase 4)
- Si el SHA actual es compatible con el bump → bump. Si no → ticket de deuda.

---

## Funciones / utilidades a reutilizar

- **`mtcHead` polling** ([atomic load](src/cuems-videocomposer/src/cuems_videocomposer/cpp/sync/MIDISyncSource.cpp#L202)): patrón ya usado por todos los consumers, no se toca.
- **`AsyncVideoLoader` callback+queue pattern** ([videocomposer/src/cuems_videocomposer/cpp/input/AsyncVideoLoader.h](src/cuems-videocomposer/src/cuems_videocomposer/cpp/input/AsyncVideoLoader.h)): plantilla para cuando el consumer del callback necesite desacoplar trabajo pesado del MIDI thread (request queue + result queue + pollCompleted en main thread).
- **`libmtcmaster` Python sender** ([/home/stagelab/src/libmtcmaster/python/mtcsender.py](/home/stagelab/src/libmtcmaster/python/mtcsender.py)): generador de MTC para tests de integración.

---

## Verificación end-to-end

### Tests automatizados

```bash
# Fase 1: harness baseline (rama actual con PR #3)
cd /home/stagelab/src/cuems-videocomposer/src/mtcreceiver
git checkout origin/main
mkdir -p build && cd build
cmake -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" ..
make test_callback_harness
./tests/test_callback_harness > tests/baselines/before_fix.json

# Fase 2.8: tests del callback fix (TSan habilitado, rama nueva)
git checkout fix/quarter-frame-callback-safety
make test_callback
ctest -R test_callback --output-on-failure

# Fase 4: build full stack
cd /home/stagelab/src/gradient-motion-engine
git checkout fix/mtc-callback-thread-safety
git submodule update --init --recursive
mkdir -p build && cd build
cmake -DBUILD_DAEMON=ON -DBUILD_TESTS=ON ..
make -j$(nproc)
ctest --output-on-failure

# videocomposer smoke test (con mtcreceiver bumpeado)
cd /home/stagelab/src/cuems-videocomposer
# (en una rama de prueba, bumpear submódulo)
cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)
# Smoke test: lanzar contra MTC sender y verificar playback sincronizado
```

### Validación manual

1. Lanzar `gradient-motiond` registrando una curva de fundido (Bezier típica).
2. Lanzar `libmtcmaster/python/mtcsender.py` enviando MTC a 25fps con un seek a la mitad.
3. Capturar OSC output y validar que la curva avanza linealmente con el MTC, sin glitches en el seek.
4. Inspeccionar logs: 0 cout en MIDI thread, 0 warnings de TSan.
5. Repetir con `MTCRECV_VERBOSE_DIAG=1` para diagnósticos de bringup.

### Criterios de éxito

- 0 ticks fantasma (guard `!reset`).
- 0 reports de TSan sobre `wasLastUpdateFullFrame`, `curFrame`, `timecodeRunWeight`, callback registration.
- Phase jitter del consumer ≤ 1ms (medido end-to-end en gradient-motion-engine).
- Cadencia 200Hz mantenida; QF #8 dispara **una sola vez** con `isCompleteFrame=true`.
- `setTickCallback({})` en destructor de `MtcTickSource` garantiza no-call-after-unregister (TSan limpio + assertion en test).
- Build + smoke test OK en cuems-videocomposer y cuems-dmxplayer con submódulo bumpeado.
- `MtcFrame::msToFrames(1000)` a 25fps devuelve 25 (PR2).
- `MtcFrame::fromSeconds(3661)` → h=1, m=1, s=1 (PR2).

---

## Riesgos y mitigaciones

| Riesgo | Mitigación |
|---|---|
| Adrià rechaza el cambio de doble-fire a single-fire-per-QF | Sync explícito en Fase 0. Datos del harness en mano. La API single-fire sigue dando 200Hz que es lo que pide la spec. |
| Tests del consumer hay que reescribirlos (Scenarios A, B, SC-001/002) | Fase 3.3 opción (a): añadir `invokeTickForTesting()` en mtcreceiver bajo `#ifdef MTCRECV_TESTING`. Trivial de mantener. |
| Audioplayer SHA muy antiguo | Verificación explícita en Fase 0; si requiere migración profunda, ticket separado y NO bumpeamos. |
| Mutex en MIDI callback bajo carga RT (priority inversion) | Documentar contrato (handler <100µs, no llamar a setTickCallback desde handler). Registro ocurre en init, no en hot path. |
| `consumeFullFrameFlag()` originalmente propuesto rompía el patrón split read/reset del videocomposer driver | **Descartado tras verificación de código real**. Solo atomizar `wasLastUpdateFullFrame`; el patrón actual del driver sigue funcionando. |
| Spec del consumer tiene inconsistencia 1200 callbacks = 6s vs 60s | Resolver con Adrià en Fase 0. Tests sintéticos cuadran con 6s, spec dice 60s — aclarar antes de bloquear el fix por un test que ya admite ser ambiguo. |
| Otros consumers ocultos en stagesoft que usen `MtcReceiver::onQuarterFrame` | Grep en Fase 0 sobre todos los repos stagesoft. Si aparecen, coordinar update en paralelo. |
| `MtcFrame::msToFrames`/`fromSeconds` tienen callers que dependen del bug | Grep en Fase 0. Probable que nadie los use porque devuelven valores absurdos hoy. Si aparece dependencia, fix coordinado. |
| `std::atomic<double>` no lock-free en C++17 | Por eso usamos mutex (`activeStateMutex_`) para `timecodeRunWeight`. No es hot path. |
| Static initialization order para `callbackMutex_` / `tickCallback_` / `curFrameMutex_` | Definidos juntos en `mtcreceiver.cpp` con los otros statics; el estándar garantiza orden de inicialización dentro de la misma TU. Sin riesgo. |
| Cycle chicken-and-egg en submódulos (gradient-motion-engine necesita SHA de mtcreceiver que aún no existe) | Fase 0: trabajar en forks locales con remote `local-fix`; pushear a stagesoft solo cuando el ciclo end-to-end pase. |

---

## Cosas explícitamente fuera de scope

- Migrar audioplayer al mtcreceiver nuevo (deuda separada si SHA antiguo).
- Detach de `checkerThread` (race benigno, fix requiere cambiar lifecycle, mejor PR aparte).
- Convertir `MtcReceiver` a no-static / multi-instance (FR-009 lo permite hoy).
- Refactor del logging (CuemsLogger en MIDI thread es otro tema).
- Diseño del backpressure / queue del consumer en gradient-motion-engine (responsabilidad del consumer; la API del receiver solo expone el tick).
- Resolver completamente la inconsistencia 1200 vs 12000 en spec del consumer (lo aclaramos con Adrià en Fase 0; no lo arreglamos en estos PRs).

---

## Cambios v1 → v2 (resumen para auditoría)

Tras review crítico de Sonnet, cambios sustanciales:

1. **Shim `OnQuarterFrameShim` descartado**. Era defectuoso (`operator()(long ms)` desregistraba el callback). Solución: reescribir tests del consumer en Fase 3.3.
2. **Refactor a single-fire-per-QF**. Elimina doble fire en QF #8, double-lock, ticks fantasma y necesidad de dedupe en consumer.
3. **`consumeFullFrameFlag()` descartado** tras verificar el patrón split read/reset del videocomposer driver. Solo atomizar `wasLastUpdateFullFrame`.
4. **`timecodeRunWeight` et al. clarificadas como instance members** (no statics). Mutex de instancia (`activeStateMutex_`).
5. **Destructor de `MtcTickSource` explicitado** en archivos a modificar.
6. **Definición de los nuevos statics en `mtcreceiver.cpp` añadida** explícitamente (sección 2.2).
7. **Inconsistencia 1200 callbacks = 6s vs 60s** marcada para sync con Adrià en Fase 0.
8. **Verificación de SHA del audioplayer** añadida a Fase 0.
9. **Grep cross-repo de la API actual** añadido a Fase 0.
10. **Priority inversion documentada** en sección 2.7.
11. **Versionado** (`project VERSION 2.0.0`, CHANGELOG.md) añadido a Fase 6.
12. **Impacto en dmxplayer corregido**: no usa el callback, solo se ve afectado por el mutex P3.
