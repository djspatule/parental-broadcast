// ============================================================
//  Children's Intercom — 3D-printable enclosure
//
//  Contents: ESP32 DevKit + MAX98357A + 31×31 mm top-firing
//            speaker (QUARKZMAN, JST-PH2.0, 2-ear, 44 mm span)
//
//  Two-part print — change PART then export each to STL:
//    PART = "body"   → the box (floor + walls + ESP32 cradle)
//    PART = "lid"    → the top plate with speaker grille
//    PART = "both"   → coloured preview in OpenSCAD viewer
//
//  Closure: 2 rubber bands running around the SHORT sides.
//  Notches on front + back long walls keep them from sliding up.
//
//  Hardware needed:
//    2 × M2.5×8 screws   (speaker ears → lid)
//    2 × rubber bands     (≈ 15–18 cm circumference)
// ============================================================

PART = "both"; // "body" | "lid" | "both"

// ── Shell ────────────────────────────────────────────────────
WALL  = 2.0;   // wall thickness everywhere
LID_T = 4.5;   // lid plate thickness
FIT   = 0.25;  // clearance between locating rim and body inner wall
LIP_H = 2.0;   // depth the locating rim descends into the body
LIP_W = 1.8;   // thickness of the hollow locating rim walls

// ── Internal cavity ─────────────────────────────────────────
// X = left–right (along ESP32 long axis, USB end toward right wall)
// Y = front–back
// Z = up (floor at z=WALL from outside bottom)
IN_W = 90;   // inner width  — 55 mm ESP32 + cradle wall + amp + wires
IN_D = 56;   // inner depth  — 28 mm ESP32 + clearance, centred
IN_H = 28;   // inner height — 8 mm under-PCB + PCB + headroom

// ── ESP32 cradle ─────────────────────────────────────────────
// U-channel (3 walls), open on the RIGHT (USB) end.
// The ESP32 drops in from above; the lid holds it down.
// USB cable exits straight through the right short wall.
//
// Measured ESP32 DevKit: 55 × 28 mm PCB, 8 mm of components below.
ESP_L   = 55;    // board length (X axis)
ESP_D   = 28;    // board width  (Y axis)
ESP_CLR =  0.4;  // clearance each side (total 0.8 mm — snug but insertable)
CRD_T   =  1.8;  // cradle wall thickness
CRD_H   = 10;    // cradle wall height (clips past PCB level at 8 mm)

// ── USB cable cutout (RIGHT short wall, x = OUT_W face) ──────
// ESP32 USB port sits at PCB height = WALL + 8 mm from box bottom.
// Hole is centred on that wall in Y, generous in Z.
USB_W   = 13;    // hole width  (micro-USB body ≈ 8 mm, cable ≈ 10 mm)
USB_H   =  9;    // hole height
USB_Z   =  6;    // bottom of hole from outside box bottom
                 // (PCB at 10 mm, USB connector ~1 mm below PCB top → 9 mm centre)

// ── Speaker (lid) ────────────────────────────────────────────
SPK_SPAN  = 44;   // ear screw hole span (Y axis), 2 holes
SPK_SCR_R =  1.3; // M2.5 self-tap radius in lid

GRL_R     = 13.0; // grille area radius
GRL_HOLE  =  1.1; // individual grille hole radius
GRL_PITCH =  3.0; // hex grid pitch

// ── Rubber-band notches (front + back long walls) ────────────
NOTCH_W = 6;    // notch width  (band sits in here)
NOTCH_H = 4;    // notch height from top of body wall downward
NOTCH_D = 1.5;  // notch depth  from outer surface inward

// ── Foot pads ────────────────────────────────────────────────
FOOT_R  = 4.0;
FOOT_H  = 0.8;
FOOT_IN = 7.0;

// ============================================================
//  Derived
// ============================================================
OUT_W  = IN_W + 2*WALL;   // outer width
OUT_D  = IN_D + 2*WALL;   // outer depth
BODY_H = IN_H + WALL;     // outer body height (floor + walls, open top)

$fn = 64;

LID_CX = OUT_W / 2;
LID_CY = OUT_D / 2;

// Cradle inner dimensions (with clearance)
CRD_IW = ESP_L + ESP_CLR * 2;  // inner length (X)
CRD_ID = ESP_D + ESP_CLR * 2;  // inner width  (Y)

// Cradle outer dimensions
CRD_OW = CRD_IW + CRD_T;       // +1 wall on closed (left) end only
CRD_OD = CRD_ID + CRD_T * 2;   // +1 wall each side (Y)

// Cradle position in box interior coordinates (before WALL offset)
// Right end of cradle flush with inner face of right wall
CRD_X = IN_W - CRD_OW;         // closed end x (inner coords)
CRD_Y = (IN_D - CRD_OD) / 2;   // centred in Y

// ============================================================
//  Helper — rounded rectangular prism
// ============================================================
module rbox(w, d, h, r = 3) {
    hull()
        for (x = [r, w-r], y = [r, d-r])
            translate([x, y, 0])
                cylinder(r=r, h=h);
}

// ============================================================
//  ESP32 CRADLE
//  Origin at the closed (left) end, floor level.
//  U-channel open toward +X (USB / right wall).
// ============================================================
module esp32_cradle() {
    // Lead-in chamfer height at top of walls for easier insertion
    CHAMFER = 1.5;

