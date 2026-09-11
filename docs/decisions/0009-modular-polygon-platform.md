# ADR-0009: модульная полигональная платформа и вывод legacy desktop

- Статус: принято для архитектурного этапа A1-A8
- Дата: 2026-09-11

## Контекст

После M5 в проекте сосуществуют два desktop/domain-контура и один крупный solver
target. Полигональный environment уже является основой baseline, learning API,
Python dataset и новой Qt-вкладки, но IO, search, geometry и ML находятся в одной
библиотеке. Общие статусы и metadata формально принадлежат grid contract.

M6.2 заморозит полигональную обучаемую политику, а M6.3 добавит второй desktop
backend. Если начать их поверх текущих границ, временные C++ зависимости станут
частью model deployment contract.

## Решение

Развивать проект как модульный монолит с полигональным Core. Выделить общие
contracts, NestingCore, Search, Learning, Json и Application ports. Конкретные
baseline, filesystem, threading, Qt, pybind11 и ONNX Runtime являются adapters.

Сохранить wire formats и старые C++ entry points через forwarding headers и
compatibility target. Не вводить универсальную runtime-иерархию геометрий:
grid и polygon могут использовать общий search runtime, но сохраняют собственные
state/candidate adapters.

Grid solver остаётся research compatibility-компонентом для воспроизводимости
M1-M3. Клеточный desktop `Board/Figure` изолируется feature flag и выводится после
полигонального функционального паритета. Его внутреннюю event-архитектуру не
переписывать перед удалением.

Архитектурные этапы A1-A5 выполняются до M6.2. Application ports и вынос
execution runner завершаются до M6.3. Любой neural/hybrid backend обязан выбирать
действия C++ environment и проходить тот же exact validator.

Первый шаг решения реализуется в A2 без изменения namespace и wire formats:

- `AIPackaging_NestingCore` владеет общими контрактами и строковыми
  преобразованиями status/family;
- `AIPackaging_SolverImpl` временно владеет восемью существующими grid/polygon
  translation units и публично зависит только от Core;
- `AIPackaging_Solver` становится INTERFACE-совместимостью для старых flat
  headers и App/GUI;
- `SolverKind` и `SolverConfig` принадлежат отдельному search contract, но общий
  execution-control откладывается до A4;
- legacy targets создаются только для desktop либо при явном
  `BUILD_LEGACY_TESTS=ON`.

## Рассмотренные варианты

### Сохранить единый `AIPackaging_Solver`

Отклонено: минимальная стоимость сейчас приводит к связанным изменениям geometry,
dataset, bindings и deployment на каждом следующем этапе.

### Полностью переписать проект вокруг новой модели

Отклонено: рабочие алгоритмы и 38/38 + 31/31 тестов уже дают безопасную базу для
извлечения модулей. Полная перепись создаст непроверяемое изменение геометрии.

### Сделать отдельные сервисы для solver и обучения

Отклонено: для одного разработчика IPC, deployment и schema coordination не дают
компенсирующей пользы. Нужны compile-time границы, а не распределённая система.

## Последствия

Положительные:

- M6.2 обучается поверх стабильного learning/Core contract;
- M6.3 добавляет ONNX backend без изменений Qt presentation;
- headless и Python сборки не зависят от legacy desktop;
- wire compatibility контролируется отдельно от внутренней структуры;
- legacy можно удалить без потери grid research artifacts.

Отрицательные:

- перед обучением появляется отдельный архитектурный этап;
- временно поддерживаются forwarding headers и compatibility target;
- CMake targets и тестовые executables станут многочисленнее;
- побайтовую и геометрическую parity нужно проверять после каждого перемещения.

Риски ограничиваются малыми этапами, feature flags и запретом совмещать source
move с изменением алгоритма.
