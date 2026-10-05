/* Cylon cover — slide-on shell for the board.
 *
 * The board enters from the open front (Y = 0) and butts against the back
 * wall. Two shallow grooves at the top and bottom of the cavity act as
 * retention lips, and the small posts at the front corners locate the board.
 *
 * Everything below is parametric; the defaults reproduce the original part.
 * Dimensions are in millimetres. Each parameter group has a section comment so
 * it shows up grouped in OpenSCAD's Customizer.
 */

/* [Board] */
board_w   = 19;    // board width (X)
board_d   = 16;    // board depth that slides into the cover (Y)
board_h   = 4.5;   // board + tallest component (Z)

/* [Shell] */
wall      = 1;     // side, floor and roof thickness
back_wall = 0.6;   // material left behind the board

/* [Retention grooves] */
groove_h     = 0.3; // groove height (Z)
groove_inset = 6;   // how far the groove stops short of the back (Y)
cut          = 0.05; // overcut so the groove clears the side walls

/* [Back clips] */
clip_w     = 1;    // clip width (X)
clip_d     = 4;    // clip depth (Y)
clip_inset = 4;    // clip front face offset from the outer back (Y)
tab_h      = 3.5;  // short tab height (Z)
tab_d      = 6;    // short tab depth (Y)

/* [Front posts] */
post   = 1.5;         // post cross-section (X/Y)
post_h = 3.9;         // post height (Z)
post_z = wall + 0.3;  // post base height (Z)

/* [Hidden] */
outer_w = board_w + 2 * wall;  // overall width
outer_d = board_d + back_wall; // overall depth
outer_h = board_h + 2 * wall;  // overall height

// --------------------------------------------------------------- modules

// Hollow shell: open at the front, closed at the back, with both grooves.
module shell() {
    difference() {
        cube([outer_w, outer_d, outer_h]);

        // board cavity (the extra `cut` keeps the back wall exactly back_wall deep)
        translate([wall, -cut, wall])
            cube([board_w, board_d + cut, board_h]);

        // retention grooves at the floor and roof of the cavity
        for (z = [wall, wall + board_h - groove_h])
            translate([-cut, -cut, z])
                cube([outer_w + 2 * cut, board_d - groove_inset, groove_h]);
    }
}

// Back clips: two full-height clips plus two shorter locating tabs.
module clips() {
    for (x = [outer_w / 3, outer_w * 2 / 3]) {
        translate([x - clip_w / 2, outer_d - clip_inset, 0])
            cube([clip_w, clip_d, outer_h]);
        translate([x - clip_w / 2, outer_d - tab_d, 0])
            cube([clip_w, tab_d, tab_h]);
    }
}

// Front corner posts that locate the board.
module posts() {
    for (x = [0, outer_w - post])
        translate([x, 0, post_z])
            cube([post, post, post_h]);
}

// --------------------------------------------------------------- assembly

union() {
    shell();
    clips();
    posts();
}
