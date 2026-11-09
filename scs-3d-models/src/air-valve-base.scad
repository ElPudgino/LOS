$fn = 50;

height = 100;
thick = 15;

servo_win_h = 40.4;
servo_win_w = 20;
servo_hole_d = 4.3;

foot_thick = 6;
foot_wx = 70;
foot_wy = 60;
foot_hole_d = 5;

stif_thick = 10;

// BASE
module base() {
    foot_pleft = -(servo_win_w/2 + foot_wx/2);
    foot_pright = foot_wx/2 - servo_win_w/2;

    linear_extrude(height = thick, center = true)
        polygon(
            points =
            [[servo_win_w/2,  0],
             [-servo_win_w/2, 0],
             [-servo_win_w/2, servo_win_h],
             [servo_win_w/2,  servo_win_h],
             [servo_win_w/2,   servo_win_h + 8],
             [-(servo_win_w/2+5), servo_win_h + 8],
             [-foot_wx/2, -height+foot_thick],
             [foot_wx/2,  -height+foot_thick],
             [servo_win_w/2,   0]]
            );
}

// FOOT
module foot() {
    translate([0,-(height-foot_thick/2)])
        rotate([90,0,0])
        cube(size = [foot_wx, foot_wy, foot_thick], center = true);
}

// STIFFENERS
module stiffeners() {
    rib_gap = 0.25;
    rib_size = (foot_wy - thick) / 2 - rib_gap;

    module rib() {
        linear_extrude(height = stif_thick, center = true)
            polygon(points = [[0,0], [rib_size,0], [0,rib_size], [0,0]]);
    }

    translate([0,-(height-foot_thick),thick/2+rib_gap]) rotate([0,270,0]) rib();
    translate([0,-(height-foot_thick),-(thick/2+rib_gap)]) rotate([0,90,0]) rib();
}

// SERVO HOLES
module servo_holes() {
    servo_h = 40;
    gap = (servo_win_h - servo_h) / 2;

    translate([-5, gap+servo_h+4, 0]) cylinder(d = servo_hole_d, h = thick, center = true);
    translate([5, gap+servo_h+4, 0]) cylinder(d = servo_hole_d, h = thick, center = true);
    translate([-5,-4+gap,0]) cylinder(d = servo_hole_d, h = thick, center = true);
    translate([5,-4+gap,0]) cylinder(d = servo_hole_d, h = thick, center = true);
}

// FOOT HOLES
module foot_holes() {
    from_edge = 8;

    translate([foot_wx/2-from_edge, -height, foot_wy/2-from_edge]) rotate([270, 0, 0]) cylinder(d = foot_hole_d, h = foot_thick);
    translate([-(foot_wx/2-from_edge), -height, foot_wy/2-from_edge]) rotate([270, 0, 0]) cylinder(d = foot_hole_d, h = foot_thick);
    translate([foot_wx/2-from_edge, -height, -(foot_wy/2-from_edge)]) rotate([270, 0, 0]) cylinder(d = foot_hole_d, h = foot_thick);
    translate([-(foot_wx/2-from_edge), -height, -(foot_wy/2-from_edge)]) rotate([270, 0, 0]) cylinder(d = foot_hole_d, h = foot_thick);
}

module base_part() {
    difference() {
        base();
        servo_holes();
    }
}

module foot_part() {
    difference() {
        foot();
        foot_holes();
    }

    stiffeners();
}

translate([0, 0, height])
rotate([90, 0, 0])
difference() {
    union() {
        base_part();
        foot_part();
    }
    union() {
        center = -(height - (foot_wx-thick)/2) - 7;
        big_h = foot_wy/2 - thick/2 - 4;

        translate([0, center, 0]) cylinder(d = 4.5, h = foot_wy, center = true);
        translate([0, center, foot_wy/2 - big_h]) cylinder(d = 8, h = big_h);
        rotate([0, 180, 0]) translate([0, center, foot_wy/2 - big_h]) cylinder(d = 8, h = big_h);

    }
    union() {
        center = foot_wx/2 - 8;
        translate([center, -height, 0]) rotate([270, 0, 0]) cylinder(d = 4.5, h = foot_thick + 8);
        translate([-center, -height, 0]) rotate([270, 0, 0]) cylinder(d = 4.5, h = foot_thick + 8);

        translate([center, -height, 0]) rotate([270, 0, 0]) cylinder(d = 8, h = 3);
        translate([-center, -height, 0]) rotate([270, 0, 0]) cylinder(d = 8, h = 3);
    }
}
