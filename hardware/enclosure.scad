// FermentDial M5 Dial enclosure
//
// Wedge case: lower at the user/front, taller at the rear. The Dial cutout is
// circular and its axis is normal to the sloped face. The service lid is a
// separate part= export so OpenSCAD does not show a stray square under the case.
// Connector layout:
//     RIGHT SIDE: 12V POWER IN + PUMP OUT
//     LEFT SIDE:  TEMP PROBE + HEATER OUT
//
// Units: mm.
//
// Recommended workflow:
//   Set part = "shell"; render/export shell STL.
//   Set part = "lid"; render/export bottom cover STL.
//   Set part = "feet"; render/export TPU feet STL.
//   Tune connector_hole_d to your measured panel connectors.

$fn = 128;

part = "shell";        // "shell", "lid", "feet", or "assembly_preview"

// Envelope
W = 90.0;               // FTSS measured width
D = 89.0;               // FTSS measured length/depth + 3 for interior room
H_front = 46.0;         // lower/front height
H_rear = 65.0;          // rear/tallest height
wall = 2.8;
floor_thick = 2.8;
dome_height = 0.0;
case_corner_r = 7.0;
top_front_round_r = 0; // set to 0 for the original flat/sharp front top edge
top_back_round_r = 0;  // set to 0 for the original flat/sharp rear top edge
top_edge_round_segments = 18;
edge_round_r = 2.5;      // Apple-style soft round on all top edges and vertical corners; 0 = sharp

// M5Dial panel cutout from the official drawing: suggested hole Ø45 ±0.2 mm.
// The dial's threaded section is on the device; the case should provide a plain centered opening.
dial_mount_hole_d = 45.0;
dial_threaded_section_depth = 14.2;

// Connector holes: placeholder, measure real hardware
connector_hole_d = 11.0;
temp_probe_hole_d = 7.15;   // measured temp probe connector

// Lid / screws
lid_thick = 2.4;
screw_d = 3.2;         // M3 clearance
lid_screw_counterbore_d = 6.2;
lid_screw_counterbore_h = 1.1;
lid_foot_pad_d = lid_screw_counterbore_d;
lid_foot_pocket_d = lid_foot_pad_d;
lid_foot_pocket_depth = 0.35;
lid_foot_proud_h = 1.35;   // must clear the skirt standing 1.1 proud of the lid
lid_foot_pad_h = lid_foot_pocket_depth + lid_foot_proud_h;
lid_rear_foot_pad_h = lid_screw_counterbore_h + lid_foot_proud_h;
screw_plug_cap_d = lid_screw_counterbore_d - 0.2;
screw_plug_cap_h = lid_screw_counterbore_h - 0.15;
screw_plug_stem_d = screw_d - 0.3;
screw_plug_stem_h = 1.7;
screw_plug_barb_d = screw_d + 0.25;
screw_plug_barb_h = 0.45;
screw_plug_lead_in_h = 0.35;
screw_plug_print_pitch = 12.0;
heatset_boss_d = 10.0;         // 3.1 mm wall around the 3.8 bore; larger d also pulls screws inboard
lid_heatset_seat_d = 5.2;      // shallow seat for a 5 mm heat-set insert face
lid_heatset_seat_depth = 0.5;
lid_heatset_seat_bevel_w = 0.25;
lid_heatset_seat_bevel_h = 0.25;
heatset_boss_h = 11.0;         // insert depth + ~2 mm of solid plastic below
heatset_insert_bore_d = 3.8;   // Ø4 x 8 mm insert: OD minus ~0.2 for grip
heatset_insert_depth = 9.0;    // 8 mm insert + 1 mm for melt squeeze-out
heatset_lead_in_d = 4.4;
heatset_lead_in_h = 0.8;
heatset_screw_relief_d = 3.2;
heatset_rib_w = 4.0;
heatset_rib_h = 7.0;
heatset_rib_wall_overlap = 0.8;
roof_supports_from_bosses = true;  // bosses must reach the roof: upside down they'd start mid-air
roof_support_d = 7.0;
roof_support_overlap = 1.5; // must exceed roof slope drop across the boss face
// Boss centers tucked into the corners, tangent to both inner walls.
heatset_pos_x = W/2 - wall - heatset_boss_d/2;
heatset_pos_y = wall + heatset_boss_d/2;
rear_heatset_pos_y = D - heatset_pos_y;
lid_foot_pad_x = heatset_pos_x;
lid_foot_pad_front_y = heatset_pos_y;
lid_foot_pad_rear_y = rear_heatset_pos_y;
// Slide-on bottom cover using the joint from the downloaded slide-box STLs:
// a skirt around the sides and rear hangs below the case bottom; its inner
// lip is a vertical capture face with a 45-degree chamfered overhang, and the
// lid's edges carry the matching chamfer. The skirt runs along both sides and
// the front; the lid slides flat against the case bottom in from the rear and
// the two rear screws lock the slide. All features are solid wedges; both
// parts print without supports.
bottom_open_w = W - 2*wall - 5;  // service opening: 2.5 mm floor ring inside the walls
bottom_open_d = D - 2*wall - 5;

