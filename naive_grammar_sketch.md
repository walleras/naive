# Naive — Grammar Sketch (v0.1, extrapolated)

**Read this before the grammar itself.** Nothing in prior sessions ever assembled a formal grammar — every construct that exists is a worked example. This document reverse-engineers a consistent EBNF from those examples. Every rule is tagged:

- **[ATTESTED]** — directly supported by an example seen in a prior session.
- **[INFERRED]** — a reasonable generalization of an attested pattern, not itself directly shown.
- **[INVENTED]** — filling a real gap that was never addressed at all (comments, precedence, logical operators). These are guesses and should be treated as a starting proposal to argue with, not settled design.

Notation: `::=` defines a rule, `|` is alternation, `[ ]` optional, `{ }` zero-or-more, `'...'` literal terminal, `UPPERCASE` a lexer token.

---

## 1. Lexical Notes

- **Case-insensitivity** [ATTESTED]: the language is case-insensitive; uppercase is a style convention only.
- **Line-oriented, not semicolon-terminated** [INFERRED]: every example uses newlines as statement separators, never `;`. This grammar assumes `NEWLINE` is significant, following the pattern already used in the related Scratch++ project rather than C-style semicolons.
- **Comments** [INVENTED — never specified anywhere]: proposing `--` to end of line, consistent with the SQL/COBOL lineage already claimed for the language:
  ```
  -- this is a comment
  CREATE Foo AS Button  -- trailing comment
  ```
  A block-comment form was never needed in any example and is deliberately omitted rather than invented further.
- **Identifiers** [INFERRED]: `ID ::= LETTER (LETTER | DIGIT | '_')*` — standard, matches every example name (`HomePane`, `NavyBlue`, `POSITIVE_NUMBERS`).
- **Dot access** [ATTESTED]: `Name.property` (`N.value`, `Target.Location`, `Charles.Age`) is a token-level construct: `DOTTED_ID ::= ID '.' ID`.

```
NEWLINE    ::= '\n' | '\r\n'
LETTER     ::= 'a'..'z' | 'A'..'Z'
DIGIT      ::= '0'..'9'
ID         ::= LETTER (LETTER | DIGIT | '_')*
DOTTED_ID  ::= ID ('.' ID)+
NUMBER     ::= DIGIT+ ('.' DIGIT+)?
STRING_LIT ::= '"' (any char except '"')* '"'
COLOR_LIT  ::= '#' HEXDIGIT HEXDIGIT HEXDIGIT HEXDIGIT HEXDIGIT HEXDIGIT
COMMENT    ::= '--' (any char except NEWLINE)* NEWLINE   -- [INVENTED]
```

---

## 2. Top-Level Structure

A document is a flat sequence of statements. There is no `main`, no entry point [ATTESTED — "There is no main function. There is no entry point."]. Order of `CREATE` statements does not appear to matter based on examples (forward references, e.g. `ON CLICK SWAP ActiveContent WITH DocsPane` before `DocsPane` is fully wired elsewhere, are used freely) — this implies a two-pass load model [INFERRED, by analogy with the two-pass approach used in the sibling Scratch++ project, which explicitly solved the same forward-reference problem].

```
document      ::= { top_level_stmt }
top_level_stmt ::= create_stmt
                 | mutation_stmt
                 | event_stmt          -- only valid attached to a create_stmt in every example seen; see §4
```

---

## 3. Declarations — `CREATE`

This is the core declaration form. All attested examples share this shape:

```
create_stmt ::= 'CREATE' [ 'FLAT' ] ID 'AS' type_name
                [ with_clause ]
                [ at_clause ]
                [ member_clause ]
                { event_clause }

type_name    ::= ID                     -- Button, TextBox, FUNCTION, SET, COLOR, ContentPane, NavItem, etc.
```

`FLAT` is a modifier keyword sitting between `CREATE` and the name being declared — not a value in the `AS` slot. This reads naturally as "create a flat [thing]" rather than "create a thing whose type happens to be FLAT":

```
CREATE FLAT Button AS SET
    MEMBER OF Clickable, Draggable
```

Any element later added to `Button` automatically propagates upward into `Clickable` and `Draggable` too. Multiple supersets are just a comma-separated list after `MEMBER OF`.

Everything else about declarations is unchanged from before:

with_clause  ::= 'WITH' prop_assign { ',' prop_assign }
prop_assign  ::= ID '=' expr

at_clause    ::= 'AT' coord ',' coord
coord        ::= DOTTED_ID              -- X.Center, Y.300, X.Left  [ATTESTED]
                | ID                     -- bare axis reference, unattested but symmetrical [INFERRED]

