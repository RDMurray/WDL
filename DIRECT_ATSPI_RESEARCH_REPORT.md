# Direct AT-SPI vs. AccessKit for SWELL

## Executive summary

The direct AT-SPI branch is technically viable, but it is not the stronger path to usable SWELL accessibility.

Nothing in AccessKit is doing impossible magic: a direct implementation could eventually reach equivalent behavior and performance. The problem is that reaching parity would require SWELL to reimplement much of what AccessKit already provides: a semantic accessibility tree, stable synthetic nodes, filtered platform exposure, dirty-state coalescing, tree diffing, event synthesis, active-descendant behavior, relation modeling, table semantics, and mature text handling.

The observed performance gap is therefore architectural rather than linguistic or incidental. The direct branch currently exposes AT-SPI by answering requests from live SWELL state on demand. The AccessKit branch snapshots SWELL into a semantic tree and lets AccessKit's Unix accessibility runtime own the difficult middle layer between widgets and AT-SPI. That middle layer is the advantage.

**Recommendation:** preserve this branch as a research artifact, but use the AccessKit branch as the production path for Linux accessibility support in SWELL.

## Scope of this investigation

This report compares:

- the direct AT-SPI implementation on this branch (`WDL/swell/swell-atspi-generic.cpp`)
- the AccessKit implementation in the sibling worktree `../WDL-ak`
- the AccessKit runtime sources in the sibling checkout `../accesskit`

The question was not merely whether the direct implementation works, but whether it can realistically be brought up to the AccessKit branch's present level without duplicating so much infrastructure that the direct path loses its rationale.

## The two branches solve different problems

### Direct AT-SPI branch

The direct branch intentionally avoids a persistent accessibility tree. Its design, documented in `plan.md`, is to:

- emit AT-SPI objects directly from live SWELL state
- resolve object paths lazily
- compute roles, states, names, children, and interfaces on demand
- keep only narrow runtime state such as D-Bus handles, object registrations, focus state, menu serials, and a small amount of previous-value bookkeeping

Its effective flow is:

```text
AT-SPI request
  -> parse object path
  -> resolve HWND/menu identity
  -> inspect live SWELL state
  -> compute response immediately
```

That design is elegant for bootstrapping and easy to reason about while the exported model is small.

### AccessKit branch

The AccessKit branch uses SWELL to build a semantic snapshot, then delegates platform behavior to AccessKit:

```text
SWELL live state
  -> semantic snapshot
  -> AccessKit TreeUpdate
  -> AccessKit consumer/runtime tree
  -> AT-SPI objects, interfaces, and events
```

The SWELL-side implementation in `../WDL-ak/WDL/swell/swell-accesskit-generic.cpp` builds nodes with roles, relations, text data, active descendants, collection metadata, table metadata, bounds, actions, and focus. The Rust shim converts that snapshot to `TreeUpdate`. AccessKit then owns the difficult platform layer:

- filtered accessibility tree semantics
- stable platform nodes
- interface registration
- old/new diffing
- event generation
- focus transitions
- active-descendant propagation
- text-change handling
- selection-change handling
- table/table-cell exposure
- asynchronous D-Bus service behavior

That is the essential difference between the branches.

## Current capability gap

### What the direct branch currently exports

The direct branch currently provides:

- HWND-backed nodes
- synthetic menu bar and popup menu nodes
- basic roles for windows, labels, buttons, checkboxes, radios, edits, combos, list boxes, sliders, progress bars, tables, trees, and tabs
- these AT-SPI interfaces where applicable:
  - `Accessible`
  - `Application`
  - `Collection`
  - `Component`
  - `Action`
  - `Value`
  - `Text`
  - `EditableText`

However, several controls are exposed only as containers or roles. The direct implementation does **not yet** export the meaningful synthetic descendants needed for usable navigation in:

- list boxes
- list views
- report grids
- trees
- tab controls
- combo popup options

Its child-resolution logic currently covers real HWND children plus menu objects, not list rows, grid cells, tree items, tabs, or combo options.