skirt_h = 3.5;             // skirt drop below the case bottom plane
skirt_inset = 2.1;         // lip capture face inset from the case outer face
chan_vert = 1.6;           // vertical capture face depth below the bottom plane
edge_chamf = 0.95;         // 45-degree chamfer on lip overhang and lid edges
tongue_clr = 0.25;         // sliding clearance per face

skirt_face_x = W/2 - skirt_inset;
front_face_y = skirt_inset;
lid_edge_x = skirt_face_x - tongue_clr;
lid_front_y = front_face_y + tongue_clr;

// Dial placement on sloped top
dial_y = D / 2;
dial_z = top_z(0, dial_y);

// Front panel round rocker switch (KCD1-5 or equiv): Φ20mm nominal mounting hole.
// +0.5mm over nominal for FDM print clearance so the switch snaps in cleanly.
power_button_d = 20.5;
power_button_x = 0;      // centered on the front face
power_button_z = 16.0;

// Connector positions: low on the sides, grouped near the front.
conn_z = 13.0;
front_conn_y = 13.0;
conn_pair_spacing = 20.0; // center-to-center
mid_conn_y = front_conn_y + conn_pair_spacing;

// Port labels engraved above each connector hole.
port_label_size = 4.5;
port_label_depth = 0.5;
port_label_gap = 1.5;
port_label_z = conn_z + connector_hole_d/2 + port_label_size/2 + port_label_gap;

function slope_top_z(y, hf=H_front, hr=H_rear, d=D) = hf + (hr - hf) * y / d;
function top_z(x, y, w=W, d=D, hf=H_front, hr=H_rear, h=dome_height) =
    slope_top_z(y, hf, hr, d);
function clamp(v, lo, hi) = min(max(v, lo), hi);
function inner_roof_z(y) =
    floor_thick + slope_top_z(
        clamp(y - wall, 0, D - 2*wall),
        H_front - floor_thick - wall,
        H_rear - floor_thick - wall,
        D - 2*wall
    );
function slope_angle() = atan((H_rear - H_front) / D);
function top_front_round_points(r=top_front_round_r, steps=top_edge_round_segments, eps=0.04) =
    let(
        m = (H_rear - H_front) / D,
        s = sqrt(1 + m*m),
        center_v = r * (s - m),
        tangent_u = r * (1 - m / s),
        tangent_v = -m * tangent_u,
        start_a = atan2(-1, -m),
        end_a = -180
    )
    concat(
        [[-eps, H_front + eps], [tangent_u, H_front - tangent_v]],
        [for (i=[1:steps])
            let(
                a = start_a + (end_a - start_a) * i / steps,
                u = r + r * cos(a),
                v = center_v + r * sin(a)
            )
            [u, H_front - v]
        ],
        [[-eps, H_front - center_v]]
    );

