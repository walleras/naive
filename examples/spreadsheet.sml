-- spreadsheet.sml -- a spreadsheet written entirely in SML.
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

CREATE Title AS TextBox
    WITH text = "SeSaML Sheet"
    AT X.16, Y.6
    MEMBER OF BOLD

CREATE Hint AS TextBox
    WITH text = "Click a cell, type a number or a formula, press Enter."
    AT X.150, Y.6
    MEMBER OF GRAY

CREATE NamePlate AS TextBox
    WITH text = "", bgcolor = #D0D3D6, width = 78, height = 26
    AT X.16, Y.34

CREATE NameBox AS TextBox
    WITH text = "", bgcolor = #FFFFFF, width = 76, height = 24, align = "center"
    AT X.17, Y.35
    MEMBER OF BOLD

CREATE FxLabel AS TextBox
    WITH text = "fx", width = 30, height = 26, align = "center"
    AT X.98, Y.34
    MEMBER OF ITALICS, GRAY

CREATE BarPlate AS TextBox
    WITH text = "", bgcolor = #D0D3D6, width = 492, height = 26
    AT X.130, Y.34

CREATE FormulaBar AS TextBox
    WITH text = "", bgcolor = #FFFFFF, width = 490, height = 24
    AT X.131, Y.35

-- Ranges are sets. Each data cell below joins its row, its column and All.
CREATE Sheet AS SET
CREATE All AS SET
CREATE Row2 AS SET
CREATE Row3 AS SET
CREATE Row4 AS SET
CREATE Row5 AS SET
CREATE Row6 AS SET
CREATE Row10 AS SET
CREATE Row11 AS SET
CREATE Row12 AS SET
CREATE ColB AS SET
CREATE ColC AS SET
CREATE ColD AS SET
CREATE ColE AS SET

-- The gridlines: one gray plate behind everything, visible through 1px gaps.
CREATE GridPlate AS TextBox
    WITH text = "", bgcolor = #D0D3D6, width = 604, height = 310
    AT X.16, Y.68

