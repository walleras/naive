#!/usr/bin/perl
# Generates examples/spreadsheet.nve. The sheet is plain Naive: cells are widgets,
# ranges are SETS (Row2, ColB, All), formulas are ordinary "=..." content, and
# the selection UI (name box, formula bar, header highlight) is driven by the
# built-in FOCUS set. Layout uses the generic width / height / align / bgcolor
# widget properties. Nothing here is spreadsheet-specific in the engine.
use strict;

my @cols  = ('B', 'C', 'D');          # editable numeric columns
my @items = (
  ['Widgets',      120, 135, 150],
  ['Gadgets',       80,  95, 110],
  ['Gizmos',       200, 180, 210],
  ['Doohickeys',    45,  60,  55],
  ['Thingamajigs', 310, 295, 330],
);

# ---- geometry (pixels) -------------------------------------------------------
my $HDR_W   = 34;                     # row-number column (spacing)
my $X0      = 16;                     # left edge of the grid
my %colx    = (A => $X0 + $HDR_W);    # A starts after the row numbers
my %colw    = (A => 130, B => 110, C => 110, D => 110, E => 110);   # spacing
$colx{B} = $colx{A} + $colw{A};
$colx{C} = $colx{B} + $colw{B};
$colx{D} = $colx{C} + $colw{C};
$colx{E} = $colx{D} + $colw{D};
my $GRID_W  = $HDR_W + $colw{A} + $colw{B} + $colw{C} + $colw{D} + $colw{E};
my $TOP     = 68;                     # top of the column-header strip
my $HDR_H   = 22;                     # column-header spacing
my $ROW_H   = 24;                     # row spacing
my $NROWS   = 12;
sub rowy { my $r = shift; return $TOP + $HDR_H + $ROW_H * ($r - 1); }
my $GRID_H  = $HDR_H + $ROW_H * $NROWS;

my $HDR_BG  = '#F1F3F4';              # header fill
my $HDR_SEL = '#D3E3FD';              # selected header fill
my $CELL_BG = '#FFFFFF';
my $CELL_SEL= '#E8F0FE';
my $LINE    = '#D0D3D6';              # gridlines (the plate showing through)

my $o = '';
sub out { $o .= join('', @_); }

sub cw { my $c = shift; return $colw{$c} - 1; }     # cell width inside the 1px gap

# which rows hold what
my @scratch  = (10, 11, 12);
my @datarows = (2 .. 6);
my @sumrows  = ([7, 'Total', 'SUM'], [8, 'Average', 'AVG'], [9, 'Max', 'MAX']);

# TextBox cells that pick() must reset to white (everything that is not a TextInput)
my @boxcells;
push @boxcells, "E$_" for (@datarows, @scratch);
for my $s (@sumrows) { push @boxcells, "$_$s->[0]" for (@cols, 'E'); }

out <<'H';
-- spreadsheet.nve -- a spreadsheet written entirely in Naive.
--
-- There is nothing spreadsheet-specific in the engine. This page is made of:
--   * ordinary widgets: TextInput cells you can edit, TextBox cells that hold
--     formulas, and TextBox row/column headers, sized with the generic
--     width / height / align / bgcolor properties (the gridlines are just a
--     gray TextBox behind the cells, showing through 1px gaps);
--   * ranges that are plain SETS (Row2, ColB, All): a cell joins the ranges it
--     belongs to with MEMBER OF, so SUM(ColB) is just "sum the members of ColB";
--   * formulas: any cell's content that starts with '=' is a live formula
--     (dependency-as-membership + native delta propagation), so editing a cell
--     recomputes everything downstream with no handler written for it;
--   * selection: the built-in FOCUS set holds whichever cell you are editing.
--     "ON CHANGE OF FOCUS WHERE B2 IS FOCUS" calls pick(), which tints the
--     headers, fills the name box, and shows the raw formula in the bar.
--
-- Click a cell and type a number, or a formula such as =B2+C2, =SUM(All),
-- =AVG(ColB), =MAX(Row3); press Enter. The Total column and the bottom rows
-- are formulas too: click one to read it in the formula bar.

H

# ---- title + name box + formula bar ----------------------------------------
my $bar_w = $GRID_W - 114;
out <<"H";
CREATE Title AS TextBox
    WITH text = "Naive Sheet"
    AT X.$X0, Y.6
    MEMBER OF BOLD