### What the AccessKit branch already exports

The AccessKit branch already models:

- list box options
- list items
- report rows
- grid cells
- column headers
- tree items
- tab items
- combo popup options
- editable-combo text runs
- label relations
- active descendants
- row/column metadata
- visible-range export for large collections
- scroll metadata
- synthetic focus targets for rows, cells, tree items, and tabs

That gap is not decorative. It is the difference between “AT-SPI sees a container” and “a screen reader can use the control.”

## Why the AccessKit branch is faster

The AccessKit branch still rebuilds full snapshots when a top-level window is dirty, so its performance advantage is not simply that it does less total work. It is that it performs the right work at the right boundary.

### 1. AccessKit coalesces updates; the direct branch reacts inline

In the AccessKit branch, `swell_accesskit_window_changed()` mostly marks a top-level window dirty. `swell_accesskit_pump()` later rebuilds and pushes one consolidated update.

That means many widget mutations can collapse into one accessibility reconciliation.

In the direct branch, `swell_atspi_window_changed()` immediately inspects live state and may emit text, caret, selection, checked, value, and visibility changes. The accessibility work sits directly on hot mutation paths.

The structural difference is:

```text
direct branch:    many mutations -> many immediate accessibility inspections
AccessKit branch: many mutations -> one dirty bit -> one semantic reconciliation
```

### 2. AccessKit serves a semantic tree; the direct branch repeatedly recomputes from widgets

AT-SPI consumers ask many small questions: parent, child count, child at index, role, state, interfaces, relations, extents, and so on.

With AccessKit, those queries hit a normalized semantic tree.

With the direct branch, many answers require some combination of:

- reparsing object paths
- validating HWND/menu liveness
- walking sibling chains
- recomputing roles and states
- deriving interface sets
- traversing live descendants

The direct branch's `Collection.GetMatches` implementation is representative: it recursively walks live objects and recomputes state/interface matches per request. That is correct in spirit, but expensive under a chatty client such as Orca.

### 3. The direct branch's cache support is not a decisive advantage

The direct branch recently added AT-SPI cache support. But its `Cache.GetItems` path builds a transient tree recursively on each request rather than serving a maintained semantic cache.

That can reduce some client chatter, but it also introduces an “assemble the world now” path. More importantly, the AccessKit branch appears to perform better even without implementing the AT-SPI cache interface, which strongly suggests that cache shape is not the primary explanation for the observed performance difference.

### 4. AccessKit already filters trees intelligently

AccessKit's common filter excludes hidden subtrees, transparent containers, and irrelevant off-screen descendants while retaining nearby items needed for scrolling behavior.

The SWELL AccessKit integration also has explicit large-collection policy:

- export all items for small non-owner-data lists
- export only a visible range plus margin for large/owner-data lists
- always keep the active item included

The direct branch has not yet had to solve this fully because it does not yet export those descendants. Once it does, it will need the same kind of policy to avoid either poor performance or poor accessibility.

### 5. AccessKit owns an asynchronous platform runtime

AccessKit's Unix adapter runs its own async event loop and bus handling. The direct branch uses GDBus directly from SWELL-side code. This is probably a secondary factor, but it helps AccessKit behave like a platform adapter rather than an extension of immediate widget execution.

## What AccessKit is actually buying

AccessKit is not merely a serialization layer. It is an accessibility engine.

### Diffing and event synthesis

In `../accesskit/platforms/atspi-common`, AccessKit compares old and new nodes and emits:

- changed states only
- changed properties only
- child additions/removals
- text insertions/deletions
- caret movement
- selection changes
- focus movement
- window activation/deactivation
- active-descendant changes

The direct branch has begun a smaller per-HWND previous-value ledger, but that currently covers only a fraction of the semantics needed for full support and does not yet cover the synthetic descendants required by complex controls.

### Semantic role mapping

AccessKit already contains a large evolved mapping layer from abstract roles to AT-SPI roles and interfaces. The direct branch currently has a compact hand-written mapping sufficient for early widgets, but not the long tail.