function top_back_round_points(r=top_back_round_r, steps=top_edge_round_segments, eps=0.04) =
    let(
        m = (H_rear - H_front) / D,
        s = sqrt(1 + m*m),
        center_v = r * (m + s),
        tangent_u = r * (1 + m / s),
        tangent_v = m * tangent_u,
        start_a = atan2(-1, m),
        end_a = -180
    )
    concat(
        [[D + eps, H_rear + eps], [D - tangent_u, H_rear - tangent_v]],
        [for (i=[1:steps])
            let(
                a = start_a + (end_a - start_a) * i / steps,
                u = r + r * cos(a),
                v = center_v + r * sin(a)
            )
            [D - u, H_rear - v]
        ],
        [[D + eps, H_rear - center_v]]
    );

module wedge_body(w=W, d=D, hf=H_front, hr=H_rear, r=case_corner_r) {
    rr = min(r, min(w, d) / 2 - 0.01);

    e = edge_round_r;
    s = sqrt(1 + pow((hr - hf) / d, 2)); // slope-normal offset factor

    if (e <= 0) {
        intersection() {
            translate([0, d/2, 0])
                linear_extrude(height=max(hf, hr) + 1)
                    rounded_rect_2d(w, d, rr);

            yz_polygon_prism([[0, 0], [d, 0], [d, hr], [0, hf]], -w/2 - 1, w/2 + 1);
        }
    } else {
        // round the top edges and vertical corners with a sphere sweep,
        // then trim flat at z=0 so the bottom edge stays sharp for printing
        intersection() {
            minkowski() {
                intersection() {
                    translate([0, d/2, 0])
                        linear_extrude(height=max(hf, hr) + 1)
                            rounded_rect_2d(w - 2*e, d - 2*e, max(rr - e, 0.5));

                    yz_polygon_prism(
                        [[e, -e - 1], [d - e, -e - 1], [d - e, hr - e*s], [e, hf - e*s]],
                        -w/2 - 1, w/2 + 1);
                }
                sphere(r=e, $fn=40);
            }
            translate([-w/2 - 1, -1, 0])
                cube([w + 2, d + 2, max(hf, hr) + 2]);
        }
    }
}

module yz_polygon_prism(pts, x_min, x_max) {
    translate([x_min,0,0])
        rotate([0,90,0])
            linear_extrude(height=x_max - x_min)
                polygon(points=[for (p=pts) [-p[1], p[0]]]);
}

module top_back_round_cut() {
    if (top_back_round_r > 0)
        yz_polygon_prism(top_back_round_points(), -W/2 - 1, W/2 + 1);
}

module top_front_round_cut() {
    if (top_front_round_r > 0)
        yz_polygon_prism(top_front_round_points(), -W/2 - 1, W/2 + 1);
}

module rounded_rect_2d(w, d, r) {
    rr = min(r, min(w, d) / 2 - 0.01);
    offset(r=rr)
        square([w - 2*rr, d - 2*rr], center=true);
}

module rounded_plate(w, d, h, r) {
    linear_extrude(height=h, center=true)
        rounded_rect_2d(w, d, r);
}

module rounded_ring_plate(w, d, h, r, ring_w) {
    difference() {
        rounded_plate(w, d, h, r);
        rounded_plate(w - 2*ring_w, d - 2*ring_w, h + 0.4, max(r - ring_w, 0.5));
    }
}

module mirrored_x() {
    children();
    mirror([1, 0, 0]) children();
}

// Prism running along -Y from y1, with a 2D cross-section drawn in XZ.
module xz_profile_prism(pts, y1, len) {
    translate([0, y1, 0])
        rotate([90, 0, 0])
            linear_extrude(height=len)
                polygon(points=pts);
}

