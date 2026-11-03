# Refactoring plan

Audit date: 2026-11-03. Source baseline: `39e2157` (APK 0.1.10).
This document is a plan; application code has not been refactored by this audit.
Evidence comes from source inspection, production/test reference searches and
`ctest --preset macos-debug -N` (258 discovered tests). The suite was not rerun
for this documentation-only audit; discovery is not a passing test result.

## Recommendation

Refactor incrementally, preserving current observable behavior and storage formats.
Keep `AppController` as the QML facade while moving its responsibilities into
small, owned services. Introduce shared bank machinery only where contracts are
actually identical. A full rewrite might yield a more uniform design, but would
require revalidating authentication, persistence and platform behavior together;
its risk is disproportionate to the duplication identified here.

Success means that each responsibility has one owner, each connection state has
one authoritative representation, unused production surfaces are removed or
explicitly classified, and existing data and live-bank behavior remain intact.
Line-count reduction alone is not an acceptance criterion.

## Findings and evidence

| Priority | Finding | Evidence and proposed treatment |
| --- | --- | --- |
| P0 | Legacy credential migration can delete the old secret after a failed replacement write | [AppController.cpp](../src/app/AppController.cpp), lines 1455–1494: `put()` determines `tbank_connected_`, but `remove(legacyStateKey)` and settings cleanup are not conditional on successful migration. Add failure coverage and fix separately from structural changes. This is a code-level risk; no user data loss was observed. |
| P1 | Controller combines too many responsibilities | [AppController.cpp](../src/app/AppController.cpp) is 4,187 lines: cashback 537–849, history persistence 851–994, crypto 996–1153, initialization 1235–1606, model projection 2744–2964, plugins 2966–3422, bank commands 3424–4100. Extract ownership along these boundaries. |
| P1 | Repeated cashback result lifecycle | VTB/Alfa/Ozon start/stop paths repeat cancellation, snapshot merge, persistence, timestamps and signals. [cashback_sync.cppm](../src/app/cashback_sync.cppm) already shares browser polling. Consolidate the controller lifecycle, keeping readers and T-Bank's credential/context transport separate. |
| P1 | Connection state has several representations | Controller booleans, coordinator pointers, `connection_results`, saved flags, `connection_states` and `ConnectionModel` all contribute to status. Keep saved credential presence separate from current access; derive UI maps/roles from one state record. |
| P1 | Repeated scheduling and command guards | Four timer/schedule pairs at controller 2098–2173; Alfa/VTB/Ozon wrappers at 3745–4100. Ozon stops cashback before some command guards, unlike Alfa/VTB. Characterize rejected-command behavior before consolidation. |
| P1 | Large connection page repeats bank dispatch | [ConnectionsPage.qml](../ui/qml/pages/ConnectionsPage.qml) is 1,046 lines, mixing token entry, retail login, plugin settings, table and Android login. Four-bank property selection repeats at 88–127 and action dispatch at 129–192. |
| P1 | Documentation describes old behavior | [architecture.md](architecture.md) omits Ozon from the runtime table, describes identifiers as holding labels, says automatic cashback remains in memory, and documents balance-history schema v2. [balance_history.cpp](../src/storage/balance_history.cpp) supports schema v4 and persistent cashback. [README.md](../README.md) also contains outdated retention claims. |
| P2 | Unused old chart surface | [LineChart.qml](../ui/qml/components/LineChart.qml) is registered in CMake but never instantiated. `netWorthSeries` has no QML/test consumer; its property, getter, backing vector and demo/reset writes are removal candidates. |
| P2 | Initial status calculations are overwritten | API/T-Invest row status ternaries at controller 1965–2005 are overwritten by the common state projection at 2047–2063. Remove the redundant initial calculations. |
| P2 | Version information is duplicated | Root CMake, Conan recipe and runtime use 0.1.0; Android CMake uses code 11/name 0.1.10; shell script repeats the APK filename. Establish one explicit version source, retaining the distinct monotonic Android code. |
| P2 | Test infrastructure repeats and some UI tests check spelling | Settings isolation, fake replies and wait loops recur across app tests. [ui_contract_test.cpp](../tests/app/ui_contract_test.cpp) checks many source substrings. [tests/ui/run.sh](../tests/ui/run.sh) rewrites copied QML imports to load tests. Extract modest test helpers and make the QML module directly testable. |
| P2 | Possible unused direct dependency | CMake requests/links `Qt6::Concurrent`, but reference searches found no `QtConcurrent`, `QFuture` or `QPromise` use in source/tests. Confirm with clean builds before removing. No package-size saving has been measured. |

