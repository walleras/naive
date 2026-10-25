# Naive: a quick coding guide

Naive pages are plain text files ending in `.nve`. This guide covers everything
you need to write one. Run a page with:

```sh
build/naive.exe examples/hello.nve
```

Comments start with `--`. Keywords and names are case-insensitive (`Button`,
`button` and `BUTTON` are the same), and indentation is for humans except where
a block is introduced by a `CREATE` or `ON` line (see below).

## 1. Everything is `CREATE`

```nve
CREATE Greeting AS TextBox
    WITH text = "Hello, World"
    AT X.Center, Y.Center
```

`CREATE <name> AS <type>` makes a set. Three kinds of clause follow it, on the
same line or on indented lines below:

| Clause | Meaning |
|---|---|
| `WITH a = 1, b = "x"` | properties |
| `AT X.<where>, Y.<where>` | position |
| `MEMBER OF A, B` | put it in those sets |

`AS Button, Blue` is shorthand for `AS Button` plus `MEMBER OF Blue`.

Names are global and unique. A name used before it is created (a forward
reference) just works.

### Types

| Type | Draws |
|---|---|
| `TextBox` | a line of text |
| `H1` ... `H6` (`Heading`) | a heading |
| `P` (`Paragraph`), `HR` (`HRule`) | a paragraph, a horizontal rule |
| `Button` | a button (`label`) |
| `TextInput`, `Textarea` | a text field, a multi-line field (`rows`) |
| `Checkbox`, `Radio` | a checkbox, a radio button (`group` makes radios exclusive) |
| `Select` and `Option` | a dropdown and its choices |
| `Image` | a bitmap (`src`, `width`, `height`) |
| `UL`/`OL`/`List`, `LI`/`ListItem` | lists |
| `Table`, `Row`, `Column` | tables (first row is the header) |
| `SET` | an invisible container, just a set |
| `FUNCTION` | a function (section 6) |
| `Color` | a custom colour set |

### Properties

`text` / `label` / `content` (what is shown), `placeholder`, `link` (makes the widget
a hyperlink), `action` (a Button that submits a form), `src`, `value`, `name`,
`checked`, `selected`, `rows`, and the layout ones: `width`, `height`, `align`
(`"left"`, `"center"`, `"right"`), `bgcolor = #RRGGBB`, `color = #RRGGBB`.

Strings are `"double quoted"` with no escapes. Colours are `#RRGGBB`.

### Position

`X.Left`, `X.Center`, `X.Right`, `X.<pixels>`, and `Y.Top`, `Y.Center`,
`Y.Bottom`, `Y.<pixels>`. `Y.0` is just under the navigation bar.

## 2. State is membership

There are no booleans and no flags. Whether something is shown, what colour it
is and what font it uses are all set membership.

```nve
ADD Greeting TO VISIBLE        -- show it
REMOVE Greeting FROM VISIBLE   -- hide it
ADD Greeting TO RED            -- red text
ADD Greeting TO BOLD           -- bold
MOVE Greeting FROM RED TO BLUE -- leave one set, join another
```

- **Visibility.** A widget with no `MEMBER OF` is shown automatically. A widget in a
  container set is shown when the container is a member of `VISIBLE`, so hiding the
  container hides everything in it, and one `ADD box TO VISIBLE` switches a whole
  panel on.
- **Colours** are sets: `BLACK WHITE RED GREEN BLUE CYAN MAGENTA YELLOW GRAY` and
  `DARK_` versions of the colours and gray. All of them are members of `COLORS`.
- **Styles** are sets: `BOLD ITALICS UNDERLINE STRIKETHROUGH MONOSPACE`.
- `W IS S` is true when `W` is a member of `S`.

## 3. Events

```nve
CREATE Hide AS Button
    WITH label = "Hide"
    AT X.Center, Y.300
    ON CLICK REMOVE Greeting FROM VISIBLE
```

Events that fire: `CLICK`, `TYPE` (after each character typed), `CHANGE` (a field was
committed with Enter, or a value was `SET`) and `LOAD` (once, when the page opens).
A handler can have several actions on indented lines below it, and a guard:

```nve
ON CHANGE OF FOCUS WHERE Field IS FOCUS
    Report()
```

`FOCUS` is a built-in set holding whichever input has keyboard focus.

### Actions

| Action | Does |
|---|---|
| `ADD a TO b` / `REMOVE a FROM b` | change membership |
| `MOVE a FROM b TO c` | leave `b`, join `c` |
| `SET a TO <expr>` | set a's value |
| `SET a.prop TO <expr>` | set a property (e.g. `SET Label.bgcolor TO #FFEEAA`) |
| `SWAP a WITH b` | rebind `a` to hold `b` |
| `f(args)` or `CALL f WITH (args)` | call a function |