    difference() {
        // Outer block
        cube([CRD_OW, CRD_OD, CRD_H]);

        // Hollow centre (ESP32 slot), open on +X face
        translate([CRD_T, CRD_T, -0.1])
            cube([CRD_IW + 0.1, CRD_ID, CRD_H + 0.2]);

        // Chamfer the top inner edges of the two side walls for easy drop-in
        translate([CRD_T, CRD_T - 0.1, CRD_H - CHAMFER])
            rotate([-45, 0, 0])
                cube([CRD_IW, CHAMFER * 2, CHAMFER * 2]);
        translate([CRD_T, CRD_T + CRD_ID - CHAMFER + 0.1, CRD_H - CHAMFER])
            rotate([45, 0, 0])
                cube([CRD_IW, CHAMFER * 2, CHAMFER * 2]);

        // Chamfer the top inner edge of the closed end wall
        translate([CRD_T - CHAMFER, CRD_T - 0.1, CRD_H - CHAMFER])
            rotate([0, 45, 0])
                cube([CHAMFER * 2, CRD_OD + 0.2, CHAMFER * 2]);
    }
}

// ============================================================
//  BODY
// ============================================================
module body() {
    difference() {
        rbox(OUT_W, OUT_D, BODY_H);

        // Inner cavity (open top)
        translate([WALL, WALL, WALL])
            cube([IN_W, IN_D, IN_H + 1]);

        // USB hole — right short wall (x = OUT_W face), centred in Y
        translate([OUT_W - WALL - 0.1, OUT_D/2 - USB_W/2, USB_Z])
            cube([WALL + 0.2, USB_W, USB_H]);

        // Rubber-band notches — front long wall (y = 0 face)
        for (x = [OUT_W*0.3, OUT_W*0.7])
            translate([x - NOTCH_W/2, -0.1, BODY_H - NOTCH_H])
                cube([NOTCH_W, NOTCH_D + 0.1, NOTCH_H + 0.1]);

        // Rubber-band notches — back long wall (y = OUT_D face)
        for (x = [OUT_W*0.3, OUT_W*0.7])
            translate([x - NOTCH_W/2, OUT_D - NOTCH_D, BODY_H - NOTCH_H])
                cube([NOTCH_W, NOTCH_D + 0.1, NOTCH_H + 0.1]);
    }

    // ESP32 cradle — sits on interior floor, USB end toward right wall
    translate([WALL + CRD_X, WALL + CRD_Y, WALL])
        esp32_cradle();

    // Foot pads
    for (x = [FOOT_IN, OUT_W - FOOT_IN], y = [FOOT_IN, OUT_D - FOOT_IN])
        translate([x, y, 0])
            cylinder(r=FOOT_R, h=FOOT_H);
}

// ============================================================
//  LID  (print grille-face-up for cleanest holes)
// ============================================================
module lid() {
    RIM_OX = WALL + FIT;
    RIM_OY = WALL + FIT;
    RIM_OW = IN_W - 2*FIT;
    RIM_OD = IN_D - 2*FIT;
    RIM_IW = RIM_OW - 2*LIP_W;
    RIM_ID = RIM_OD - 2*LIP_W;

    difference() {
        union() {
            rbox(OUT_W, OUT_D, LID_T);

            // Locating rim — hollow square tube, hangs below plate
            translate([RIM_OX, RIM_OY, -LIP_H])
                difference() {
                    cube([RIM_OW, RIM_OD, LIP_H]);
                    translate([LIP_W, LIP_W, -0.1])
                        cube([RIM_IW, RIM_ID, LIP_H + 0.2]);
                }
        }

        // Speaker grille — hex grid, circular boundary, through full plate
        translate([LID_CX, LID_CY, -0.1])
            for (row = [-12:12], col = [-12:12]) {
                gx = col * GRL_PITCH + (abs(row) % 2) * GRL_PITCH / 2;
                gy = row * GRL_PITCH * 0.866;
                if (sqrt(gx*gx + gy*gy) < GRL_R)
                    translate([gx, gy, 0])
                        cylinder(r=GRL_HOLE, h=LID_T + 0.2);
            }

        // Speaker mounting holes — through plate AND rim where rim exists
        for (dy = [-1, 1])
            translate([LID_CX, LID_CY + dy * SPK_SPAN/2, -(LIP_H + 0.1)])
                cylinder(r=SPK_SCR_R, h=LID_T + LIP_H + 0.2);

        // Rubber-band notches on lid edges (same X positions as body notches)
        for (x = [OUT_W*0.3, OUT_W*0.7]) {
            translate([x - NOTCH_W/2, -0.1, -0.1])
                cube([NOTCH_W, NOTCH_D + 0.1, LID_T + 0.2]);
            translate([x - NOTCH_W/2, OUT_D - NOTCH_D, -0.1])
                cube([NOTCH_W, NOTCH_D + 0.1, LID_T + 0.2]);
        }
    }
}

// ============================================================
//  Render
// ============================================================
if (PART == "body") {
    body();
} else if (PART == "lid") {
    lid();
} else {
    color("SteelBlue",      0.85) body();
    color("LightSteelBlue", 0.85)
        translate([0, 0, BODY_H + 2]) lid();
}