## Scope decisions: unreachable does not mean obsolete

1. **Standalone pages:** Accounts, Transactions, Imports and Categories are bundled
   but absent from the seven-page navigation/StackLayout in
   [Main.qml](../ui/qml/Main.qml). Default recommendation: remove redundant Accounts,
   Transactions and Categories page files only after confirming the intended
   seven-page navigation; retain the live account/transaction/category models
   used by the dashboard. For Imports, choose either an accessible statement-preview
   action or retirement of the unreachable page and misleading import copy.
   Restoring a route changes product behavior and should be a separate change.
2. **Ledger/import subsystem:** `LedgerRepository` and `ImportSession` have storage
   tests but no app/connector callers. They are implemented but unintegrated.
   Decide whether persistent transaction import is still planned. Until then,
   retain this subsystem and document its status; do not conflate it with the
   active `BalanceHistoryRepository` or remove migration compatibility.
3. **Core analytics:** `Categorizer` and `calculate_xirr` have tests but no current
   app call sites found. The app uses normalized connector models and its own
   reporting projection. Decide whether to integrate or retire these capabilities
   before attempting a broad merger of core and connector types. Inventory actual
   money/decimal users first; exact financial arithmetic remains required.
4. **Keep:** manual cashback fallback, demo mode, official API connectors,
   ZenPlugins, credential migrations, and bank-specific diagnostic codes have
   active uses or compatibility obligations. Their deletion is not justified by
   this audit. Generic history support remains needed by business/API connectors.
5. **Keep tracked Android source:** manifest, trust configuration, native bridge,
   tests and packaging are application code. Outputs, local keys and caches are
   already ignored; moving all of `android/` outside the repository would lose
   reproducible application behavior.

## Ordered implementation plan

Each numbered stage should produce small, independently reviewable commits.
Structural moves and behavior changes must be separate commits.

1. **Protect the baseline and migration (P0; prerequisite).**
   Run the current native/QML/Python and Android checks, recording platform and
   test counts. Add migration cases for replacement-write failure, unavailable
   credentials, malformed legacy state and successful retry. Preserve old secrets
   and settings until the replacement is durably stored; keep failed migrations
   retryable. Pin rejected login/refresh command behavior, especially Ozon cashback.
   **Gate:** failure/retry fixtures pass, no live credentials are accessed, existing
   restart/disconnect tests stay green. Fix this before moving initialization.

2. **Remove confirmed redundant surfaces and align docs (P2, low risk).**
   Remove the unused chart path and overwritten status initializers. Update
   architecture/retention documentation from current code. Investigate removing
   the direct Concurrent dependency in a separate commit. Resolve the page/import
   decisions above before deleting those features.
   **Gate:** all references accounted for; clean macOS and Android builds, QML
   lint and relevant dashboard/navigation tests pass. Record removed tests with
   their retired feature rather than silently reducing coverage.

3. **Make tests resilient to extraction (P1, moderate risk).**
   Extract shared QSettings isolation, bounded waits and fake network replies into
   `tests/support`; keep bank-specific payloads and timing semantics explicit.
   Add behavior tests for connection state transitions, privacy cancellation and
   per-bank refresh. Replace source-string layout checks with QML interaction and
   geometry tests where they assert behavior. Expose QML as a directly importable
   module so the test runner no longer edits source copies. A small CMake helper
   may consolidate repeated test setup while retaining GUI/platform differences.
   **Gate:** tests pass individually and together without leaked settings;
   inventory changes are explained; tests load the same QML as the app.

4. **Extract controller domains without changing the QML API (P1).**
   In separate commits, move cashback ownership, financial model projection,
   history persistence/restoration, wallet orchestration, and plugin orchestration
   into focused services. Move their state with their behavior; avoid creating
   files that still manipulate a shared controller-private structure. Make startup
   sequencing explicit. Keep signal timing and existing commands initially stable.
   **Gate:** existing domain tests pass unchanged where possible; exact amounts,
   saved timestamps, partial-month merges, signals, cancellation and restoration
   match the baseline. All platforms still build after module moves.

