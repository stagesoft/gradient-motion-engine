Resumen para Adrià — v2

Asunto: mtcreceiver — cambios que vamos a aplicar encima de tu PR#3

Hola Adrià,

Estamos preparando un fix encima de tu PR#3 (onQuarterFrame + portIndex) para (a) cerrar las races que quedaron abiertas y (b) integrar gradient-motion-engine como primer consumer real. Antes de abrir PR upstream te paso las decisiones de diseño que tocan tu trabajo, por si ves algún problema.

1. Cadencia del callback: single-fire-per-QF con flag (cambio respecto a tu diseño)

Tu PR dispara el callback dos veces en la QF #8: Site 1 (extrapolado, por cada QF) y Site 2 (posición real, cuando termina el decode). En la spec de gradient-motion-engine lo documentas como intencional ("fires after both position-update sites").

Vamos a colapsarlo a una sola invocación por QF con un flag bool isCompleteFrame:

isCompleteFrame=false en QFs 1-7 (extrapolado).
isCompleteFrame=true en QF 8 (posición autoritativa tras decodificar el frame completo).
En secuencias inválidas (reset==true): no dispara.
Firma nueva: void(long mtcHeadMs, bool isCompleteFrame).

Razones:

Misma cadencia 200 Hz que pedía la spec.
El consumer ya no tiene que deduplicar.
Una sola adquisición del mutex por QF (evita la ventana entre Site 1 y Site 2 donde otro thread podría swapear el callback entre las dos llamadas consecutivas).
Se elimina el tick fantasma en secuencias inválidas.
El flag preserva la distinción extrapolado/real por si el consumer la quiere usar (en gradient-motion-engine la ignoramos por defecto).
Si tenías un caso concreto donde necesitabas ver las dos fires separadas, dínoslo y lo revisamos — pero hasta donde vemos, la información se recupera entera con el flag.

2. Thread-safety del registro del callback

static std::function<void(long)> onQuarterFrame como variable pública es UB cuando alguien la asigna desde un thread mientras el MIDI thread la está leyendo. MtcTickSource::setTickCallback en gradient-motion-engine lo hace exactamente así.

Vamos a sustituir la variable pública por static void setTickCallback(TickCallback) con mutex interno. El destructor de MtcTickSource llamará setTickCallback({}) para garantizar no-call-after-unregister. Es un breaking change menor (solo un consumer lo usa hoy, coordinamos el update en la misma PR).

3. Inconsistencia en tu test — la resolvemos por 6 s / 1200 ticks

En specs/003-mtc-tick-source/ los SC-001/002 hablan de 60 s, pero tests/test_mtc_tick_source.cpp::testSyntheticMtcStream itera 1200 callbacks (= 6 s a 200 Hz). Asumimos que el número correcto es 6 s / 1200 ticks (lo que el código ya hace) y que la SC tiene un cero de más — un unit test de 60 s sería 10× más lento sin añadir cobertura real. Corregimos la SC para que cuadre, no el test. Si querías 60 s de verdad, dínoslo y lo invertimos.

4. Además: bugs preexistentes (PR separada)

En paralelo vamos a arreglar P1-P8 que llevan tiempo ahí y no tocan tu código nuevo: atomizar wasLastUpdateFullFrame, mutex en curFrame y timecodeRunWeight/timecodeStartTimestamp/timecodeTimestamp, MtcFrame::fromSeconds (integer division bug → minutos/horas siempre 0), MtcFrame::msToFrames (conversión invertida → msToFrames(1000) a 25fps devuelve 40000 en vez de 25), reset de estado en decodeFullFrame, quitar std::cout del MIDI thread, exit() en constructor → excepción.

Plan de PRs

PR1 a stagesoft/mtcreceiver fix/quarter-frame-callback-safety: single-fire + thread-safety + atomics. Breaking change de API, versioning a 2.0.0.
PR a stagesoft/gradient-motion-engine fix/mtc-callback-thread-safety: adaptar MtcTickSource + tests + bump submódulo. Coordinada con PR1.
PR2 a stagesoft/mtcreceiver fix/mtcframe-conversions-and-fullframe-reset: bugs preexistentes (no-breaking).
Cuando PR1 esté mergeado, bumpeamos el submódulo en videocomposer, dmxplayer y audioplayer (audioplayer ya está verificado que compila sin cambios con tu PR#3).

¿Ves algún problema con los dos cambios de diseño (single-fire, API de registro)?

Gracias,
Ion