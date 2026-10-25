# SeSaML / SetScript — Design Document

*A set-theoretic scripting language and document model, as if shipped by Netscape in 1995.*

---

## 1. Premise

HTML plus the DOM is one particular answer to "how do you represent a document as data." SeSaML is the answer that never gets asked: there is no DOM, because there was never a document-vs-tree distinction to invent. There are only sets, and membership.

The one-line pitch, as stated directly: **COBOL and SeQL had a baby, raised declaratively by React.**

The counterfactual: in 1995, before HTML calcified into standardized infrastructure, a sufficiently compelling demo at Netscape could have made HTML the thing that never became the standard, rather than the thing that had to be worked around. By 1997 the window is closed — the DOM spec is being written, IE4 is shipping. In 1995 the market leader could still ship something else as *the* format.

SetScript is the applied, shippable layer of this idea, constrained to real 1995 hardware and Win32. The pure, unconstrained version of the same idea is a separate thought experiment (sometimes called STL) — publishable as PL-theory philosophy, not meant to ship. SeSaML sits at the applied end: it's SetScript's language, aimed at being a document format.

**Important distinction from a related, separate project:** SeSaML is not the austere symbolic esolang sometimes discussed alongside it (internally called "Russell's Nightmare" in some sessions), which deliberately strips everything down to bare set-algebra symbols (`∈ ∉ ∪ ∩ ⊆` etc.) with no concession to readability. SeSaML is explicitly the *opposite* choice: it keeps ordinary English-keyword control flow, ordinary infix arithmetic, and a real (if minimal) standard library, because it's meant to be shippable and learnable by ordinary 1995 web developers, not a foundations-of-mathematics thought experiment. Where the pure/symbolic language says "there is no `+`, only `∪`," SeSaML just has `+`.

---

## 2. The One Primitive

**Everything is a set. Membership is the only state. Migration (moving an element into or out of a set) is the only mutation.**

No numbers, no strings, no booleans, no DOM nodes, no functions as a distinct kind of thing — all of these are modeled as sets or as membership in sets. This is a smaller axiomatic core than Lisp: Lisp still has atoms, cons cells, and nil as distinct kinds of thing. SeSaML has one kind of thing.

Consequences that fall out for free, rather than being separately designed:

- **Styling is membership.** An element is red because it's a member of `Red`, not because it has a `color` property.
- **Layout is a Cartesian product.** The screen is `ScreenX × ScreenY`. A position is membership in that product. A region is a subset of it.
- **State is membership.** An element being focused, hidden, selected — all membership in named sets (`FocusSet`, `Hidden`, etc.), not boolean flags.
- **Control flow is membership cycles**, not imperative loops (see §8, still open).
- **Execution is intersection with the function class** (see §6).

---

## 3. Why Booleans Were Eliminated

A boolean property is a lazily-defined set waiting to be named properly. The rule: **if a property partitions your data, make the partition an explicit named set.**

- `isAdult: true/false` → membership in a set called `Adults`.
- Scalar/ordered properties are the only legitimate exception — `age`, `phoneNumber` — because the partition would be infinite or meaningless to enumerate (nobody wants a set called `Age37`).

This is not a product-type objection (see §5) — it's a modeling-discipline objection. A boolean is information smuggled in as a scalar instead of being named as a set.

---

## 4. Ordered Sets Subsume Coordinates, Tuples, and Scalars

Rather than introduce numbers, tuples, or pairs as new primitives, **ordered sets** are added as one extension to the model, and everything falls out of them:

- **Age, phone numbers** — membership in an ordered set of integers/digits, not a scalar property.
- **Coordinates** — `X` and `Y` are ordered sets; a pixel position is an ordered pair.
- **Ordered pairs / tuples generally** — a 2-tuple `(a, b)` is just a two-element ordered set. No Kuratowski encoding (`{{a},{a,b}}`) is needed, because the encoding trick Kuratowski used to build pairs out of unordered sets is unnecessary when ordered sets already exist as a primitive.
- **Cartesian product**: `A × B` takes two sets and returns an ordered set of ordered pairs, `{(a,b) : a∈A, b∈B}`. Computationally this is the standard nested-loop construction; the result size is `|A|×|B|`.