5. **Unify connection state and scheduling (P1, higher risk).**
   Introduce one record per connection for saved access material, interactive login,
   background refresh and last validated result. Derive list and dialog state from
   it. Extract the six-hour scheduling/ten-second retry policy with explicit bank
   actions. Use adapters over existing coordinators; retain bank-specific origins,
   parsing, authentication restoration and TLS rules.
   **Gate:** every manual start order works, background banks overlap, only one
   interactive login runs, lock cancels work, failed reads retain prior data and
   sessions, and late callbacks cannot publish into a new request.

6. **Split connection UI and retire duplicated dispatch (P1).**
   Expose a selected-connection presentation object to QML; migrate one bank at a
   time. Extract retail-login content, token editor, plugin editor and connection
   list. Remove old bank-specific QML properties only after callers migrate.
   **Gate:** real click tests and phone/desktop screenshots pass; Android native
   bank windows remain visible; closing/reopening dialogs preserves deferred
   WebView cleanup and clears sensitive form fields correctly.

7. **Consolidate release metadata and verification (P2).**
   Define product/display version and Android code once; derive runtime metadata,
   packaging and output names. Document one verification matrix for native, QML,
   Python, Android instrumentation and separate live-bank checks. Extend CI where
   practical; Android CI is not a prerequisite for earlier stages.
   **Gate:** package metadata is consistent, signatures match existing releases,
   installation over the previous APK preserves data, and supported desktop
   builds/package checks pass. Do not rename package IDs, credential keys or the
   data directory during this refactor.

Stages 2 and the test-helper portion of 3 can proceed independently after stage 1.
Stage 4 precedes 5; stage 5 precedes retirement of the old API in 6. Stage 7 is
largely independent. Deleting unintegrated ledger/analytics functionality remains
a separate scope decision, not a prerequisite for simplifying the active app.

## Verification and release boundaries

- Use the existing `macos-debug` build/CTest/QML-lint commands from AGENTS.md;
  run Python script tests separately as CI currently does. Native WebView tests
  need a real macOS display context; sandbox display failures are not app defects.
- Keep exact integer/decimal money, bounded external input, exact host allowlists,
  TLS validation, read-only methods and Qt-thread callback dispatch unchanged.
- Preserve saved balance/cashback observations on refresh failure, current/next
  month boundaries, history deselection, portfolio ticker fallback and sorting.
- Exercise database upgrades from supported versions using synthetic copies;
  never rewrite or delete a user's store to simplify the architecture.
- For structural UI/platform changes, verify phone/desktop layouts and the actual
  Android native WebView bridge. Automated fixtures do not prove live-bank login.
  Schedule user-run smoke checks for touched bank adapters before release.
- Keep each stage revertible through its commits. Any storage format change needs
  its own migration and rollback design; none is required by this plan.

## Implementation progress

Implemented so far:
- Credential migration preserves retryable state on failed replacement writes,
  unavailable backends and malformed input; successful retry/cleanup is tested.
- Removed unused LineChart/netWorthSeries and overwritten status initialization.
- Removed the unused direct Qt Concurrent dependency; desktop and Android build.
- Corrected retention/schema/holding-label documentation.
- Centralized product/Android/Conan versions in version.json and added
  scripts/verify.sh with docs/verification.md.

Further implementation (current refactoring pass):
- Shared isolated QSettings and bounded wait support now serves the cashback,
  Ozon and retail WebView suites, preserving each suite's timing semantics.
- Automatic cashback snapshots, last-success times and status now belong to
  CashbackState. Bank readers, credentials and encrypted persistence keep their
  established boundaries; manual selections remain separate.
- ConnectionPresentation consumes a typed ConnectionState; refresh scheduling
  uses one policy with the existing six-hour cadence and ten-second minimum.
  Per-bank coordinators still own activity: this is not yet a unified state store.
- ConnectionList and RetailConnection separate the list and selected bank
  presentation/commands. Connection page size is now 818 lines; phone/desktop
  before/after renders are identical. Adapter behavior tests are part of CTest's
  QML test command.

Still pending: directly importable QML test module without source adaptation;
remaining controller domain ownership (history, crypto, plugin and financial
projection); unified activity ownership; token/plugin/login content components.
Unintegrated product capabilities and unreachable pages are retained pending the
scope decisions above. This record does not mark the full plan complete.