A widget that has a `value` shows the value instead of its `text`, so
`SET Counter TO 5` changes what a `TextBox` displays.

## 4. Expressions and formulas

Arithmetic `+ - * / %`, comparison `< <= > >= = == <> ~= !=`, logic `and or not`,
string join `..`, membership `a IS b`, and `a.value`. Numbers print without
trailing zeros.

Any widget whose `content` starts with `=` is a **live formula**:

```nve
CREATE A AS TextInput WITH content = "5"   AT X.20, Y.20
CREATE B AS TextInput WITH content = "7"   AT X.20, Y.50
CREATE Sum AS TextBox WITH content = "=A+B" AT X.20, Y.80
```

Edit `A` and press Enter and `Sum` updates. The formula's references become its
members, so changes propagate with no handler written.

`SUM`, `AVG`, `MIN`, `MAX` and `COUNT` work over the members of a set, which is
how ranges are written:

```nve
CREATE Prices AS SET
CREATE P1 AS TextInput WITH content = "10" AT X.20, Y.120 MEMBER OF Prices
CREATE P2 AS TextInput WITH content = "20" AT X.20, Y.150 MEMBER OF Prices
CREATE Total AS TextBox WITH content = "=SUM(Prices)" AT X.20, Y.180
```

## 5. Forms

Put the inputs in a set, give the button an `action`, and add the set to the
button:

```nve
CREATE Contact AS SET
CREATE NameF AS TextInput WITH placeholder = "Name", name = "name"
    AT X.Left, Y.60 MEMBER OF Contact
CREATE Go AS Button WITH label = "Send", action = "/submit" AT X.Left, Y.100
ADD Contact TO Go
```

Clicking `Go` posts the fields (`name=...&...`) to `/submit`, which echoes them
back as a page.

## 6. Functions

Function bodies use Lua-style control flow, with Naive verbs for the actions:

```nve
CREATE fact AS FUNCTION
    TAKES n
    IF n.value <= 1 THEN RETURN 1 END
    RETURN n.value * fact(n.value - 1)
END

CREATE sum_to AS FUNCTION
    TAKES n
    LOCAL total = 0
    LOCAL i = 1
    WHILE i <= n.value DO
        total = total.value + i.value
        i = i.value + 1
    END
    RETURN total.value
END

SET Result TO "10! = " .. fact(10)
```

- `TAKES a, b` declares parameters. A set name is passed as that very set (so a
  function can `ADD`/`REMOVE` it); a number or string becomes a fresh set holding
  that value, read with `.value`.
- `IF ... THEN ... ELSEIF ... ELSE ... END`, `WHILE ... DO ... END`, `LOCAL x = ...`,
  `x = ...` (assignment), `RETURN [expr]`.
- Calls: `f(a, b)` or `CALL f WITH (a, b)`, as a statement or inside an expression.
- Runaway loops and recursion are cut off, not hung.

**Bind a function to an event** by listing the event and the widget after the type:

```nve
CREATE flip AS FUNCTION, OnClick, Toggler
    TAKES w
    IF w IS BLUE THEN
        MOVE w FROM BLUE TO RED
    ELSE
        MOVE w FROM RED TO BLUE
    END
END
```

`OnClick`, `OnLoad` and `OnChange` are the ones that fire. With no widget, as in
`CREATE boot AS FUNCTION, OnLoad`, the function runs when the page loads.

## 7. Pages, links and the mock internet

The viewer reads pages from `www/` as if it were a server:

| Address | Served from |
|---|---|
| `/` | `www/index.nve` |
| `/docs` or `/docs.nve` | `www/docs.nve` |
| `/examples/hello.nve` | `www/examples/hello.nve` |
| `xyz.com` | `www/xyz/index.nve` |
| `xyz.com/about` | `www/xyz/about.nve` |

Make a hyperlink with a `link` property; the address bar and the back/forward
buttons work as in a browser.

```nve
CREATE ToDocs AS TextBox
    WITH text = "Read the docs", link = "/docs"
    AT X.Center, Y.100
```

To use a fixed canvas that scales with the window, put `DEFINE GRID X 800, Y 1400`
at the top; `X.n` and `Y.n` then count in that virtual space, and content past the
window scrolls.

## 8. Where to look next

- `examples/hello.nve` is the smallest page; `examples/demo.nve` shows tabs, a counter,
  colours and recursive/iterative maths; `examples/showcase.nve` has every element;
  `examples/functions.nve` and `examples/form.nve` cover sections 6 and 5.
- `naive_design_doc.md` is the thinking behind the language, including parts not
  built yet. `naive_grammar_sketch.md` is the grammar.