This directly resolves the tension between "the screen is a table of pixels, i.e., a product" and "I deleted booleans to avoid faking primitives via products." The resolution: **products are legitimate when the domain is irreducibly product-shaped** (the screen genuinely is `X × Y`) **and illegitimate when used to fake something that should be atomic** (a boolean encoded as `{0,1} × {0,1}` would be the illegitimate case). The screen qualifies for the former; booleans were correctly excluded as the latter.

---

## 5. FLAT Sets — Explicit Composition, Not Inheritance

A set can be declared **FLAT**. This is a property of the set, not the element:

> If `Button` is FLAT and `Button ⊆ Clickable`, then anything added to `Button` is automatically added to `Clickable` too. Membership propagates upward through the flat set's declared supersets.

Without FLAT, membership stays local — an element can be in `Button` without ever appearing in `Clickable`. FLAT is an explicit, chosen composition operation, not automatic classing or inheritance in the OOP sense.

This gives two genuinely different relationships, cleanly separated at the set level instead of conflated the way class-based OOP conflates them:

- **"Is tagged as"** — casual grouping, non-flat. A `Button` can be in the set `Red` without anything about `Button`'s internals being exposed to `Red`.
- **"Is a kind of"** — a formal, load-bearing ontological commitment, expressed via FLAT.

### Multiple flat supersets

If `Button` is flat with two supersets, `Clickable` and `Draggable`, membership propagates to both. A conflict is only detected **at insertion time**, not at set-declaration time — the schema owns the rule, the insertion owns the violation, and rejecting eagerly at declaration time would preemptively reject valid architectures that never actually hit a real conflict.

---

## 6. Forced Schema Sets — Validation via Intersection Contracts

A set can declare a **schema**: a constraint expressed as an intersection contract rather than a bolted-on type system.

> `Clickable` requires that its members, when intersected with `Function`, return only sets of a certain shape — specifically, members must **have** a function element, not **be** one.

Worked example (the canonical test case for this mechanism):

1. `Button` is FLAT, `Button ⊆ Clickable`.
2. A raw `Function`-typed set (e.g. `DoThing`) is added directly to `Button` as an element.
3. Because `Button` is flat, `DoThing` propagates up to `Clickable`.
4. `Clickable`'s schema screams: it expected members that *contain* a function, not a function itself.
5. **The entire insertion is rejected** — `DoThing` is not left in `Button` either, since the violation was surfaced by the propagation the insertion would have caused. (If `Button` were *not* flat, `DoThing` would stay purely local to `Button` and no violation would ever be detected — flatness is what exposes the modeling error.)

This means **schema validation and set algebra are the same mechanism.** No separate type system is bolted on; a "forced schema set" is just a set whose declared intersection behavior acts as its contract, checked at insertion time.

### Type system as membership predicate (design-by-contract equivalent)

The full type system is set membership, and this was explicitly argued to be equivalent to design-by-contract, not a weaker substitute for it:

- **Primitives are predicated sets.** `IntegerSet` accepts only integers; passing a string where a member of `IntegerSet` is expected fails to compile. The predicate *is* the type check.
- **Floating-point issues are arithmetic issues, not type-system issues** — no type system fixes precision or overflow, in any language, and `FloatingPointSet` isn't claimed to be a fix for that.
- **String subtypes are FLAT subsets with different predicates.** `ASCIIString` and `UTF8String` are both flat subsets of `StringSet`, each with its own encoding predicate. A function expecting `UTF8String` will not accept `ASCIIString` unless `ASCIIString` is itself declared a subset of `UTF8String` — which you'd only do if that's semantically true in your domain.
- **Pre/post-condition contracts fall out for free.** A sort routine takes members of `UnsortedSet` and must produce members of `SortedSet`; `SortedSet` carries an ordering predicate as part of its definition. If the output doesn't satisfy the predicate, it simply isn't a member of `SortedSet`, and the program doesn't compile. This was argued to be *the same foundation* as Eiffel's design-by-contract, not an addition on top of an object model the way Eiffel's contracts are — the predicate *is* the type system, rather than sitting on top of one.