Recent work in `../accesskit` around table interfaces, active descendants, focused table cells, action key bindings, and text attributes shows the kind of maintenance stream a direct provider would need to absorb locally forever.

### Relations and naming

The AccessKit branch already exports `labelled_by` relations using dialog-label heuristics. The direct branch currently returns an empty relation set.

This looks small in code but is large in user experience: it is the difference between a screen reader saying “edit” and “Single-line edit.”

### Text semantics

The direct branch has useful early `Text` and `EditableText` support. The AccessKit branch goes further by modeling:

- text input nodes
- synthetic text run children
- character lengths
- character positions
- character widths
- text-selection nodes
- caret state
- scroll offsets

AccessKit then layers mature text-event logic on top of that representation.

## Signs that the direct design is already bending

### Focus divergence

The direct branch already needs special handling for menu focus and has an unused dedicated focus-event helper. That is a signal that native widget focus and accessibility focus are diverging. The divergence becomes unavoidable for menu items, list rows, grid cells, tree items, tabs, and active descendants.

AccessKit already treats these as first-class semantic concepts.

### Synthetic identity

The direct branch currently has synthetic identities for menus. The AccessKit branch already has stable synthetic IDs for text runs, combo text runs, list items, grid rows, grid cells, column headers, tree items, tabs, combo options, and popup instances.

Stable synthetic identity is the basis of sane diffing, focus, and eventing. The direct branch has only begun that work.

### Dynamic dispatch pushes work into query time

The direct branch uses a dynamic GDBus subtree and resolves objects on demand. That is concise for initial exposure, but it places more cost on individual queries than AccessKit's model of registering platform interfaces as the semantic tree changes.

## Could the direct branch reach parity?

Yes, but only by growing the missing middle layer.

A serious parity effort would require:

1. a persistent semantic node model
2. stable IDs for all synthetic descendants
3. dirty-state batching per top-level window
4. synthetic descendants for lists, grids, trees, tabs, combos, and text runs
5. filtered-tree policy for hidden and clipped content
6. large-collection export-range policy
7. an old/new diff engine
8. event synthesis for properties, children, text, selection, focus, active descendants, and bounds
9. richer interfaces such as `Selection`, `Table`, and `TableCell`
10. relation modeling and richer text semantics
11. ongoing compatibility maintenance for Orca and AT-SPI behavior

At that point, the direct implementation would no longer be the simple lazy design that motivated the branch. It would be a bespoke SWELL accessibility runtime that happens to speak AT-SPI directly.

That is feasible, but it is very close to recreating the relevant part of AccessKit.

## The decisive clue from `../accesskit`

The most important clue in the AccessKit checkout is its layering:

```text
widget-provided semantics
  -> consumer tree and filtering
  -> AT-SPI common semantic mapping and diffing
  -> Unix platform transport
```

That is exactly the layer cake the direct branch lacks. The AccessKit branch is faster and further along because it is standing on that layer cake rather than rebuilding it inside SWELL one feature at a time.

## Recommendation

Keep this branch as an investigation artifact, not as the production path.

For real SWELL accessibility on Linux, the AccessKit branch is the better foundation because:

- the observed performance advantage is structural
- the feature gap is already substantial
- direct parity would duplicate a large amount of mature infrastructure
- AccessKit continues to improve upstream in exactly the areas SWELL needs
- its abstraction boundary matches the problem better than a direct widget-to-AT-SPI bridge

The direct branch proves that SWELL can speak AT-SPI. The AccessKit branch is much closer to proving that SWELL can provide usable accessibility.

## If this branch is revisited later

If direct AT-SPI is ever reconsidered, the first premise to revisit should be the pure “lazy live state” design. A competitive direct implementation would likely need:

- a semantic cache
- dirty batching
- stable synthetic nodes
- a diff engine

Once those are admitted, the burden of proof shifts heavily toward explaining why SWELL should own that runtime instead of relying on AccessKit.