CREATE Hint AS TextBox
    WITH text = "Click a cell, type a number or a formula, press Enter."
    AT X.150, Y.6
    MEMBER OF GRAY

CREATE NamePlate AS TextBox
    WITH text = "", bgcolor = $LINE, width = 78, height = 26
    AT X.$X0, Y.34

CREATE NameBox AS TextBox
    WITH text = "", bgcolor = $CELL_BG, width = 76, height = 24, align = "center"
    AT X.@{[$X0 + 1]}, Y.35
    MEMBER OF BOLD

CREATE FxLabel AS TextBox
    WITH text = "fx", width = 30, height = 26, align = "center"
    AT X.@{[$X0 + 82]}, Y.34
    MEMBER OF ITALICS, GRAY

CREATE BarPlate AS TextBox
    WITH text = "", bgcolor = $LINE, width = @{[$bar_w + 2]}, height = 26
    AT X.@{[$X0 + 114]}, Y.34

CREATE FormulaBar AS TextBox
    WITH text = "", bgcolor = $CELL_BG, width = $bar_w, height = 24
    AT X.@{[$X0 + 115]}, Y.35

H

# ---- ranges (sets) ----------------------------------------------------------
out "-- Ranges are sets. Each data cell below joins its row, its column and All.\n";
out "CREATE Sheet AS SET\n";
out "CREATE All AS SET\n";
out "CREATE Row$_ AS SET\n" for (@datarows, @scratch);
out "CREATE Col$_ AS SET\n" for (@cols, 'E');
out "\n";

# ---- gridline plate (drawn first, so the cells sit on top of it) -----------
out <<"H";
-- The gridlines: one gray plate behind everything, visible through 1px gaps.
CREATE GridPlate AS TextBox
    WITH text = "", bgcolor = $LINE, width = $GRID_W, height = $GRID_H
    AT X.$X0, Y.$TOP

H

# ---- headers ----------------------------------------------------------------
out "-- Column and row headers (pick() tints the selected cell's pair).\n";
out "CREATE Hdr0 AS TextBox\n    WITH text = \"\", bgcolor = $HDR_BG, width = " . ($HDR_W - 1) . ", height = " . ($HDR_H - 1) . "\n    AT X.$X0, Y.$TOP\n";
for my $c ('A', @cols, 'E') {
    out "CREATE Hdr$c AS TextBox\n    WITH text = \"$c\", bgcolor = $HDR_BG, width = " . cw($c) . ", height = " . ($HDR_H - 1) . ", align = \"center\"\n    AT X.$colx{$c}, Y.$TOP\n    MEMBER OF GRAY\n";
}
for my $r (1 .. $NROWS) {
    out "CREATE Hdr$r AS TextBox\n    WITH text = \"$r\", bgcolor = $HDR_BG, width = " . ($HDR_W - 1) . ", height = " . ($ROW_H - 1) . ", align = \"center\"\n    AT X.$X0, Y." . rowy($r) . "\n    MEMBER OF GRAY\n";
}
out "\n";

# ---- pick(): the whole selection UI ----------------------------------------
out "-- pick(name, cell, columnHeader, rowHeader): tint the headers, fill the name\n";
out "-- box, and show the cell's raw content (the formula) in the formula bar.\n";
out "CREATE pick AS FUNCTION\n    TAKES name, cell, ch, rh\n";
out "    SET Hdr$_.bgcolor TO $HDR_BG\n" for ('A', @cols, 'E', 1 .. $NROWS);
out "    SET $_.bgcolor TO $CELL_BG\n" for @boxcells;
out "    SET ch.bgcolor TO $HDR_SEL\n    SET rh.bgcolor TO $HDR_SEL\n    SET cell.bgcolor TO $CELL_SEL\n";
out "    SET NameBox TO name.value\n    SET FormulaBar TO cell.content\nEND\n\n";

# helper emitting a cell's selection wiring
sub wiring {
    my ($name, $col, $row, $is_input) = @_;
    my $call = "pick(\"$name\", $name, Hdr$col, Hdr$row)";
    if ($is_input) {
        return "    ON CHANGE OF FOCUS WHERE $name IS FOCUS\n        $call\n" .
               "    ON TYPE\n        $call\n" .
               "    ON CHANGE\n        $call\n";
    }
    return "    ON CLICK\n        $call\n";
}

