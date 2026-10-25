-- grid_demo.sml -- DEFINE GRID X/Y: a virtual coordinate system that scales
-- to whatever the actual window is, plus scrolling for content that runs
-- past the visible area. Neither X nor Y is a hard boundary.

DEFINE GRID X 800, Y 1400

CREATE Title AS H2
    WITH text = "DEFINE GRID demo -- resize the window, or scroll down"
    AT X.Center, Y.10

CREATE Note AS TextBox
    WITH text = "This page is a virtual 800 x 1400 canvas. Try the mouse wheel."
    AT X.Center, Y.44

CREATE Box1 AS ContentPane WITH text = "X.100, Y.100"  AT X.100, Y.100
CREATE Box2 AS ContentPane WITH text = "X.400, Y.100"  AT X.400, Y.100
CREATE Box3 AS ContentPane WITH text = "X.100, Y.300"  AT X.100, Y.300
CREATE Box4 AS ContentPane WITH text = "X.400, Y.300"  AT X.400, Y.300

-- Y.1300 is INSIDE the declared 1400, so it always scales to fit the window
-- (that is the entire point of declaring Y) -- it can never need scrolling.
-- To actually need a scroll, content has to sit PAST the declared bound;
-- neither axis is a hard boundary, so this is not rejected, just placed
-- beyond what "fits", the same as it would be with no grid at all.
CREATE Deep AS ContentPane
    WITH text = "Past the declared grid: X.100, Y.2000 -- scroll to reach me"
    AT X.100, Y.2000
