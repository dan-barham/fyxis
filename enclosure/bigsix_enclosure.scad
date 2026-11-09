// FYXIS: PYXIS side display enclosure, version 3
// Holds the Waveshare ESP32-S3-Touch-LCD-4.3 on the PYXIS side plate, with a recessed REC key in a
// tab at the top front corner. Everything below the tab stays inside the side plate's pocket height,
// so the camera's top plate stays reachable.
//
// Two shells:
//   tray  thin back: locating boss for the side plate's pocket, slotted 1/4"-20 bolts (button heads),
//         sockets for the board's corner posts, side rails that take the cap screws
//   cap   one-piece sculpted front (face and sides, 6° draft, 45° chamfered face edge) with the screen
//         window, the key well and the switch plate. It slides over the tray, and four M2 button heads
//         go in through its top and bottom edges, so the face has no screws.
// Multi-material inlays: exported as extra parts in the same frame, then packed with their body into one
// 3MF object per printed piece by make_3mf.py (see README):
//   cap_inlay_white   "FYXIS" under the screen, "USB" beside the USB-C port on the rear end
//   cap_inlay_red     ring round the key well
//   keycap_inlay      "REC" on the key
//
// Coordinates (as mounted, looking at the screen): X toward the lens, Y up, Z out of the camera.
// Z = 0 is the side plate's surface. All sizes in mm.

part = "assembly"; // [assembly, section, section_key, cap, cap_inlay_white, cap_inlay_red, tray, keycap, keycap_inlay]
screen_key = false;          // product shots: paint the screen pure green so a picture can be put there

/* [Camera side plate (measured)] */
pocket_w = 94.5;
pocket_h = 83.5;
pocket_depth = 3.0;
boss_h = 2.8;
boss_clear = 0.4;
boss_corner = 9;
hole_spacing = 37.2;
hole_from_front = 51.2;
bolt_to_rail = 63.5;        // hole centres to the NATO rail
bolt_to_rear = 59;          // hole centres to the first obstruction behind
rail_margin = 1.0;
rail_h = 14.5;
rail_w = 21;
space_above = 25;           // free side plate above the pocket
corner_screw_from_rear = 10.9;
corner_screw_from_front = 13.9;
corner_screw_from_edge = 7.7;
corner_screw_relief_d = 12;
corner_screw_relief_depth = 1.2;
bolt_d = 7.0;               // slot width
bolt_slot = 6;              // slot travel (±3)
bolt_head_d = 12.2;         // counterbore for 1/4"-20 button heads (head about 11.1 x 3.4)
bolt_under_head = 1.6;      // plastic left under the head

/* [Board (measured)] */
pcb = [106, 68];
pcb_holes = [[4, 4], [102, 4], [4, 64], [102, 64]];
glass = [105.42, 67.07];
glass_stack = 9.0;          // glass front to PCB back
post_h = 4.0;               // the board's own corner posts
post_socket_d = 6.6;
post_socket_depth = 1.0;
z_pcb = 6.5;                // PCB back above the side plate: the bolt heads and the 4 mm parts sit under it
border_rear = 5.19;
border_front = 3.01;
border_top = 6.47;
border_bottom = 4.20;
active = [95.04, 53.86];
opening_margin = 1.0;
usb_y = [12.93, 32.90];     // UART, USB (from the board's bottom edge as mounted)
usb_from_glass = 10.65;     // port centre behind the glass front
usb_cut = [13.5, 7.5];
header_window = [10.5, 15]; // window through the floor under the sensor header (X, Y)
header_window_y = 9;        // from the board's bottom edge

/* [Case] */
clear = 0.4;                // round the board
skirt = 1.6;                // cap side wall thickness at the face
draft = 6;                  // degrees: the sides lean in toward the face
face_t = 1.6;               // cap face over the glass
face_chamfer = 2.4;         // 45° round the face
foam = 0.5;                 // compressed foam tape between the glass border and the cap
inner_r = 3;
tab_margin = 3.0;           // face around the key well
tab_fillet = 6;             // where the key tab meets the top edge
rim_w = 3.0;                // tray side rails
rim_gap = 0.2;              // tray to cap
floor_t = 1.6;
screw_z = 8;                // M2 button heads through the cap's top and bottom edges into the rails
screw_x_inset = 18;
screw_clear_d = 2.3;
screw_pilot_d = 1.7;
screw_head_d = 4.2;
screw_head_depth = 0.9;
dz = 0.2;                   // slice height of the sculpted cap

