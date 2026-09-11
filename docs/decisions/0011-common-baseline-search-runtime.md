# ADR-0011: единый runtime baseline-поиска

Статус: принято.

## Контекст

После A3 grid- и polygon-поиск находились в одном target, но независимо
реализовывали ordering, random permutations, beam, timeout, метрики и metadata.
Только polygon API поддерживал progress и cooperative cancellation. Такое
дублирование затрудняло добавление новых baseline и создавало риск различий в
семантике остановки и диагностических счётчиков.

Геометрия двух сред при этом принципиально различается: grid отдельно ранжирует
left-bottom кандидаты, а polygon получает уже отсортированный динамический
NFP-каталог. Их состояния, objective и обработка ограничения каталога также не
должны становиться общими типами.

## Решение

- `SearchContracts` владеет общими `SearchProgressStage`, `SearchProgress` и
  `SearchExecutionControl`.
- `Search` содержит закрытый `SearchRuntime`, общий ordering и lifecycle
  ordered/random/beam.
- Grid и polygon подключаются к lifecycle небольшими compile-time адаптерами,
  которые сохраняют состояния, геометрию, выбор кандидата, comparator и
  независимую валидацию своей предметной области.
- Runtime не создаёт потоки. Cancellation и progress callback выполняются
  синхронно в потоке вызывающего кода; их исключения не перехватываются.
- Отмена проверяется только на безопасных границах до следующей мутации и имеет
  приоритет над timeout, если обе причины наблюдаются одновременно.
- Отмена остаётся признаком execution result и не добавляется в `SolveStatus`
  или wire-форматы. Timeout и deterministic budget сохраняют прежние статусы.
- Монотонные часы внедряются только во внутренний runtime: production использует
  `steady_clock`, а unit tests управляют временем без ожидания.
- Прежние polygon-названия progress/control сохраняются aliases. Grid получает
  симметричный `runGridProblem`; старые `solve*` остаются фасадами.

## Последствия

Алгоритмы обеих сред используют один механизм seed, остановки, progress,
метрик и metadata, но runtime не знает о клетках, NFP или raster objective.
Wire-форматы и Python API не меняются. Новые baseline смогут переиспользовать
execution lifecycle, не копируя инфраструктуру.

Нулевая стоимость абстракции достигается шаблонным внутренним lifecycle. Его
требования к адаптеру остаются закрытой деталью `AIPackaging_Search` и не
становятся новым публичным framework API.