member_clause ::= 'MEMBER' 'OF' ID      -- [ATTESTED]
```

**Gap flagged:** every attested example gives `AT` exactly two coordinates (`X.*, Y.*`). Nothing addresses 3D, z-index/stacking order, or elements with no spatial component at all (e.g. `CREATE MySet AS SET` appears with no `AT` in one example and *with* an `AT` in another — §7's `ProductList` has `AT X.Left, Y.Top` despite being declared `AS SET`). This grammar treats `at_clause` as always optional, but the underlying semantic rule ("does every set have an implicit position, or only ones meant to render") was never settled.

---

## 4. Events

```
event_clause ::= 'ON' event_name [ 'OF' ID ] [ where_clause ] action { NEWLINE action }
               | 'ON' event_name [ 'OF' ID ] [ where_clause ] NEWLINE INDENT action { action } DEDENT

event_name   ::= 'CLICK' | 'DRAG' | 'DROP' | 'TYPE'                     -- [ATTESTED] user events
               | 'KEY' | 'CHANGE' | 'LOAD' | 'TIMER'                    -- [ATTESTED] as named set targets (ON_KEY etc.); treated here as event names by analogy [INFERRED]

where_clause ::= 'WHERE' expr

action       ::= mutation_stmt
               | send_stmt
               | swap_stmt
               | move_stmt
               | call_stmt
```

**Block scoping — resolved.** Two distinct conventions, by design:
- **Set blocks** (the body attached to a `CREATE ... AS <Set-like thing>` declaration, including `ON` event actions declared under it) use **indentation/dedent scoping** — no closing keyword. This is the pattern already seen in every `ON DRAG OF Item` / indented-body example.
- **Everything else** (functional sets, `IF`, `FOR`, `WHILE`) closes with an explicit `END`.

```
event_clause ::= 'ON' event_name [ 'OF' ID ] [ where_clause ] action
               | 'ON' event_name [ 'OF' ID ] [ where_clause ] NEWLINE INDENT action { action } DEDENT
```

---

## 5. Mutation Statements

```
mutation_stmt ::= add_stmt | remove_stmt | set_stmt | update_stmt | delete_stmt

add_stmt     ::= 'ADD' ID 'TO' ID
remove_stmt  ::= 'REMOVE' ID 'FROM' ID
set_stmt     ::= 'SET' ID 'TO' expr                 -- e.g. SET ActiveContent TO HomePane
               | 'SET' DOTTED_ID 'TO' expr           -- e.g. SET NavyBlue.value TO #001F5B

swap_stmt    ::= 'SWAP' ID 'WITH' ID                -- [ATTESTED]
move_stmt    ::= 'MOVE' ID 'TO' DOTTED_ID           -- [ATTESTED] MOVE Item TO Target.Location
send_stmt    ::= 'SEND' ID 'TO' path                -- [ATTESTED] SEND InputField TO server/endpoint
path         ::= ID { '/' ID }
```

**Gap flagged:** the CantorDB-derived SeQL sibling language has a full CRUD surface (`DELETE SET`, `RENAME SET`, `UPDATE PROPERTY ... OF ... TO`) — but no example ever shows Naive itself using `DELETE` or `RENAME` directly (deletion in Naive is implicit, via membership-based GC — §8 of the design doc). Whether Naive exposes an explicit delete/rename verb at all, or relies entirely on "lose your last parent and get GC'd," was never addressed. This grammar omits `delete_stmt`/`update_stmt` production bodies for that reason — they're placeholders, not real rules.

---

## 6. Server / Network

```
load_stmt ::= 'LOAD' ID 'FROM' 'SERVER' 'AT' path
              'AS' ID
              [ member_clause ]
```

**Gap flagged:** only `LOAD ... FROM SERVER` (an implicit GET) and `SEND ... TO ...` (an implicit POST-ish send) are attested. Neither shows explicit `GET`/`POST` as verbs in the surface syntax, even though the design doc's Implementation Notes reference "`GET` and `POST` as built-in network sets" — that's describing the *runtime's* internal treatment (via `QUEUE`), not necessarily user-facing keywords. Whether a Naive author ever writes the literal word `GET` or `POST` themselves, or always goes through `LOAD`/`SEND`, is unresolved. Response error-handling syntax (what happens on a failed `LOAD`) is entirely unspecified — no `ON ERROR` or equivalent appears anywhere.

---

## 7. Functional Sets (control flow, recursion, Turing completeness)

```
function_decl ::= 'CREATE' ID 'AS' 'FUNCTION'
                   NEWLINE INDENT
                   [ takes_clause ]
                   { stmt_or_control }
                   DEDENT
                   'END'                              -- [ATTESTED — closing END appears in every function example]

takes_clause ::= 'TAKES' ID { ',' ID }                 -- [ATTESTED single-param; multi-param INFERRED]

stmt_or_control ::= mutation_stmt
                   | if_stmt
                   | return_stmt
                   | expr_stmt