---

## 7. Namespace Sets — Encapsulation and the Universal-Set Problem

A true universal set (containing literally everything) is computationally useless: complementing `Red` shouldn't hand back every function, integer, and off-screen element in existence along with the elements you actually meant.

The fix is **namespace sets**: sets that hide their members such that they're only reachable via explicit traversal, e.g. `Charles.Age ∩ ℤ`. The dot-notation is scoped membership traversal — still pure set algebra, no separate primitive introduced. This is standard practice in formal logic and model theory too: a model assumes a bounded *domain of discourse*, not literally everything.

A concrete bug this solves: without namespacing, complementing a category like `Ferromagnetic` against a naive universal set returns both non-ferromagnetic *elements* (e.g. `Radioactive` isotopes) and non-ferromagnetic *category sets themselves* (e.g. the set `Radioactive` as an object), because the universal set didn't distinguish leaf elements from higher-level category sets. Namespace sets (sub-universal sets acting as scoped runtime environments) wall categories off from elements so traversal and complement stay meaningful.

---

## 8. Closed World, Nulls, and Garbage Collection

**Closed world is a per-instant property of the runtime, not a static property of the whole program.** At any given moment, the universal set *is* the universe — nothing exists outside it, and non-membership is a complete, trustworthy answer (not "unknown," as it would be under an open-world assumption). This is what makes complement well-defined and reliable.

This doesn't conflict with I/O: reading a file or a network response just *adds new members* to the runtime's set hierarchy. The world is closed at any instant; it's not closed across time. A GET request returning a set is simply new membership being asserted.

**No null.** A set either has a member or it doesn't. If an I/O operation would return nothing, nothing is added — the set stays valid rather than being poisoned with a null sentinel.

**Garbage collection is membership-based.** A set that loses every (non-`UNIVERSAL`) parent is destroyed, and any of its own childless dependents cascade with it. This is a direct consequence of "membership is the only state" rather than a separately bolted-on GC algorithm.

---

## 9. Self-Membership

Self-membership is simply **disallowed by axiom** — no set may contain itself. This is stricter than ZFC's regularity axiom (which rules out self-membership as a *derived consequence* of the axiom of foundation) but simpler: SeSaML just states it directly rather than deriving it from a stratified type theory (à la Russell) or from foundation-axiom machinery. Practically this also means the set of all functions, or any similarly broad category set, is well-defined without re-deriving Russell's Paradox: it's a proper-class-like set of things-that-are-functions, and since a function (being a set) can't be its own member, no paradox arises.

---

## 10. Surface Syntax

SeSaML's concrete syntax reads as declarative element construction plus event wiring — closer to a declarative UI DSL than to a general-purpose language, with functional sets layered in for Turing completeness. This is drawn directly from working examples, not just the four stated influences.

### Basic element declaration and layout

Coordinates are named positions on the `X`/`Y` sets (`Center`, `Left`, `Top`, or a literal number), not raw pixel math:

```
CREATE Greeting AS TextBox
    WITH text = "Hello World"
    AT X.Center, Y.Center
```

### Buttons, events, and membership as visibility

```
CREATE HideButton AS Button
    WITH label = "Hide"
    AT X.Center, Y.300
    ON CLICK REMOVE Greeting FROM VISIBLE

CREATE ShowButton AS Button
    WITH label = "Show"
    AT X.Center, Y.350
    ON CLICK ADD Greeting TO VISIBLE
```

`VISIBLE` is a built-in set. Membership in it *is* visibility — there's no separate `display` or `hidden` property. Hiding ten elements is one `REMOVE MySet FROM VISIBLE` against a containing set — no loop is written; membership propagation handles all ten at once:

```
CREATE MySet AS SET

CREATE Item1 AS TextBox WITH text = "First Item"  AT X.Left, Y.100 MEMBER OF MySet
CREATE Item2 AS TextBox WITH text = "Second Item" AT X.Left, Y.150 MEMBER OF MySet

CREATE HideAll AS Button
    WITH label = "Hide All"
    AT X.Left, Y.200
    ON CLICK REMOVE MySet FROM VISIBLE
```

### Built-in events

```
ON CLICK   — mouse pressed and released on the element
ON DRAG    — mouse held and moving
ON DROP    — mouse released after a drag
ON TYPE    — keyboard input while the element has focus
```

### Conditional event guards

```
CREATE SubmitButton AS Button
    WITH label = "Submit"
    AT X.Center, Y.300
    ON CLICK WHERE InputField IS NOT EMPTY
        SEND InputField TO server/endpoint
```

`WHERE` suppresses the event entirely if the condition doesn't hold — no separate `if` inside the handler body needed for the common case.

### Text input

```
CREATE InputField AS TextInput
    AT X.Center, Y.200
    WITH placeholder = "Type something"
```

### Drag and drop via the built-in MouseSet

```
CREATE Item AS ContentPane WITH text = "Drag me"    AT X.Left, Y.Top
CREATE Target AS ContentPane WITH text = "Drop here" AT X.Right, Y.Top

ON DRAG OF Item
    ADD Item TO MouseSet

ON DROP WHERE TARGET IS Target
    MOVE Item TO Target.Location
```

`MouseSet` tracks current cursor position as a built-in — no manual coordinate tracking is written.

### Loading data from a server

```
CREATE ProductList AS SET
    AT X.Left, Y.Top

LOAD Products FROM SERVER AT /api/products
    AS ProductPane
    MEMBER OF ProductList
```

The server response is itself a SeSaML set declaration; the runtime instantiates the members and assigns membership directly rather than the client writing individual `CREATE` statements for each item. The server side can be written in any HTTP-speaking language.

### Named event-target sets (reference table)

| Set | Behavior |
|---|---|
| `ON_KEY` | Function sets here execute on key events for the focused set. |
| `ON_CHANGE` | Function sets here execute when a member set's value property changes. |
| `ON_LOAD` | Function sets here execute once on document load. |
| `ON_TIMER` | Function sets here execute on timer events from the event queue. |
| `FOCUS` | At most one set is a member at any time. Keyboard events route to this set. |
| `HOVER` | Automatically managed by the runtime based on cursor position. |

### Styling sets

All styling sets require the member to have a `content` property of type `STRING` for text rendering, or a coordinate membership for layout rendering.

| Set | Behavior |
|---|---|
| `BOLD` | Renders member text bold. |
| `ITALICS` | Renders member text italic. |
| `UNDERLINE` | Renders member text underlined. |
| `STRIKETHROUGH` | Renders member text with strikethrough. |
| `HYPERLINKS` | Members require a `link` property. Click navigates to link. |

### Color sets

Sixteen built-in colors corresponding to the CGA palette — period-authentic for 1995: `BLACK`, `WHITE`, `RED`, `GREEN`, `BLUE`, `CYAN`, `MAGENTA`, `YELLOW`, `DARK_RED`, `DARK_GREEN`, `DARK_BLUE`, `DARK_CYAN`, `DARK_MAGENTA`, `DARK_YELLOW`, `GRAY`, `DARK_GRAY`.

Custom colors are unary sets of type `COLOR`:

```
CREATE NavyBlue AS COLOR
SET NavyBlue.value TO #001F5B
```

### Widget sets (reference table)