sub box {   # a TextBox cell
    my ($name, $col, $row, $props, $members, $wire) = @_;
    return "CREATE $name AS TextBox\n    WITH $props, bgcolor = $CELL_BG, width = " . cw($col) .
           ", height = " . ($ROW_H - 1) . "\n    AT X.$colx{$col}, Y." . rowy($row) . "\n" .
           (($members ne '') ? "    MEMBER OF $members\n" : '') . $wire;
}
sub input { # a TextInput cell
    my ($name, $col, $row, $props, $members) = @_;
    return "CREATE $name AS TextInput\n    WITH $props, width = " . cw($col) . ", height = " . ($ROW_H - 1) .
           ", align = \"right\"\n    AT X.$colx{$col}, Y." . rowy($row) . "\n    MEMBER OF $members\n" .
           wiring($name, $col, $row, 1);
}

# ---- row 1: labels ----------------------------------------------------------
out "-- Row 1: labels\n";
out box('A1', 'A', 1, 'text = "Item"', 'BOLD', '');
for my $l (['B', 'Q1'], ['C', 'Q2'], ['D', 'Q3'], ['E', 'Total']) {
    out box("$l->[0]1", $l->[0], 1, "text = \"$l->[1]\", align = \"right\"", 'BOLD', '');
}
out "\n";

# ---- data rows --------------------------------------------------------------
out "-- Rows 2-6: data you can edit, with a row total in column E.\n";
for my $i (0 .. $#items) {
    my $r = $i + 2;
    my @it = @{ $items[$i] };
    out box("A$r", 'A', $r, "text = \"$it[0]\"", '', '');
    for my $k (0 .. 2) {
        my $c = $cols[$k];
        out input("$c$r", $c, $r, "content = \"$it[$k+1]\"", "Sheet, Row$r, Col$c, All");
    }
    out box("E$r", 'E', $r, "content = \"=SUM(Row$r)\", align = \"right\"", 'Sheet, ColE', wiring("E$r", 'E', $r, 0));
    out "\n";
}

# ---- summary rows -----------------------------------------------------------
out "-- Rows 7-9: formulas over the column ranges (TextBoxes: click one to read it).\n";
for my $s (@sumrows) {
    my ($r, $label, $fn) = @$s;
    out box("A$r", 'A', $r, "text = \"$label\"", 'BOLD', '');
    for my $c (@cols, 'E') {
        out box("$c$r", $c, $r, "content = \"=$fn(Col$c)\", align = \"right\"", 'Sheet', wiring("$c$r", $c, $r, 0));
    }
    out "\n";
}

# ---- scratch rows -----------------------------------------------------------
out "-- Rows 10-12: empty cells to try formulas in (try =SUM(All) or =B2*1.1). Column E totals each row.\n";
for my $r (@scratch) {
    out box("A$r", 'A', $r, ($r == 10) ? 'text = "Scratch"' : 'text = ""', ($r == 10) ? 'ITALICS' : '', '');
    for my $c (@cols) {
        out input("$c$r", $c, $r, 'placeholder = ""', "Sheet, Row$r");
    }
    out box("E$r", 'E', $r, "content = \"=SUM(Row$r)\", align = \"right\"", 'Sheet', wiring("E$r", 'E', $r, 0));
    out "\n";
}

# ---- bottom: ranges note + sheet tabs --------------------------------------
my $BAR_Y = $TOP + $GRID_H + 52;
my $NOTE_Y = $TOP + $GRID_H + 14;
out <<"H";
CREATE Notes AS TextBox
    WITH text = "Ranges are sets you can use in any formula: Row2..Row6, Row10..Row12, ColB..ColE, All."
    AT X.$X0, Y.$NOTE_Y
    MEMBER OF GRAY

-- the sheet-tab bar along the bottom
CREATE TabBar AS TextBox
    WITH text = "", bgcolor = $HDR_BG, width = $GRID_W, height = 28
    AT X.$X0, Y.$BAR_Y

CREATE Home AS TextBox
    WITH text = "<- Home", link = "/", height = 26
    AT X.@{[$X0 + 6]}, Y.@{[$BAR_Y + 1]}

CREATE SheetTab AS TextBox
    WITH text = "Sheet1", bgcolor = $CELL_BG, width = 90, height = 26, align = "center"
    AT X.150, Y.@{[$BAR_Y + 1]}
    MEMBER OF BOLD, BLUE

-- the cells live in Sheet, so showing Sheet shows them all
ADD Sheet TO VISIBLE
H

print $o;