if_stmt      ::= 'IF' expr 'THEN'
                 ( stmt_or_control                     -- single-line form: IF ... THEN X END  [ATTESTED]
                 | NEWLINE INDENT { stmt_or_control } DEDENT )
                 [ 'ELSE'
                   ( stmt_or_control
                   | NEWLINE INDENT { stmt_or_control } DEDENT ) ]
                 'END'

return_stmt  ::= 'RETURN' expr
call_expr    ::= 'CALL' ID 'WITH' '(' [ expr { ',' expr } ] ')'
```

**Gap flagged, explicitly:** `ELSE` was never once demonstrated in prior examples — every `IF` example is a bare guard (`IF N.value <= 1 THEN RETURN 1 END`) — but is now a confirmed decision rather than a proposal, per §8. Likewise `AND`/`OR`/`NOT` boolean connectives are now confirmed rather than invented.

**No loop keyword in prior sessions** [now resolved]: iteration previously had no attested surface syntax at all, only the claim that recursion/ordered-set traversal was sufficient. Two loop forms are now specified:

```
for_stmt   ::= 'FOR' ID 'IN' ID
               NEWLINE INDENT { stmt_or_control } DEDENT
               'END'

while_stmt ::= 'WHILE' expr
               NEWLINE INDENT { stmt_or_control } DEDENT
               'END'
```

`FOR item IN MySet` binds `item` to each member of `MySet` in turn, in canonical/ordered-set order for an ordered set, or in an unspecified-but-consistent order for an unordered one — consistent with the traversal model already described for ordered sets. `WHILE` re-evaluates its condition each iteration, same as any C-family language. Both close with `END`, consistent with the function/`IF` convention rather than the dedent-only convention used for set/event blocks.

Add these to `stmt_or_control`:
```
stmt_or_control ::= mutation_stmt
                   | if_stmt
                   | for_stmt
                   | while_stmt
                   | return_stmt
                   | expr_stmt
```

---

## 8. Expressions

Operator precedence is C's, low to high — this is now a decision, not a guess:

1. `OR`
2. `AND`
3. `NOT` (unary)
4. Equality: `=`, `<>`
5. Relational: `<`, `>`, `<=`, `>=`
6. Additive: `+`, `-`
7. Multiplicative: `*`, `/`, `%`
8. Unary `-`
9. Primary (literals, calls, parens)

```
expr           ::= or_expr
or_expr        ::= and_expr { 'OR' and_expr }
and_expr       ::= not_expr { 'AND' not_expr }
not_expr       ::= [ 'NOT' ] equality
equality       ::= relational { ('=' | '<>') relational }
relational     ::= additive { ('<' | '>' | '<=' | '>=') additive }
additive       ::= multiplicative { ('+' | '-') multiplicative }
multiplicative ::= unary { ('*' | '/' | '%') unary }
unary          ::= [ '-' ] primary
primary        ::= NUMBER
                  | STRING_LIT
                  | COLOR_LIT
                  | DOTTED_ID
                  | ID
                  | call_expr
                  | '(' expr ')'
                  | is_expr

is_expr        ::= ID 'IS' [ 'NOT' ] ID                              -- [ATTESTED] TARGET IS Target / TARGET IS NOT NavItem
                  | ID 'IS' 'NOT' 'EMPTY'                             -- [ATTESTED] InputField IS NOT EMPTY
```

`%` (modulo) is added for parity with C even though no prior example used it — arithmetic on set sizes/indices (e.g. alternating row striping in a table widget) will likely need it. `ELSE`, `AND`, `OR`, `NOT` are now confirmed language features rather than proposals.

---

## 9. FLAT Sets and Forced Schema Sets

**FLAT — resolved.** `FLAT` is a modifier keyword between `CREATE` and the identifier being declared (see §3), not a value in the `AS` slot:

```
CREATE FLAT Button AS SET
    MEMBER OF Clickable, Draggable
```

Any element later added to `Button` is automatically added to `Clickable` and `Draggable` too — flatness propagates membership upward through whatever supersets are established via the ordinary `member_clause`, no separate mechanism required beyond the `FLAT` modifier itself and the existing `MEMBER OF` list (extended below to accept multiple names).

Grammar change from the original sketch — `create_stmt` gains an optional `FLAT` modifier, and `member_clause` now accepts a list rather than a single superset:

```
create_stmt   ::= 'CREATE' [ 'FLAT' ] ID 'AS' type_name ...   -- see §3
member_clause ::= 'MEMBER' 'OF' ID { ',' ID }                  -- was single-superset only
```

This reuses machinery that already exists (`AS <type>` for the real type, `MEMBER OF <set>` for supersets) rather than inventing a dedicated clause the way the earlier sketch guessed at.

**Forced schema sets — proposed syntax.**

The semantics to capture: `Clickable` requires that its members, when checked against `FUNCTION`, must *contain* a function rather than *be* one. The cleanest way to express this reuses the existing declarative style rather than inventing new machinery — a `REQUIRES` clause on `create_stmt`, with `MEMBER` as a placeholder standing for "whatever element is being inserted":

```
CREATE Clickable AS SET
    REQUIRES MEMBER HAS FUNCTION