/* [Key] */
key = [26, 17];
key_h = 2.6;
key_r = 2.0;
key_chamfer = 0.6;
key_recess = 0.8;           // key top below the face
key_seat = 7.5;             // switch plate top to key underside (13 mm measured with the 5.5 mm key)
key_gap = 0.8;
well_wall = 1.2;
well_chamfer = 0.6;
sw_cut = 14.0;
sw_plate_t = 1.3;
// MX-style cross stem (the V2 low-profile switch)
mx_post_d = 5.5;
mx_cross = [4.15, 1.30];
mx_depth = 3.8;

/* [Inlays] */
inlay_d = 0.6;              // 3 layers at 0.2 mm
font = "Avenir Next:style=Bold";
brand = "FYXIS";
brand_size = 4.2;
brand_spacing = 1.35;
key_label = "REC";
key_label_size = 5.4;
usb_label = "USB";
usb_label_size = 2.6;
ring_gap = 1.0;             // key well edge (after its chamfer) to the ring
ring_w = 1.0;

/* [Hidden] */
$fn = 64;
eps = 0.01;

// ---------------------------------------------------------------- derived
z_glass = z_pcb + glass_stack;
z_lip = z_glass + foam;                                  // underside of the cap face
z_face = z_lip + face_t;
S = skirt + z_face * tan(draft);                         // cap wall thickness at the back edge
function inset(z) = z * tan(draft) + max(0, z - (z_face - face_chamfer));
pcb0 = [S + clear, S + rim_gap + rim_w + clear];
pcb_top = pcb0[1] + pcb[1];
x1 = pcb0[0] + pcb[0] + clear;                           // inside front
y1 = pcb_top + clear + rim_w + rim_gap;                  // inside top (main body)
L = x1 + S;
H = y1 + S;                                              // main body height
well_in = key + 2 * [key_gap, key_gap];
cup = well_in + 2 * [well_wall, well_wall];
cup0 = [x1 - clear - tab_margin - cup[0], pcb_top + clear];
sw = cup0 + cup / 2;
well0 = sw - well_in / 2;
tab_x0 = cup0[0] - clear - tab_margin;                   // inside left edge of the tab
H_tab = cup0[1] + cup[1] + clear + tab_margin + S;
z_usb = z_glass - usb_from_glass;
z_key_top = z_face - key_recess;
z_plate_top = z_key_top - key_h - key_seat;
glass0 = pcb0 + (pcb - glass) / 2;
view = [glass[0] - border_rear - border_front, glass[1] - border_bottom - border_top];
view0 = glass0 + [border_rear, border_bottom];
open_size = active + 2 * [opening_margin, opening_margin];
open0 = view0 + (view - open_size) / 2;
bolt_x = L - (bolt_to_rail - rail_margin);
pocket0 = [bolt_x + hole_from_front - pocket_w, 0];      // case bottom level with the pocket bottom
bolts = [[bolt_x, (pocket_h - hole_spacing) / 2], [bolt_x, (pocket_h + hole_spacing) / 2]];
screws_bottom = [screw_x_inset, L - screw_x_inset];
screws_top = [screw_x_inset, tab_x0 - 12];
rim_bottom_y = [S + rim_gap, S + rim_gap + rim_w];
rim_top_y = [pcb_top + clear, y1 - rim_gap];
pad_top = z_pcb - post_h + post_socket_depth;

echo(str("Case ", L, " x ", H, " mm (", H_tab, " at the key tab), ", z_face, " mm proud of the side plate (+", boss_h, " boss)"));
echo(str("Front ", bolt_to_rail - (L - bolt_x), " mm short of the rail; rear ", bolt_to_rear - bolt_x, " mm short of the rear obstruction"));
echo(str("Main body top ", H - pocket_h, " mm from the pocket top; key tab ", H_tab - pocket_h, " mm above it (", space_above, " free)"));
echo(str("PCB back Z ", z_pcb, ", bolt head top Z ", -boss_h + bolt_under_head + 3.4, ", switch plate top Z ", z_plate_top));

// ---------------------------------------------------------------- outlines
module rrect(size, r) { offset(r = r) offset(delta = -r) square(size); }

module chamfer_rect(size, c, r = 1.5) {
    offset(r = r) offset(delta = -r)
        polygon([[c, 0], [size[0] - c, 0], [size[0], c], [size[0], size[1] - c],
                 [size[0] - c, size[1]], [c, size[1]], [0, size[1] - c], [0, c]]);
}