| Set | Required Properties | Behavior |
|---|---|---|
| `BUTTONS` | `content` (STRING) | Rendered as a clickable button. Click intersects with `ON_CLICK`. |
| `TEXTFIELDS` | `content` (STRING) | Rendered as an editable text input. Receives keyboard events when in `FOCUS`. |
| `CHECKBOXES` | `checked` (INTEGER) | Rendered as a checkbox. Click toggles `checked` between 0 and 1. |
| `RADIO` | `group` (STRING), `checked` (INTEGER) | Rendered as a radio button. Click sets `checked` to 1 and all other members of the same `group` to 0. |
| `IMAGES` | `src` (STRING) | Rendered as a bitmap image loaded from `src`. |

### Document structure and navigation

A SeSaML document is a plain text file served over HTTP. The runtime fetches and parses it. Navigation between documents uses `HYPERLINKS`. There is no page reload on navigation — the runtime loads the new document's set tree and merges or replaces as specified.

---

## 11. Functional Sets and Turing Completeness

SeSaML achieves Turing completeness through **functional sets** — sets that are themselves functions, expressed via the same `CREATE ... AS` grammar used for everything else. Control flow is expressed through set-membership predicates rather than imperative branching/looping constructs bolted on separately.

**Conditional:**
```
CREATE IfPositive AS FUNCTION
    TAKES Count
    IF Count.value > 0 THEN
        ADD Count TO POSITIVE_NUMBERS
    END
END
```

**Recursion:**
```
CREATE Factorial AS FUNCTION
    TAKES N
    IF N.value <= 1 THEN RETURN 1 END
    RETURN N.value * CALL Factorial WITH (N.value - 1)
END
```

**Iteration** is not a separate looping construct — it's expressed either through recursive functional sets with a base case (as above), or through traversal of an ordered set. There is no dedicated `FOR`/`WHILE` keyword; recursion plus ordered-set traversal is treated as sufficient and more consistent with "everything is a set."

### This is not Russell's Nightmare — ordinary control flow and arithmetic are real, first-class syntax

It's worth being explicit about this, since it's easy to conflate SeSaML with the more austere symbolic thought-experiment language discussed in adjacent sessions. **SeSaML is not that.** Look again at the `Factorial` example above: `N.value <= 1`, `N.value * ...`, `N.value - 1` are ordinary infix comparison and arithmetic expressions, evaluated the normal way — not `∩`/`∪`/`\` set-algebra symbols standing in for arithmetic. `IF ... THEN ... END` is a real conditional keyword construct, not a membership predicate written out in symbolic set notation.

The design choice is deliberate: sets are the *data model and the styling/layout/state model*, but they were never meant to replace ordinary expression syntax for something as mundane as `x > 0` or `n - 1`. A 1995 web developer writing SeSaML writes `IF`, does arithmetic with `+`/`-`/`*`/`/`, and calls functions with `CALL ... WITH (...)` — they never have to think in raw set-theoretic notation to do everyday things. The set-theoretic purity lives in *what the language's nouns are* (an element, a style, a position, a function are all sets under the hood), not in *forcing the verbs into set-algebra symbols*. That trade-off is what makes SeSaML plausible as something Netscape could have shipped to ordinary developers, as opposed to the pure esolang, which was never intended to be usable by anyone who isn't already fluent in ZFC notation.

### Why functional sets specifically (not bolted-on functions)

A function is not a distinct kind of thing from a set — it's a set whose members happen to be treated as an input/output mapping, consistent with the model's single-primitive commitment (§2). This is the Lisp influence made concrete: functions are data, first-class, composable, and passable as ordinary set members, rather than a special-cased construct the way functions are special in most C-family languages. `CALL X WITH (...)` and `TAKES`/`RETURN` are the only additional grammar needed on top of the base `CREATE`/`ADD`/`REMOVE` vocabulary — no separate function-definition syntax exists outside of `CREATE ... AS FUNCTION`.

### A larger worked example — case-insensitive, no entry point

The language is case-insensitive throughout; uppercase is a style convention, not a requirement. There is no `main` function and no explicit entry point — the document declares an initial state and an event queue, and execution is event propagation from that initial state. A complete multi-pane application with tab-style navigation and drag-and-drop, using the built-in `SWAP` and `MOVE` verbs, runs to about 33 lines:

```
CREATE HomePane AS ContentPane
    WITH text = "Welcome"
    AT X.Center, Y.Main