module bottom_skirt() {
    difference() {
        // U-shaped band following the case outline, open at the rear, fading
        // out naturally where the rear corner radius pulls inside the lip.
        translate([0, 0, -skirt_h])
            linear_extrude(height=skirt_h)
                difference() {
                    translate([0, D/2]) rounded_rect_2d(W, D, case_corner_r);
                    // Round the front corners so the skirt wall stays a uniform
                    // skirt_inset thick around the case_corner_r arc.  Without
                    // rounding the inner cutout has a sharp 90° corner that
                    // leaves only ~0.9 mm of wall at the front-corner transition,
                    // which prints open.  offset(r) offset(-r) rounds convex
                    // (outer) corners of the square, which become the concave
                    // corners of the U interior after subtraction.
                    offset(r=max(case_corner_r - skirt_inset - edge_chamf, 0.1), $fn=32)
                        offset(r=-max(case_corner_r - skirt_inset - edge_chamf, 0.1))
                            translate([-(skirt_face_x - edge_chamf), front_face_y + edge_chamf])
                                square([2*(skirt_face_x - edge_chamf),
                                        D - front_face_y - edge_chamf + 1]);
                }

        // Channels: vertical capture face, then the 45-degree lip chamfer.
        mirrored_x()
            xz_profile_prism([
                [skirt_face_x - edge_chamf, 0.1],
                [skirt_face_x, 0.1],
                [skirt_face_x, -chan_vert],
                [skirt_face_x - edge_chamf, -chan_vert - edge_chamf]
            ], D + 1, D + 1 - front_face_y);

        yz_polygon_prism([
            [front_face_y + edge_chamf, 0.1],
            [front_face_y, 0.1],
            [front_face_y, -chan_vert],
            [front_face_y + edge_chamf, -chan_vert - edge_chamf]
        ], -skirt_face_x, skirt_face_x);
    }
}

module tpu_foot_pad(h=lid_foot_pad_h) {
    cylinder(d=lid_foot_pad_d, h=h);
}

module lid_foot_pocket_void(x, y) {
    translate([x, y, -lid_thick/2 - 0.02])
        cylinder(d=lid_foot_pocket_d, h=lid_foot_pocket_depth + 0.04);
}

module lid_heatset_seat_void(x, y) {
    seat_top_z = lid_thick/2;
    translate([x, y, 0])
    union() {
        translate([0, 0, seat_top_z - lid_heatset_seat_depth])
            cylinder(d=lid_heatset_seat_d, h=lid_heatset_seat_depth + 0.12);

        translate([0, 0, seat_top_z - lid_heatset_seat_bevel_h])
            cylinder(
                d1=lid_heatset_seat_d,
                d2=lid_heatset_seat_d + 2*lid_heatset_seat_bevel_w,
                h=lid_heatset_seat_bevel_h + 0.08
            );
    }
}

module side_hole_right(y, z, d) {
    translate([W/2 + 0.1, y, z])
        rotate([0,90,0])
            cylinder(d=d, h=12, center=true);
}

module side_hole_left(y, z, d) {
    translate([-W/2 - 0.1, y, z])
        rotate([0,90,0])
            cylinder(d=d, h=12, center=true);
}

module dial_axis_transform() {
    translate([0, dial_y, dial_z])
        rotate([slope_angle(), 0, 0])
            children();
}

module heatset_boss(x, y) {
    side_x = x > 0
        ? W/2 - wall + heatset_rib_wall_overlap
        : -W/2 + wall - heatset_rib_wall_overlap;
    end_y = y > D/2
        ? D - wall + heatset_rib_wall_overlap
        : wall - heatset_rib_wall_overlap;
    support_h = roof_supports_from_bosses
        ? max(heatset_boss_h, inner_roof_z(y) - floor_thick + roof_support_overlap)
        : heatset_boss_h;