module cup2d() { translate(cup0) rrect(cup, key_r + key_gap + well_wall); }
module well2d(grow = 0) { translate(well0 - [grow, grow]) rrect(well_in + 2 * [grow, grow], key_r + key_gap + grow); }

// inside of the cap (vertical walls)
module inner2d() {
    translate([S, S]) rrect([x1 - S, y1 - S], inner_r);
    offset(r = clear + tab_margin) cup2d();
}

// outside of the cap at Z = 0
module outer2d() { offset(r = -tab_fillet) offset(delta = tab_fillet) offset(r = S) inner2d(); }

// ---------------------------------------------------------------- cap
module cap_slices() {
    n = ceil(z_face / dz);
    for (i = [0 : n - 1]) {
        z0 = i * dz;
        h = min(dz, z_face - z0);
        translate([0, 0, z0]) linear_extrude(h + (i < n - 1 ? eps : 0))
            offset(r = -inset(z0 + h / 2)) outer2d();
    }
}

// shell with its cavity, plus the key well cup (switch plate as its floor)
module cap_solid() {
    difference() {
        cap_slices();
        translate([0, 0, -1]) linear_extrude(z_lip + 1) inner2d();
    }
    translate([0, 0, z_plate_top - sw_plate_t]) linear_extrude(z_lip - (z_plate_top - sw_plate_t) + eps) cup2d();
}

module cap_body() {
    difference() {
        cap_solid();
        // screen window, 45° chamfer on the face
        hull() {
            translate([open0[0], open0[1], z_lip - 1]) cube([open_size[0], open_size[1], 1]);
            translate([open0[0], open0[1], z_face - 1.2]) cube([open_size[0], open_size[1], eps]);
            translate([open0[0] - 1.2, open0[1] - 1.2, z_face + eps]) cube([open_size[0] + 2.4, open_size[1] + 2.4, eps]);
        }
        // key well, chamfered on the face
        translate([0, 0, z_plate_top]) linear_extrude(z_face) well2d();
        hull() {
            translate([0, 0, z_face - well_chamfer]) linear_extrude(eps) well2d();
            translate([0, 0, z_face + eps]) linear_extrude(eps) well2d(well_chamfer);
        }
        translate([sw[0] - sw_cut / 2, sw[1] - sw_cut / 2, z_plate_top - sw_plate_t - 1]) cube([sw_cut, sw_cut, 3]);
        // USB-C openings in the rear wall
        for (y = usb_y) usb_cutout(pcb0[1] + y, z_usb);
        // cap screws: clearance holes and spot-faces for the button heads
        for (x = screws_bottom) translate([x, 0, screw_z]) rotate([-90, 0, 0]) {
            translate([0, 0, -1]) cylinder(d = screw_clear_d, h = S + 2);
            translate([0, 0, -1]) cylinder(d = screw_head_d, h = 1 + inset(screw_z) + screw_head_depth);
        }
        for (x = screws_top) translate([x, H, screw_z]) rotate([90, 0, 0]) {
            translate([0, 0, -1]) cylinder(d = screw_clear_d, h = S + 2);
            translate([0, 0, -1]) cylinder(d = screw_head_d, h = 1 + inset(screw_z) + screw_head_depth);
        }
        inlay_white();
        inlay_red();
    }
}

module usb_cutout(y, z) {
    r = 3;
    hull() for (dy = [-1, 1], dz2 = [-1, 1])
        translate([-1, y + dy * (usb_cut[0] / 2 - r), z + dz2 * (usb_cut[1] / 2 - r)])
            rotate([0, 90, 0]) cylinder(r = r, h = pcb0[0] + 1.5);
}

// inlay volumes (they also cut the matching recesses)
module inlay_white() {
    // brand under the screen, left-aligned with the window
    band = [inset(z_face), open0[1] - 1.2];
    translate([open0[0], (band[0] + band[1]) / 2, z_face - inlay_d])
        linear_extrude(inlay_d + eps) text(brand, size = brand_size, font = font, spacing = brand_spacing, valign = "center");
    // "USB" on the rear end, above the USB port, upright when seen from behind; follows the draft
    yc = pcb0[1] + usb_y[1] + usb_cut[0] / 2 + 1.2 + usb_label_size / 2;
    zc = z_usb;                                          // centred on the port, clear of the face chamfer
    translate([inset(zc), yc, zc]) rotate([0, draft, 0])
        multmatrix([[0, 0, -1, 0], [0, 1, 0, 0], [1, 0, 0, 0], [0, 0, 0, 1]])
            translate([0, 0, -inlay_d]) linear_extrude(inlay_d + eps)
                text(usb_label, size = usb_label_size, font = font, halign = "center", valign = "center");
}