CREATE DocsPane AS ContentPane
    WITH text = "Documentation"
    AT X.Center, Y.Main

CREATE HomeLink AS NavItem
    WITH label = "Home"
    AT X.Nav, Y.Top
    ON CLICK SWAP ActiveContent WITH HomePane

CREATE DocsLink AS NavItem
    WITH label = "Docs"
    AT X.Nav, Y.Mid
    ON CLICK SWAP ActiveContent WITH DocsPane

CREATE SaveButton AS ToolbarItem
    WITH label = "Save"
    AT X.Toolbar, Y.Top

SET ActiveContent TO HomePane

ON DRAG OF ContentPane
    ADD DRAGGED TO MouseSet

ON DROP WHERE TARGET IS NOT NavItem
    MOVE DRAGGED TO DROP.Location
```

`SWAP a WITH b` replaces the current binding of `a` in-place with `b`, re-triggering delta propagation for anything depending on `a`. `MOVE x TO location` updates `x`'s coordinate-set membership directly (see §4 — position is membership in `X × Y`).

---

## 12. The Runtime, Event Loop, and Async Model

This is the concrete answer to "how does async actually work" — SeSaML has a real, fully specified execution model, not just a philosophical claim that "cycles are the only control flow."

### Parsing produces the live state directly

There is no separate AST and evaluation pass. Parsing a `CREATE` statement calls `set_create` directly; parsing a membership declaration calls `set_add_member` directly. The parse result *is* the runtime state, live as soon as parsing finishes.

### The event loop

The runtime runs a single event loop listening for platform events (mouse, keyboard, timers, network) and resolves each one against the current set tree:

- **Mouse click** at coordinate `(x, y)` → find all sets whose coordinate membership intersects `(x, y)` → intersect that result with `ON_CLICK` → push the resulting function set(s) onto the execution stack.
- **Key press** → find the set currently in `FOCUS` → intersect with `ON_KEY` → push the resulting function set(s).
- **Timer fires** → pop the corresponding entry from the event queue → push its function set onto the execution stack.

Dispatch itself is literally implemented as set intersection — "what fires on this click" is `(sets at this coordinate) ∩ ON_CLICK`, not a separately maintained listener-callback table.

### STACK and QUEUE — the async mechanism, exposed as ordinary sets

This is the actual async model, and it's deliberately **not** multithreaded (see the threading note at the end of this section):

- **`STACK`** is a built-in ordered set representing the execution stack. Function sets are pushed onto it and executed in order; a function set may mutate memberships/properties, push further function sets (this is how recursion works), or return a value to its caller.
- **`QUEUE`** is a built-in set holding pending asynchronous operations — timers and network requests specifically. When a timer fires or a network response arrives, its associated function set is *moved from `QUEUE` to `STACK`* by the runtime.
- Both `STACK` and `QUEUE` are directly inspectable and mutable by ordinary SeSaML programs using standard set operations — the runtime doesn't hide its own execution state from the language. This was called out explicitly as a design principle: **introspectability** — "the execution stack and event queue are sets. The program can see and manipulate its own runtime."

This gives you the same practical effect as JavaScript's task/microtask queues (a pending operation doesn't block the main loop; its continuation runs later when the loop gets to it) but implemented as literal, first-class, inspectable set membership rather than an opaque runtime-internal queue. Network requests specifically are modeled as `GET`/`POST` operations that enqueue onto `QUEUE` and resolve asynchronously — this is what enables polling-based live updates (email refresh, Webcel recalculation triggers) without ever blocking the event loop.

### Delta propagation — the actual redraw/reactivity algorithm

Every set maintains two pointer arrays: `members[]` (forward) and `parents[]` (reverse, i.e. "what sets is this a member of"). When a set's membership changes, the runtime:

1. Walks `parents[]` — every set that contains the changed set as a member.
2. Marks those sets dirty.
3. Walks *their* `parents[]` recursively.
4. Re-evaluates dirty sets in reverse topological order.
5. Redraws only the screen regions actually affected.

This is `O(n+m)` where `n` is the number of affected sets and `m` is the number of affected memberships — a large document with a small, localized change stays cheap. This is the literal mechanism behind the "no DOM re-rendering" claim in §13 below: dirty propagation is structural (it follows real parent pointers), not heuristic (there's no diffing algorithm guessing what changed).

### Threading note (restated precisely)

Single-threaded, JS-style event loop — `GetMessage`/`MsgWaitForMultipleObjects` as the Win32 equivalent of epoll. Threading was explicitly considered and rejected as premature optimization for v1; CantorDB itself has zero threading primitives, which was treated as supporting evidence rather than a limitation.

---

## 13. Rendering Model and Update Strategy (SetScript, Win32 target)

Consistent with "everything is a set," UI elements have no built-in widget behavior beyond what's defined above — they're composed from primitives already defined:

- **A text box** is a set with a text property (itself an ordered set of characters), membership in a coordinate set (position), and membership in `FocusSet` (whether it's active). On keypress, if the element is in `FocusSet`, the character is appended and the element is marked dirty for redraw. Cursor blink is a timer event toggling membership in a `Blinking`-type set — not a boolean flag.
- **No DOM re-rendering / no reflow model.** Updates are set operations — add or remove a member, the runtime resolves what changed, and only the affected coordinate sets redraw. This was explicitly argued to be closer to a game engine's dirty-rectangle approach than a browser's reflow model, and to matter *more* in 1995 than it would today: full-document reflow on a 66MHz 486 with 8MB RAM was genuinely painful (part of why early DHTML felt janky even into the late '90s). Membership-driven redraw sidesteps that class of problem entirely rather than optimizing around it.
- **Rendering backend** is abstracted behind a thin interface (`render_rect`, `render_text`, `poll_events`, `begin_frame`, `end_frame`) so the runtime never depends on the specific backend. In the 1995 counterfactual, Raylib stands in for what would have been a hand-rolled Win32 GDI wrapper — justified as the kind of pre-written personal toolkit a competent 1993–95 C programmer would already have, not an architectural cheat. (A weaker, alternate position considered: since the goal is testing a historical thesis rather than shipping a product, a C89-compliant runtime that simply *compiles clean* is sufficient proof on its own — an actual Win32 backend is a separable, bounded task that doesn't need to be built by hand to make the argument.)
- **Native widgets (e.g. Win32 `EDIT` controls)** could be used through the wrapper for free cursor/selection/keyboard handling, but this creates real tension with the pure set model, since the widget's internal state would then live outside the set hierarchy entirely. **This tradeoff is unresolved** — see open questions below.
- **Event loop**: single-threaded, JS-style (`GetMessage`/`MsgWaitForMultipleObjects` as the Win32 equivalent of epoll), not multi-threaded. Threading was explicitly considered and rejected as premature optimization for v1 — CantorDB itself has zero threading primitives, which was treated as supporting evidence rather than a limitation.

### Honest limitations, argued directly

- **Network-bound demos don't work.** Live-updating email over a poll loop was judged a failure case for the 1995 counterfactual — not because of SeSaML's model, but because push required persistent connections 1995 infrastructure couldn't sustain. SeSaML solves the *runtime* problem, not the *transport* problem. Anything depending on fast server round-trips is blocked by infrastructure the language can't touch.
- **Webcel is the strongest demo**, precisely because it's entirely client-side once data is loaded: spreadsheet-as-set-membership maps cleanly onto the model, and it needs no network round-trip mid-interaction.
- **Debuggability is an open wound, not a solved problem.** When something doesn't render or an event doesn't fire, tracing why means querying the full membership state at the moment of failure by hand — there's no dedicated debugger yet, and this was explicitly named as the most pressing missing piece of tooling.
- **Governance/continuity risk**, raised and only partly answered: SeSaML would have been a single-vendor (Netscape) product, unlike a standards-body language. The stated mitigating argument is that a language with one coherent primitive is far easier to reimplement from spec than one with years of accumulated specification — simplicity as a survival property, not a guarantee of survival.
- **Search-engine indexability** was raised as a real open problem: HTML's tag structure is directly parseable by a 1995-era crawler; a set-membership structure would need a crawler that understands set membership, which none did. The honest answer given was that the document *does* have structure (it's set membership) but current tooling doesn't understand that structure, and that gap was left unresolved rather than argued away.

---

## 14. Language Lineage

SetScript's surface syntax and structure draw from:

- **SeQL** — the set-algebra query language already implemented for CantorDB (Principia Mathematica–style dot-precedence notation).
- **COBOL** — sectioned, near-English code structure.
- **React** — declarative UI binding (describe the desired set membership, don't imperatively mutate).
- **Lisp** — functions as data (a function is a set; the `FUNCTION` class is just the set of all sets-that-are-functions).

---

## 15. Demonstration Targets (the 1995 press-demo thought experiment)

To argue this could have shipped instead of JavaScript, the following demos were treated as sufficient proof, runnable on period-authentic hardware (a Windows 95 VM on a period-accurate 486):

- **Webcel** — a live spreadsheet. Each cell is a text box; Enter triggers formula parsing and delta propagation up parent-membership pointers.
- Drag-and-drop toolbar rearrangement (~35 lines).
- Fluid, resolution-agnostic window resizing via coordinate sets rather than hardcoded pixels.
- Tag-based filtering (client-side, since it avoids the network-latency problem above).
- Long-poll chat and turn-based chess over the web via server polling.

Rough build-hour estimate for the full runtime + Webcel, excluding the pre-written rendering wrapper: lexer/parser (~20h), coordinate system + renderer (~25h), event loop + delta propagation (~30h), Webcel (~25h), supporting buffer/utility work (~20h) — roughly 120 hours, treated as a fair comparison point against Brendan Eich's original 10-day JavaScript sprint, since the set-engine foundation (CantorDB) is treated as already existing pre-clock, the same way Eich had an existing Netscape codebase and host browser to build inside.

---

## 16. Open Questions (unresolved as of this document)

1. **Fine-grained halting semantics for functional sets.** §12 resolves the *runtime scheduling* question concretely (STACK/QUEUE, intersection-based dispatch, delta propagation). What's still loose is the halting/termination condition for a single functional set specifically — i.e. what stops a pathological recursive functional set from infinitely pushing onto `STACK`, beyond "the base case in the `IF` eventually returns." No stack-depth or timeout safeguard has been specified.
2. **Native widget boundary.** Whether SetScript draws its own controls (text boxes, buttons) entirely through the set model, or delegates to native Win32 controls through the render wrapper and accepts that some state then lives outside the set hierarchy.
3. **FFI / foreign-function boundary.** How a set-theoretic runtime calls out to native C code cleanly — flagged as unresolved, no concrete design yet.
4. **Multi-schema conflict resolution, general case.** The Function/Clickable example is worked out concretely, but the *general* rule for how two forced schemas interact when a flat set inherits from both has not been generalized beyond that one example.
5. **Debugger/tooling for membership-state inspection.** Named directly as the most pressing missing piece: there is currently no way to inspect "why didn't this render" or "why didn't this event fire" other than manually querying runtime state at the point of failure.
6. **Search-engine indexability of a non-HTML document format** — raised as a legitimate concern with no resolution beyond "the structure exists, current tools don't understand it."

---

*This document consolidates design decisions made across multiple conversations. It reflects the applied SetScript/SeSaML layer specifically — the pure, unconstrained set-theoretic language (STL) and the separate Russell's Nightmare bytecode VM (which has its own, more elaborate symbolic notation and a distinct three-way complement split) are related but distinct projects and are deliberately excluded here.*