```

```
schema_clause    ::= 'REQUIRES' requirement_expr
requirement_expr ::= requirement { ( 'AND' | 'OR' ) requirement }
requirement       ::= 'MEMBER' 'HAS' ID       -- candidate must itself CONTAIN a member of ID
                     | 'MEMBER' 'IS' ID        -- candidate must itself BE a member of ID
                     | 'NOT' requirement
```

`HAS` versus `IS` does the actual work here, and it's why a separate `NOT` clause isn't needed for the canonical `DoThing` case: `MEMBER HAS FUNCTION` asks "does this candidate *contain* something in `FUNCTION` among its own members?" — which is naturally false for a bare `Function`-typed set like `DoThing` added directly, since `DoThing` *is* a function rather than containing one. The distinction the original design cared about (have vs. be) falls directly out of `HAS` vs. `IS` as two different tests, rather than needing an explicit exclusion.

`create_stmt` gains the clause:
```
create_stmt ::= 'CREATE' [ 'FLAT' ] ID 'AS' type_name
                [ with_clause ]
                [ at_clause ]
                [ member_clause ]
                [ schema_clause ]
                { event_clause }
```

Checked at insertion time (§8 of the design doc — schema violations scream when something is added, not at declaration time), and — per the earlier worked example — a FLAT propagation that would carry a violating element up into a schema-bearing superset fails the whole insertion, not just the propagation step.

This is a proposal, not something you've confirmed the way FLAT's position now is — flag it back if `REQUIRES`/`HAS` reads wrong, particularly since `HAS` is a brand-new reserved word not used anywhere else in the language so far.

**Worked trace, to sanity-check it against the canonical example:**

```
CREATE Clickable AS SET
    REQUIRES MEMBER HAS FUNCTION

CREATE FLAT Button AS SET
    MEMBER OF Clickable

CREATE DoThing AS FUNCTION

ADD DoThing TO Button
```

1. `DoThing` is added to `Button`.
2. `Button` is `FLAT` with superset `Clickable`, so `DoThing` propagates upward toward `Clickable`.
3. Before that propagation completes, `Clickable`'s schema checks the candidate: does `DoThing` `HAS FUNCTION` — i.e. does `DoThing` itself contain a member that's in `FUNCTION`? No — `DoThing` *is* a `FUNCTION`, it doesn't contain one.
4. The `REQUIRES` clause fails. Per the design doc's insertion-time rollback rule, the entire `ADD DoThing TO Button` is rejected — `DoThing` never lands in `Button` either, since the violation was only surfaced by the propagation the insertion would have triggered.

That matches the semantics from the design doc exactly, so the proposed syntax holds up under its own worked example rather than just looking plausible in isolation.

---

## 10. Summary Table — Attestation Status by Feature

| Feature | Status |
|---|---|
| `CREATE ... AS ... WITH ... AT ...` | Attested |
| `ON <event> [WHERE ...] <action>` | Attested |
| `ADD/REMOVE ... TO/FROM ...` | Attested |
| `SET ... TO ...` | Attested |
| `SWAP`, `MOVE`, `SEND` | Attested |
| `LOAD ... FROM SERVER ...` | Attested |
| `CREATE ... AS FUNCTION / TAKES / IF...THEN...END / RETURN / CALL...WITH` | Attested |
| Block scoping: set/event blocks by indentation, everything else by `END` | **Decided** |
| Operator precedence (C-style) | **Decided** |
| `ELSE`, `AND`, `OR`, `NOT` | **Decided** |
| `FOR item IN Set ... END` | **Decided** |
| `WHILE expr ... END` | **Decided** |
| `CREATE FLAT X AS type [MEMBER OF Set1, Set2, ...]` | **Decided** |
| Comments (`--` to end of line) | Invented, unconfirmed |
| `REQUIRES MEMBER HAS/IS <Set>` schema clause | **Proposed, unconfirmed** — checked against the canonical worked example and holds up, but you haven't signed off on it the way you did FLAT |
| Error handling (`ON ERROR`, failed `LOAD`) | Missing entirely |
| Explicit `DELETE`/`RENAME` verbs vs. GC-only deletion | Missing entirely |

---

*Block scoping, precedence, `ELSE`/`AND`/`OR`/`NOT`, both loop forms, and FLAT are now settled. Forced-schema syntax now has a concrete, self-consistent proposal (`REQUIRES MEMBER HAS/IS <Set>`) — the last thing to do with it is your sign-off, the same way FLAT's position got corrected once you saw the first draft.*