module inlay_red() {
    translate([0, 0, z_face - inlay_d]) linear_extrude(inlay_d + eps) difference() {
        well2d(well_chamfer + ring_gap + ring_w);
        well2d(well_chamfer + ring_gap);
    }
}

// ---------------------------------------------------------------- tray
module tray2d() { offset(r = -rim_gap) inner2d(); }

module rims2d() {
    intersection() {
        difference() { tray2d(); offset(r = -rim_w) tray2d(); }
        union() {
            translate([-1, -1]) square([L + 2, rim_bottom_y[1] + 1]);
            translate([-1, rim_top_y[0]]) square([tab_x0 - 1, H]);
        }
    }
}

module tray() {
    difference() {
        union() {
            linear_extrude(floor_t) tray2d();
            linear_extrude(z_lip - 0.8) rims2d();
            translate([0, 0, -boss_h]) linear_extrude(boss_h)
                translate(pocket0 + [boss_clear, boss_clear])
                    chamfer_rect([pocket_w, pocket_h] - 2 * [boss_clear, boss_clear], boss_corner);
            intersection() {                                                            // post pads, kept inside the tray
                for (p = pcb_holes) translate([pcb0[0] + p[0], pcb0[1] + p[1], 0]) cylinder(d = post_socket_d + 3, h = pad_top);
                linear_extrude(pad_top) tray2d();
            }
        }
        for (p = pcb_holes) translate([pcb0[0] + p[0], pcb0[1] + p[1], pad_top - post_socket_depth]) {
            cylinder(d = post_socket_d, h = 10);
            translate([0, 0, -3]) cylinder(d = 3.4, h = 10);                          // room for a screw tip
        }
        // 1/4"-20 slots, flat counterbores for the button heads
        for (p = bolts) translate([p[0], p[1], 0]) {
            translate([0, 0, -boss_h - 1]) slot(bolt_slot, bolt_d, 20);
            translate([0, 0, -boss_h + bolt_under_head]) slot(bolt_slot, bolt_head_d, 20);
        }
        // reliefs for the screws in the pocket corners
        for (x = [corner_screw_from_rear, pocket_w - corner_screw_from_front],
             y = [corner_screw_from_edge, pocket_h - corner_screw_from_edge])
            translate([pocket0[0] + x, pocket0[1] + y, -boss_h - 1])
                cylinder(d = corner_screw_relief_d, h = corner_screw_relief_depth + 1);
        // window under the sensor header (flat-soldered REC lead)
        translate([pcb0[0] + pcb[0] - header_window[0], pcb0[1] + header_window_y, -boss_h - 1])
            cube([header_window[0] + clear + 1, header_window[1], 10]);
        // window under the switch pins
        translate([sw[0] - 9, sw[1] - 9, -boss_h - 1]) cube([18, 18, 10]);
        // USB plug clearance at the rear edge of the floor
        for (y = usb_y) usb_cutout(pcb0[1] + y, z_usb);
        // pilot holes for the cap screws
        for (x = screws_bottom) translate([x, -1, screw_z]) rotate([-90, 0, 0]) cylinder(d = screw_pilot_d, h = rim_bottom_y[1] + 1);
        for (x = screws_top) translate([x, rim_top_y[0], screw_z]) rotate([-90, 0, 0]) cylinder(d = screw_pilot_d, h = H);
    }
}

module slot(len, d, h) { hull() for (s = [-1, 1]) translate([s * len / 2, 0, 0]) cylinder(d = d, h = h); }

// ---------------------------------------------------------------- key (underside at z = 0, stem below)
module key_shape(size = key, h = key_h) {
    translate([-size[0] / 2, -size[1] / 2, 0]) hull() {
        linear_extrude(h - key_chamfer) rrect(size, key_r);
        translate([key_chamfer, key_chamfer, 0]) linear_extrude(h) rrect(size - 2 * [key_chamfer, key_chamfer], key_r - key_chamfer);
    }
}