    difference() {
        union() {
            // One uniform-diameter column from floor to roof: no overhang step
            // when printed upside down, and stiffer than a boss + thin post.
            translate([x,y,floor_thick])
                cylinder(d=heatset_boss_d, h=support_h);

            // Blend the boss into the corner so there is no crescent gap
            // between the boss and the walls.
            corner_x = side_x - sign(x) * heatset_boss_d/2;
            corner_y = end_y + (y > D/2 ? -1 : 1) * heatset_boss_d/2;
            hull() {
                translate([x, y, floor_thick])
                    cylinder(d=heatset_boss_d, h=support_h);
                translate([corner_x, corner_y, floor_thick])
                    cylinder(d=heatset_boss_d, h=support_h);
            }
        }

        translate([x,y,floor_thick - 0.1])
            cylinder(d=heatset_screw_relief_d, h=support_h + 0.3);

        translate([x,y,floor_thick - 0.1])
            cylinder(d1=heatset_lead_in_d, d2=heatset_insert_bore_d, h=heatset_lead_in_h + 0.1);

        translate([x,y,floor_thick + heatset_lead_in_h - 0.1])
            cylinder(d=heatset_insert_bore_d, h=heatset_insert_depth + 0.1);

        // The bosses are added after the shell is hollowed, so re-cut nearby
        // side connector bores through the boss/rib geometry too.
        side_hole_right(front_conn_y, conn_z, connector_hole_d);
        side_hole_right(mid_conn_y,   conn_z, connector_hole_d);
        side_hole_left(front_conn_y, conn_z, temp_probe_hole_d);
        side_hole_left(mid_conn_y,   conn_z, connector_hole_d);
    }
}

module port_label_right(txt, y) {
    translate([W/2 - port_label_depth, y, port_label_z])
        rotate([90, 0, 90])
            linear_extrude(port_label_depth + 0.1)
                text(txt, size=port_label_size, halign="center", valign="center",
                     font="Liberation Sans:style=Bold");
}

module port_label_left(txt, y) {
    translate([-W/2 + port_label_depth, y, port_label_z])
        rotate([90, 0, -90])
            linear_extrude(port_label_depth + 0.1)
                text(txt, size=port_label_size, halign="center", valign="center",
                     font="Liberation Sans:style=Bold");
}

module shell() {
    difference() {
        wedge_body();
        top_front_round_cut();
        top_back_round_cut();

        // Hollow interior from bottom.
        translate([0, wall, floor_thick])
            wedge_body(
                W - 2*wall,
                D - 2*wall,
                H_front - floor_thick - wall,
                H_rear - floor_thick - wall,
                max(case_corner_r - wall, 0.5)
            );

        // Bottom service opening.
        translate([0, D/2, -0.5])
            cube([bottom_open_w, bottom_open_d, 8], center=true);

        // Centered M5Dial panel cutout: official drawing suggests Ø45 ±0.2 mm.
        dial_axis_transform()
            cylinder(d=dial_mount_hole_d, h=40, center=true);

        // RIGHT SIDE: 12V input + pump output
        side_hole_right(front_conn_y, conn_z, connector_hole_d);
        side_hole_right(mid_conn_y,   conn_z, connector_hole_d);

        // LEFT SIDE: temp probe + heater output
        side_hole_left(front_conn_y, conn_z, temp_probe_hole_d);
        side_hole_left(mid_conn_y,   conn_z, connector_hole_d);

        // FRONT: push-button power switch.
        translate([power_button_x, 0, power_button_z])
            rotate([90, 0, 0])
                cylinder(d=power_button_d, h=12, center=true);

        // Engraved port labels above each hole.
        // RIGHT: power + cooler.  LEFT: temp probe + heater.
        port_label_right("P", front_conn_y);
        port_label_right("C", mid_conn_y);
        port_label_left("T", front_conn_y);
        port_label_left("H", mid_conn_y);

        // Rear lid screw holes. The front and sides are captured by the
        // dovetail; these two screws lock the slide.
        for (x=[-heatset_pos_x,heatset_pos_x])
            translate([x,rear_heatset_pos_y,-1])
                cylinder(d=screw_d, h=12, center=true);
    }