-- Column and row headers (pick() tints the selected cell's pair).
CREATE Hdr0 AS TextBox
    WITH text = "", bgcolor = #F1F3F4, width = 33, height = 21
    AT X.16, Y.68
CREATE HdrA AS TextBox
    WITH text = "A", bgcolor = #F1F3F4, width = 129, height = 21, align = "center"
    AT X.50, Y.68
    MEMBER OF GRAY
CREATE HdrB AS TextBox
    WITH text = "B", bgcolor = #F1F3F4, width = 109, height = 21, align = "center"
    AT X.180, Y.68
    MEMBER OF GRAY
CREATE HdrC AS TextBox
    WITH text = "C", bgcolor = #F1F3F4, width = 109, height = 21, align = "center"
    AT X.290, Y.68
    MEMBER OF GRAY
CREATE HdrD AS TextBox
    WITH text = "D", bgcolor = #F1F3F4, width = 109, height = 21, align = "center"
    AT X.400, Y.68
    MEMBER OF GRAY
CREATE HdrE AS TextBox
    WITH text = "E", bgcolor = #F1F3F4, width = 109, height = 21, align = "center"
    AT X.510, Y.68
    MEMBER OF GRAY
CREATE Hdr1 AS TextBox
    WITH text = "1", bgcolor = #F1F3F4, width = 33, height = 23, align = "center"
    AT X.16, Y.90
    MEMBER OF GRAY
CREATE Hdr2 AS TextBox
    WITH text = "2", bgcolor = #F1F3F4, width = 33, height = 23, align = "center"
    AT X.16, Y.114
    MEMBER OF GRAY
CREATE Hdr3 AS TextBox
    WITH text = "3", bgcolor = #F1F3F4, width = 33, height = 23, align = "center"
    AT X.16, Y.138
    MEMBER OF GRAY
CREATE Hdr4 AS TextBox
    WITH text = "4", bgcolor = #F1F3F4, width = 33, height = 23, align = "center"
    AT X.16, Y.162
    MEMBER OF GRAY
CREATE Hdr5 AS TextBox
    WITH text = "5", bgcolor = #F1F3F4, width = 33, height = 23, align = "center"
    AT X.16, Y.186
    MEMBER OF GRAY
CREATE Hdr6 AS TextBox
    WITH text = "6", bgcolor = #F1F3F4, width = 33, height = 23, align = "center"
    AT X.16, Y.210
    MEMBER OF GRAY
CREATE Hdr7 AS TextBox
    WITH text = "7", bgcolor = #F1F3F4, width = 33, height = 23, align = "center"
    AT X.16, Y.234
    MEMBER OF GRAY
CREATE Hdr8 AS TextBox
    WITH text = "8", bgcolor = #F1F3F4, width = 33, height = 23, align = "center"
    AT X.16, Y.258
    MEMBER OF GRAY
CREATE Hdr9 AS TextBox
    WITH text = "9", bgcolor = #F1F3F4, width = 33, height = 23, align = "center"
    AT X.16, Y.282
    MEMBER OF GRAY
CREATE Hdr10 AS TextBox
    WITH text = "10", bgcolor = #F1F3F4, width = 33, height = 23, align = "center"
    AT X.16, Y.306
    MEMBER OF GRAY
CREATE Hdr11 AS TextBox
    WITH text = "11", bgcolor = #F1F3F4, width = 33, height = 23, align = "center"
    AT X.16, Y.330
    MEMBER OF GRAY
CREATE Hdr12 AS TextBox
    WITH text = "12", bgcolor = #F1F3F4, width = 33, height = 23, align = "center"
    AT X.16, Y.354
    MEMBER OF GRAY

-- pick(name, cell, columnHeader, rowHeader): tint the headers, fill the name
-- box, and show the cell's raw content (the formula) in the formula bar.
CREATE pick AS FUNCTION
    TAKES name, cell, ch, rh
    SET HdrA.bgcolor TO #F1F3F4
    SET HdrB.bgcolor TO #F1F3F4
    SET HdrC.bgcolor TO #F1F3F4
    SET HdrD.bgcolor TO #F1F3F4
    SET HdrE.bgcolor TO #F1F3F4
    SET Hdr1.bgcolor TO #F1F3F4
    SET Hdr2.bgcolor TO #F1F3F4
    SET Hdr3.bgcolor TO #F1F3F4
    SET Hdr4.bgcolor TO #F1F3F4
    SET Hdr5.bgcolor TO #F1F3F4
    SET Hdr6.bgcolor TO #F1F3F4
    SET Hdr7.bgcolor TO #F1F3F4
    SET Hdr8.bgcolor TO #F1F3F4
    SET Hdr9.bgcolor TO #F1F3F4
    SET Hdr10.bgcolor TO #F1F3F4
    SET Hdr11.bgcolor TO #F1F3F4
    SET Hdr12.bgcolor TO #F1F3F4
    SET E2.bgcolor TO #FFFFFF
    SET E3.bgcolor TO #FFFFFF
    SET E4.bgcolor TO #FFFFFF
    SET E5.bgcolor TO #FFFFFF
    SET E6.bgcolor TO #FFFFFF
    SET E10.bgcolor TO #FFFFFF
    SET E11.bgcolor TO #FFFFFF
    SET E12.bgcolor TO #FFFFFF
    SET B7.bgcolor TO #FFFFFF
    SET C7.bgcolor TO #FFFFFF
    SET D7.bgcolor TO #FFFFFF
    SET E7.bgcolor TO #FFFFFF
    SET B8.bgcolor TO #FFFFFF
    SET C8.bgcolor TO #FFFFFF
    SET D8.bgcolor TO #FFFFFF
    SET E8.bgcolor TO #FFFFFF
    SET B9.bgcolor TO #FFFFFF
    SET C9.bgcolor TO #FFFFFF
    SET D9.bgcolor TO #FFFFFF
    SET E9.bgcolor TO #FFFFFF
    SET ch.bgcolor TO #D3E3FD
    SET rh.bgcolor TO #D3E3FD
    SET cell.bgcolor TO #E8F0FE
    SET NameBox TO name.value
    SET FormulaBar TO cell.content
END

-- Row 1: labels
CREATE A1 AS TextBox
    WITH text = "Item", bgcolor = #FFFFFF, width = 129, height = 23
    AT X.50, Y.90
    MEMBER OF BOLD
CREATE B1 AS TextBox
    WITH text = "Q1", align = "right", bgcolor = #FFFFFF, width = 109, height = 23
    AT X.180, Y.90
    MEMBER OF BOLD
CREATE C1 AS TextBox
    WITH text = "Q2", align = "right", bgcolor = #FFFFFF, width = 109, height = 23
    AT X.290, Y.90
    MEMBER OF BOLD
CREATE D1 AS TextBox
    WITH text = "Q3", align = "right", bgcolor = #FFFFFF, width = 109, height = 23
    AT X.400, Y.90
    MEMBER OF BOLD
CREATE E1 AS TextBox
    WITH text = "Total", align = "right", bgcolor = #FFFFFF, width = 109, height = 23
    AT X.510, Y.90
    MEMBER OF BOLD

-- Rows 2-6: data you can edit, with a row total in column E.
CREATE A2 AS TextBox
    WITH text = "Widgets", bgcolor = #FFFFFF, width = 129, height = 23
    AT X.50, Y.114
CREATE B2 AS TextInput
    WITH content = "120", width = 109, height = 23, align = "right"
    AT X.180, Y.114
    MEMBER OF Sheet, Row2, ColB, All
    ON CHANGE OF FOCUS WHERE B2 IS FOCUS
        pick("B2", B2, HdrB, Hdr2)
    ON TYPE
        pick("B2", B2, HdrB, Hdr2)
    ON CHANGE
        pick("B2", B2, HdrB, Hdr2)
CREATE C2 AS TextInput
    WITH content = "135", width = 109, height = 23, align = "right"
    AT X.290, Y.114
    MEMBER OF Sheet, Row2, ColC, All
    ON CHANGE OF FOCUS WHERE C2 IS FOCUS
        pick("C2", C2, HdrC, Hdr2)
    ON TYPE
        pick("C2", C2, HdrC, Hdr2)
    ON CHANGE
        pick("C2", C2, HdrC, Hdr2)
CREATE D2 AS TextInput
    WITH content = "150", width = 109, height = 23, align = "right"
    AT X.400, Y.114
    MEMBER OF Sheet, Row2, ColD, All
    ON CHANGE OF FOCUS WHERE D2 IS FOCUS
        pick("D2", D2, HdrD, Hdr2)
    ON TYPE
        pick("D2", D2, HdrD, Hdr2)
    ON CHANGE
        pick("D2", D2, HdrD, Hdr2)
CREATE E2 AS TextBox
    WITH content = "=SUM(Row2)", align = "right", bgcolor = #FFFFFF, width = 109, height = 23
    AT X.510, Y.114
    MEMBER OF Sheet, ColE
    ON CLICK
        pick("E2", E2, HdrE, Hdr2)

CREATE A3 AS TextBox
    WITH text = "Gadgets", bgcolor = #FFFFFF, width = 129, height = 23
    AT X.50, Y.138
CREATE B3 AS TextInput
    WITH content = "80", width = 109, height = 23, align = "right"
    AT X.180, Y.138
    MEMBER OF Sheet, Row3, ColB, All
    ON CHANGE OF FOCUS WHERE B3 IS FOCUS
        pick("B3", B3, HdrB, Hdr3)
    ON TYPE
        pick("B3", B3, HdrB, Hdr3)
    ON CHANGE
        pick("B3", B3, HdrB, Hdr3)
CREATE C3 AS TextInput
    WITH content = "95", width = 109, height = 23, align = "right"
    AT X.290, Y.138
    MEMBER OF Sheet, Row3, ColC, All
    ON CHANGE OF FOCUS WHERE C3 IS FOCUS
        pick("C3", C3, HdrC, Hdr3)
    ON TYPE
        pick("C3", C3, HdrC, Hdr3)
    ON CHANGE
        pick("C3", C3, HdrC, Hdr3)
CREATE D3 AS TextInput
    WITH content = "110", width = 109, height = 23, align = "right"
    AT X.400, Y.138
    MEMBER OF Sheet, Row3, ColD, All
    ON CHANGE OF FOCUS WHERE D3 IS FOCUS
        pick("D3", D3, HdrD, Hdr3)
    ON TYPE
        pick("D3", D3, HdrD, Hdr3)
    ON CHANGE
        pick("D3", D3, HdrD, Hdr3)
CREATE E3 AS TextBox
    WITH content = "=SUM(Row3)", align = "right", bgcolor = #FFFFFF, width = 109, height = 23
    AT X.510, Y.138
    MEMBER OF Sheet, ColE
    ON CLICK
        pick("E3", E3, HdrE, Hdr3)

CREATE A4 AS TextBox
    WITH text = "Gizmos", bgcolor = #FFFFFF, width = 129, height = 23
    AT X.50, Y.162
CREATE B4 AS TextInput
    WITH content = "200", width = 109, height = 23, align = "right"
    AT X.180, Y.162
    MEMBER OF Sheet, Row4, ColB, All
    ON CHANGE OF FOCUS WHERE B4 IS FOCUS
        pick("B4", B4, HdrB, Hdr4)
    ON TYPE
        pick("B4", B4, HdrB, Hdr4)
    ON CHANGE
        pick("B4", B4, HdrB, Hdr4)
CREATE C4 AS TextInput
    WITH content = "180", width = 109, height = 23, align = "right"
    AT X.290, Y.162
    MEMBER OF Sheet, Row4, ColC, All
    ON CHANGE OF FOCUS WHERE C4 IS FOCUS
        pick("C4", C4, HdrC, Hdr4)
    ON TYPE
        pick("C4", C4, HdrC, Hdr4)
    ON CHANGE
        pick("C4", C4, HdrC, Hdr4)
CREATE D4 AS TextInput
    WITH content = "210", width = 109, height = 23, align = "right"
    AT X.400, Y.162
    MEMBER OF Sheet, Row4, ColD, All
    ON CHANGE OF FOCUS WHERE D4 IS FOCUS
        pick("D4", D4, HdrD, Hdr4)
    ON TYPE
        pick("D4", D4, HdrD, Hdr4)
    ON CHANGE
        pick("D4", D4, HdrD, Hdr4)
CREATE E4 AS TextBox
    WITH content = "=SUM(Row4)", align = "right", bgcolor = #FFFFFF, width = 109, height = 23
    AT X.510, Y.162
    MEMBER OF Sheet, ColE
    ON CLICK
        pick("E4", E4, HdrE, Hdr4)

CREATE A5 AS TextBox
    WITH text = "Doohickeys", bgcolor = #FFFFFF, width = 129, height = 23
    AT X.50, Y.186
CREATE B5 AS TextInput
    WITH content = "45", width = 109, height = 23, align = "right"
    AT X.180, Y.186
    MEMBER OF Sheet, Row5, ColB, All
    ON CHANGE OF FOCUS WHERE B5 IS FOCUS
        pick("B5", B5, HdrB, Hdr5)
    ON TYPE
        pick("B5", B5, HdrB, Hdr5)
    ON CHANGE
        pick("B5", B5, HdrB, Hdr5)
CREATE C5 AS TextInput
    WITH content = "60", width = 109, height = 23, align = "right"
    AT X.290, Y.186
    MEMBER OF Sheet, Row5, ColC, All
    ON CHANGE OF FOCUS WHERE C5 IS FOCUS
        pick("C5", C5, HdrC, Hdr5)
    ON TYPE
        pick("C5", C5, HdrC, Hdr5)
    ON CHANGE
        pick("C5", C5, HdrC, Hdr5)
CREATE D5 AS TextInput
    WITH content = "55", width = 109, height = 23, align = "right"
    AT X.400, Y.186
    MEMBER OF Sheet, Row5, ColD, All
    ON CHANGE OF FOCUS WHERE D5 IS FOCUS
        pick("D5", D5, HdrD, Hdr5)
    ON TYPE
        pick("D5", D5, HdrD, Hdr5)
    ON CHANGE
        pick("D5", D5, HdrD, Hdr5)
CREATE E5 AS TextBox
    WITH content = "=SUM(Row5)", align = "right", bgcolor = #FFFFFF, width = 109, height = 23
    AT X.510, Y.186
    MEMBER OF Sheet, ColE
    ON CLICK
        pick("E5", E5, HdrE, Hdr5)

CREATE A6 AS TextBox
    WITH text = "Thingamajigs", bgcolor = #FFFFFF, width = 129, height = 23
    AT X.50, Y.210
CREATE B6 AS TextInput
    WITH content = "310", width = 109, height = 23, align = "right"
    AT X.180, Y.210
    MEMBER OF Sheet, Row6, ColB, All
    ON CHANGE OF FOCUS WHERE B6 IS FOCUS
        pick("B6", B6, HdrB, Hdr6)
    ON TYPE
        pick("B6", B6, HdrB, Hdr6)
    ON CHANGE
        pick("B6", B6, HdrB, Hdr6)
CREATE C6 AS TextInput
    WITH content = "295", width = 109, height = 23, align = "right"
    AT X.290, Y.210
    MEMBER OF Sheet, Row6, ColC, All
    ON CHANGE OF FOCUS WHERE C6 IS FOCUS
        pick("C6", C6, HdrC, Hdr6)
    ON TYPE
        pick("C6", C6, HdrC, Hdr6)
    ON CHANGE
        pick("C6", C6, HdrC, Hdr6)
CREATE D6 AS TextInput
    WITH content = "330", width = 109, height = 23, align = "right"
    AT X.400, Y.210
    MEMBER OF Sheet, Row6, ColD, All
    ON CHANGE OF FOCUS WHERE D6 IS FOCUS
        pick("D6", D6, HdrD, Hdr6)
    ON TYPE
        pick("D6", D6, HdrD, Hdr6)
    ON CHANGE
        pick("D6", D6, HdrD, Hdr6)
CREATE E6 AS TextBox
    WITH content = "=SUM(Row6)", align = "right", bgcolor = #FFFFFF, width = 109, height = 23
    AT X.510, Y.210
    MEMBER OF Sheet, ColE
    ON CLICK
        pick("E6", E6, HdrE, Hdr6)

-- Rows 7-9: formulas over the column ranges (TextBoxes: click one to read it).
CREATE A7 AS TextBox
    WITH text = "Total", bgcolor = #FFFFFF, width = 129, height = 23
    AT X.50, Y.234
    MEMBER OF BOLD
CREATE B7 AS TextBox
    WITH content = "=SUM(ColB)", align = "right", bgcolor = #FFFFFF, width = 109, height = 23
    AT X.180, Y.234
    MEMBER OF Sheet
    ON CLICK
        pick("B7", B7, HdrB, Hdr7)
CREATE C7 AS TextBox
    WITH content = "=SUM(ColC)", align = "right", bgcolor = #FFFFFF, width = 109, height = 23
    AT X.290, Y.234
    MEMBER OF Sheet
    ON CLICK
        pick("C7", C7, HdrC, Hdr7)
CREATE D7 AS TextBox
    WITH content = "=SUM(ColD)", align = "right", bgcolor = #FFFFFF, width = 109, height = 23
    AT X.400, Y.234
    MEMBER OF Sheet
    ON CLICK
        pick("D7", D7, HdrD, Hdr7)
CREATE E7 AS TextBox
    WITH content = "=SUM(ColE)", align = "right", bgcolor = #FFFFFF, width = 109, height = 23
    AT X.510, Y.234
    MEMBER OF Sheet
    ON CLICK
        pick("E7", E7, HdrE, Hdr7)

CREATE A8 AS TextBox
    WITH text = "Average", bgcolor = #FFFFFF, width = 129, height = 23
    AT X.50, Y.258
    MEMBER OF BOLD
CREATE B8 AS TextBox
    WITH content = "=AVG(ColB)", align = "right", bgcolor = #FFFFFF, width = 109, height = 23
    AT X.180, Y.258
    MEMBER OF Sheet
    ON CLICK
        pick("B8", B8, HdrB, Hdr8)
CREATE C8 AS TextBox
    WITH content = "=AVG(ColC)", align = "right", bgcolor = #FFFFFF, width = 109, height = 23
    AT X.290, Y.258
    MEMBER OF Sheet
    ON CLICK
        pick("C8", C8, HdrC, Hdr8)
CREATE D8 AS TextBox
    WITH content = "=AVG(ColD)", align = "right", bgcolor = #FFFFFF, width = 109, height = 23
    AT X.400, Y.258
    MEMBER OF Sheet
    ON CLICK
        pick("D8", D8, HdrD, Hdr8)
CREATE E8 AS TextBox
    WITH content = "=AVG(ColE)", align = "right", bgcolor = #FFFFFF, width = 109, height = 23
    AT X.510, Y.258
    MEMBER OF Sheet
    ON CLICK
        pick("E8", E8, HdrE, Hdr8)

CREATE A9 AS TextBox
    WITH text = "Max", bgcolor = #FFFFFF, width = 129, height = 23
    AT X.50, Y.282
    MEMBER OF BOLD
CREATE B9 AS TextBox
    WITH content = "=MAX(ColB)", align = "right", bgcolor = #FFFFFF, width = 109, height = 23
    AT X.180, Y.282
    MEMBER OF Sheet
    ON CLICK
        pick("B9", B9, HdrB, Hdr9)
CREATE C9 AS TextBox
    WITH content = "=MAX(ColC)", align = "right", bgcolor = #FFFFFF, width = 109, height = 23
    AT X.290, Y.282
    MEMBER OF Sheet
    ON CLICK
        pick("C9", C9, HdrC, Hdr9)
CREATE D9 AS TextBox
    WITH content = "=MAX(ColD)", align = "right", bgcolor = #FFFFFF, width = 109, height = 23
    AT X.400, Y.282
    MEMBER OF Sheet
    ON CLICK
        pick("D9", D9, HdrD, Hdr9)
CREATE E9 AS TextBox
    WITH content = "=MAX(ColE)", align = "right", bgcolor = #FFFFFF, width = 109, height = 23
    AT X.510, Y.282
    MEMBER OF Sheet
    ON CLICK
        pick("E9", E9, HdrE, Hdr9)

-- Rows 10-12: empty cells to try formulas in (try =SUM(All) or =B2*1.1). Column E totals each row.
CREATE A10 AS TextBox
    WITH text = "Scratch", bgcolor = #FFFFFF, width = 129, height = 23
    AT X.50, Y.306
    MEMBER OF ITALICS
CREATE B10 AS TextInput
    WITH placeholder = "", width = 109, height = 23, align = "right"
    AT X.180, Y.306
    MEMBER OF Sheet, Row10
    ON CHANGE OF FOCUS WHERE B10 IS FOCUS
        pick("B10", B10, HdrB, Hdr10)
    ON TYPE
        pick("B10", B10, HdrB, Hdr10)
    ON CHANGE
        pick("B10", B10, HdrB, Hdr10)
CREATE C10 AS TextInput
    WITH placeholder = "", width = 109, height = 23, align = "right"
    AT X.290, Y.306
    MEMBER OF Sheet, Row10
    ON CHANGE OF FOCUS WHERE C10 IS FOCUS
        pick("C10", C10, HdrC, Hdr10)
    ON TYPE
        pick("C10", C10, HdrC, Hdr10)
    ON CHANGE
        pick("C10", C10, HdrC, Hdr10)
CREATE D10 AS TextInput
    WITH placeholder = "", width = 109, height = 23, align = "right"
    AT X.400, Y.306
    MEMBER OF Sheet, Row10
    ON CHANGE OF FOCUS WHERE D10 IS FOCUS
        pick("D10", D10, HdrD, Hdr10)
    ON TYPE
        pick("D10", D10, HdrD, Hdr10)
    ON CHANGE
        pick("D10", D10, HdrD, Hdr10)
CREATE E10 AS TextBox
    WITH content = "=SUM(Row10)", align = "right", bgcolor = #FFFFFF, width = 109, height = 23
    AT X.510, Y.306
    MEMBER OF Sheet
    ON CLICK
        pick("E10", E10, HdrE, Hdr10)

CREATE A11 AS TextBox
    WITH text = "", bgcolor = #FFFFFF, width = 129, height = 23
    AT X.50, Y.330
CREATE B11 AS TextInput
    WITH placeholder = "", width = 109, height = 23, align = "right"
    AT X.180, Y.330
    MEMBER OF Sheet, Row11
    ON CHANGE OF FOCUS WHERE B11 IS FOCUS
        pick("B11", B11, HdrB, Hdr11)
    ON TYPE
        pick("B11", B11, HdrB, Hdr11)
    ON CHANGE
        pick("B11", B11, HdrB, Hdr11)
CREATE C11 AS TextInput
    WITH placeholder = "", width = 109, height = 23, align = "right"
    AT X.290, Y.330
    MEMBER OF Sheet, Row11
    ON CHANGE OF FOCUS WHERE C11 IS FOCUS
        pick("C11", C11, HdrC, Hdr11)
    ON TYPE
        pick("C11", C11, HdrC, Hdr11)
    ON CHANGE
        pick("C11", C11, HdrC, Hdr11)
CREATE D11 AS TextInput
    WITH placeholder = "", width = 109, height = 23, align = "right"
    AT X.400, Y.330
    MEMBER OF Sheet, Row11
    ON CHANGE OF FOCUS WHERE D11 IS FOCUS
        pick("D11", D11, HdrD, Hdr11)
    ON TYPE
        pick("D11", D11, HdrD, Hdr11)
    ON CHANGE
        pick("D11", D11, HdrD, Hdr11)
CREATE E11 AS TextBox
    WITH content = "=SUM(Row11)", align = "right", bgcolor = #FFFFFF, width = 109, height = 23
    AT X.510, Y.330
    MEMBER OF Sheet
    ON CLICK
        pick("E11", E11, HdrE, Hdr11)

CREATE A12 AS TextBox
    WITH text = "", bgcolor = #FFFFFF, width = 129, height = 23
    AT X.50, Y.354
CREATE B12 AS TextInput
    WITH placeholder = "", width = 109, height = 23, align = "right"
    AT X.180, Y.354
    MEMBER OF Sheet, Row12
    ON CHANGE OF FOCUS WHERE B12 IS FOCUS
        pick("B12", B12, HdrB, Hdr12)
    ON TYPE
        pick("B12", B12, HdrB, Hdr12)
    ON CHANGE
        pick("B12", B12, HdrB, Hdr12)
CREATE C12 AS TextInput
    WITH placeholder = "", width = 109, height = 23, align = "right"
    AT X.290, Y.354
    MEMBER OF Sheet, Row12
    ON CHANGE OF FOCUS WHERE C12 IS FOCUS
        pick("C12", C12, HdrC, Hdr12)
    ON TYPE
        pick("C12", C12, HdrC, Hdr12)
    ON CHANGE
        pick("C12", C12, HdrC, Hdr12)
CREATE D12 AS TextInput
    WITH placeholder = "", width = 109, height = 23, align = "right"
    AT X.400, Y.354
    MEMBER OF Sheet, Row12
    ON CHANGE OF FOCUS WHERE D12 IS FOCUS
        pick("D12", D12, HdrD, Hdr12)
    ON TYPE
        pick("D12", D12, HdrD, Hdr12)
    ON CHANGE
        pick("D12", D12, HdrD, Hdr12)
CREATE E12 AS TextBox
    WITH content = "=SUM(Row12)", align = "right", bgcolor = #FFFFFF, width = 109, height = 23
    AT X.510, Y.354
    MEMBER OF Sheet
    ON CLICK
        pick("E12", E12, HdrE, Hdr12)

CREATE Notes AS TextBox
    WITH text = "Ranges are sets you can use in any formula: Row2..Row6, Row10..Row12, ColB..ColE, All."
    AT X.16, Y.392
    MEMBER OF GRAY

-- the sheet-tab bar along the bottom
CREATE TabBar AS TextBox
    WITH text = "", bgcolor = #F1F3F4, width = 604, height = 28
    AT X.16, Y.430

CREATE Home AS TextBox
    WITH text = "<- Home", link = "/", height = 26
    AT X.22, Y.431

CREATE SheetTab AS TextBox
    WITH text = "Sheet1", bgcolor = #FFFFFF, width = 90, height = 26, align = "center"
    AT X.150, Y.431
    MEMBER OF BOLD, BLUE

-- the cells live in Sheet, so showing Sheet shows them all
ADD Sheet TO VISIBLE
