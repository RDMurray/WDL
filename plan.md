# Direct Lazy AT-SPI For SWELL

## Summary

- Start the direct AT-SPI implementation from `main`, not from the previous AccessKit branch.
- Keep the old AccessKit work as historical reference on the `accesskit` branch.
- Do not touch the sibling `../accesskit` checkout.
- Remove the Rust AccessKit shim model entirely for this branch. The new provider should emit AT-SPI objects directly from live SWELL state on demand.

## Key Changes

- Add a C++ AT-SPI provider for the Linux GDK backend, compiled behind `SWELL_ATSPI` and disabled with `NOATSPI=1`.
- Use GIO/GDBus directly. Do not link AccessKit or depend on the sibling AccessKit checkout.
- Export a SWELL application root and per-object AT-SPI object paths. Method calls should parse object paths, resolve live SWELL state, and compute properties lazily.
- Keep only provider/runtime state: D-Bus connections, object registrations, live top-level windows, focus/event state, active menu serials, and small accessibility state that SWELL itself does not already expose.
- Port useful lessons from the AccessKit branch under AT-SPI-neutral names: focus lookup, edit selection helpers, list/listview/tree/tab state readers, menu tracking/action helpers, and GDK key forwarding.

## Implementation Phases

- Provider skeleton: connect to `org.a11y.Bus`, connect to the AT-SPI bus, register a lazy GDBus subtree, and expose top-level SWELL windows.
- Core object model: implement `Accessible`, `Application`, `Component`, `Action`, and `Value` for windows, static text, buttons, checkboxes, radios, sliders, progress bars, and basic focus/action routing.
- Text path: implement single-line edit and editable combo support with `Text` and `EditableText`, including caret/selection events.
- Menus and combos: expose menu bars, active popup menus, combo popup options, checked/radio state, focus, selection, and activation.
- Collections: expose listbox/listview/tree/tab/report-grid children lazily, including visible ranges for large or owner-data collections.
- Eventing: emit focused/state/name/value/text/selection/children events from SWELL mutation hooks and forward GDK key events to AT-SPI key watchers where needed.
- Cache support: add `/org/a11y/atspi/cache` only after basic Orca navigation works, and keep it a transient reply generator rather than the source of truth.

## Test Plan

- Build SWELL with `make -C WDL/swell -j2`.
- Build the sample app with `make -C WDL/swell/sample_project`.
- Run `git diff --check`.
- Confirm no new references to AccessKit, `SWELL_ACCESSKIT`, `NOACCESSKIT`, or `rust/accesskit_shim` remain in the implementation.
- Use AT-SPI inspection tools to confirm the sample app appears on the bus and that lazy object paths return sane roles, names, parents, children, states, extents, and actions.
- Manually verify Orca focus traversal, edit typing/caret movement/deletion, button/check/radio activation, menus, combo dropdowns, lists, trees, tabs, and report-list row/cell focus.

## Assumptions And Risks

- “Lazy” means no persistent full accessibility tree, while still satisfying AT-SPI parent/child/object-reference contracts.
- Initial implementation can be correctness-first and cache-light; performance tuning can follow once Orca can navigate the basic object model.
- Object-path resolution must never dereference stale SWELL pointers. Destroyed windows, closed menus, and invalid synthetic row/cell paths should return defunct or safe errors.
- AT-SPI is D-Bus based and chatty; a small amount of runtime state is acceptable when it prevents stale focus, stale menu, or duplicate event behavior.