module key_text(size = key, h = key_h) {
    translate([0, 0, h - inlay_d]) linear_extrude(inlay_d + eps)
        text(key_label, size = key_label_size, font = font, spacing = 1.1, halign = "center", valign = "center");
}

module key_stem() {
    translate([0, 0, -mx_depth]) difference() {
        cylinder(d = mx_post_d, h = mx_depth + eps);
        translate([0, 0, -1]) {
            translate([-mx_cross[0] / 2, -mx_cross[1] / 2, 0]) cube([mx_cross[0], mx_cross[1], mx_depth]);
            translate([-mx_cross[1] / 2, -mx_cross[0] / 2, 0]) cube([mx_cross[1], mx_cross[0], mx_depth]);
        }
    }
}

module keycap_body() { difference() { key_shape(); key_text(); } key_stem(); }
module keycap_inlay() { intersection() { key_shape(); key_text(); } }

// ---------------------------------------------------------------- ghosts
module board_ghost() {
    color("#1f5fa8") translate([pcb0[0], pcb0[1], z_pcb]) cube([pcb[0], pcb[1], 1.6]);
    color("#b8b8b8") for (p = pcb_holes) translate([pcb0[0] + p[0], pcb0[1] + p[1], z_pcb - post_h]) cylinder(d = 5, h = post_h, $fn = 6);
    color("#c8c8c8") translate([glass0[0], glass0[1], z_pcb + 1.6]) cube([glass[0], glass[1], glass_stack - 1.6 - 0.7]);
    color("#101418") translate([glass0[0], glass0[1], z_glass - 0.7]) cube([glass[0], glass[1], 0.7]);
    color(screen_key ? "#00ff00" : "#2a3b52") translate([open0[0] + opening_margin, open0[1] + opening_margin, z_glass - eps]) cube([active[0], active[1], 0.02]);
}

module switch_ghost() {
    color("#8a5a2b") translate([sw[0] - 7.5, sw[1] - 7.5, z_plate_top]) cube([15, 15, 3]);
    color("#555") translate([sw[0] - 6.9, sw[1] - 6.9, z_plate_top - sw_plate_t - 2.2]) cube([13.8, 13.8, 2.2 + sw_plate_t]);
}

module camera_ghost() {
    color("#202020", 0.35) translate([-12, -12, -8]) difference() {
        cube([L + 36, H_tab + 24, 8]);
        translate([pocket0[0] + 12, pocket0[1] + 12, 8 - pocket_depth]) linear_extrude(pocket_depth + 1) chamfer_rect([pocket_w, pocket_h], 6, 2);
    }
    color("#444", 0.5) translate([bolt_x + bolt_to_rail, -12, 0]) cube([rail_w, H_tab + 24, rail_h]);
}

C_BODY = "#2d2f33";
C_RED = "#d0261c";
C_WHITE = "#f2f2f2";

module key_in_place() {
    translate([sw[0], sw[1], z_key_top - key_h]) { color(C_RED) keycap_body(); color(C_WHITE) keycap_inlay(); }
}

module everything(with_cap = true) {
    color("#4a4d52") tray();
    board_ghost();
    if (with_cap) { color(C_BODY) cap_body(); color(C_WHITE) inlay_white(); color(C_RED) inlay_red(); }
    switch_ghost();
    key_in_place();
}

// ---------------------------------------------------------------- output (print orientation)
module cap_print() { translate([0, H_tab, z_face]) rotate([180, 0, 0]) children(); }   // face down
module key_print() { translate([0, 0, key_h]) rotate([180, 0, 0]) children(); }        // top down

if (part == "cap") cap_print() cap_body();
else if (part == "cap_inlay_white") cap_print() intersection() { inlay_white(); cap_slices(); }
else if (part == "cap_inlay_red") cap_print() intersection() { inlay_red(); cap_slices(); }
else if (part == "tray") translate([0, 0, boss_h]) tray();                             // boss down
else if (part == "keycap") key_print() keycap_body();
else if (part == "keycap_inlay") key_print() keycap_inlay();
else if (part == "section") intersection() { everything(); translate([-30, pcb0[1] + usb_y[1], -20]) cube([L + 60, H_tab + 40, 60]); }
else if (part == "section_key") intersection() { everything(); translate([sw[0], -20, -20]) cube([L, H_tab + 40, 60]); }
else if (part == "assembly") { camera_ghost(); everything(); }
else if (part == "hero") everything();                   // for product shots: no camera, see screen_key