    // Rear heat-set bosses only; the front corners stay clear for the side sockets.
    for (x=[-heatset_pos_x,heatset_pos_x])
        heatset_boss(x, rear_heatset_pos_y);

    // Slide-lid capture skirt below the case bottom.
    bottom_skirt();
}

// Built around z=0 (plate mid-plane). In the case, the lid slides with its
// top face against the case bottom: translate z by -lid_thick/2.
module bottom_lid() {
    difference() {
        // Plate with 45-degree chamfers on the bottom side and front edges;
        // corners rounded to follow the case outline, inset tongue_clr from
        // the lip faces all around.
        intersection() {
            translate([0, D/2, -lid_thick/2])
                linear_extrude(height=lid_thick)
                    rounded_rect_2d(2*lid_edge_x, D - 2*lid_front_y,
                                    case_corner_r - skirt_inset - tongue_clr);

            xz_profile_prism([
                [-(lid_edge_x - edge_chamf), -lid_thick/2],
                [lid_edge_x - edge_chamf, -lid_thick/2],
                [lid_edge_x, -lid_thick/2 + edge_chamf],
                [lid_edge_x, lid_thick/2],
                [-lid_edge_x, lid_thick/2],
                [-lid_edge_x, -lid_thick/2 + edge_chamf]
            ], D + 1, D + 2);

            yz_polygon_prism([
                [lid_front_y, -lid_thick/2 + edge_chamf],
                [lid_front_y + edge_chamf, -lid_thick/2 - 0.1],
                [D, -lid_thick/2 - 0.1],
                [D, lid_thick/2 + 0.1],
                [lid_front_y, lid_thick/2 + 0.1]
            ], -lid_edge_x - 1, lid_edge_x + 1);
        }

        for (x=[-lid_foot_pad_x,lid_foot_pad_x],
             y=[lid_foot_pad_front_y,lid_foot_pad_rear_y])
            lid_foot_pocket_void(x, y);

        for (x=[-heatset_pos_x,heatset_pos_x]) {
            lid_heatset_seat_void(x, rear_heatset_pos_y);

            translate([x,rear_heatset_pos_y,0])
                cylinder(d=screw_d, h=10, center=true);

            translate([x,rear_heatset_pos_y,-lid_thick/2 - 0.1])
                cylinder(d=lid_screw_counterbore_d, h=lid_screw_counterbore_h + 0.1);
        }
    }
}

module screw_hole_plug() {
    tpu_foot_pad();
}

module bottom_screw_plugs_print() {
    for (x=[-screw_plug_print_pitch/2,screw_plug_print_pitch/2])
        translate([x,-screw_plug_print_pitch/2,0])
            tpu_foot_pad(lid_foot_pad_h);

    for (x=[-screw_plug_print_pitch/2,screw_plug_print_pitch/2])
        translate([x,screw_plug_print_pitch/2,0])
            tpu_foot_pad(lid_rear_foot_pad_h);
}

module bottom_screw_plugs_assembled() {
    for (x=[-lid_foot_pad_x,lid_foot_pad_x])
        translate([x,lid_foot_pad_front_y,-lid_thick/2 + lid_foot_pocket_depth - lid_foot_pad_h])
            tpu_foot_pad(lid_foot_pad_h);

    for (x=[-lid_foot_pad_x,lid_foot_pad_x])
        translate([x,lid_foot_pad_rear_y,-lid_thick/2 + lid_screw_counterbore_h - lid_rear_foot_pad_h])
            tpu_foot_pad(lid_rear_foot_pad_h);
}

if (part == "shell") {
    shell();
} else if (part == "lid") {
    bottom_lid();
} else if (part == "feet" || part == "screw_plugs") {
    bottom_screw_plugs_print();
} else if (part == "none") {
    // nothing: lets other files include this one and use the modules
} else {
    // Lid fully slid home: top face against the case bottom plane.
    shell();
    translate([0,0,-lid_thick/2]) bottom_lid();
    translate([0,0,-lid_thick/2]) bottom_screw_plugs_assembled();
}
