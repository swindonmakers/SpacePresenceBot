$fn=50;

h=15;

difference(){

	linear_extrude(h)
	difference() {

		square([35, 35]);
		
		translate([0, 3, 0])
			square([10, 3.5]);
		translate([0, 22+3, 0])
			square([10, 3.5]);
		
		da=6;
		
		translate([13, 0, 0])
			square([35, 28.5-da/2]);
		
		translate([13+da/2, 28.5-da/2, 0])
			circle(d=da);

		translate([13+da/2, 0, 0])
			square([35, 28.5]);
		
		translate([35-da/2, 28.5+da/2])
		rotate([0, 0, 270])
		difference() {
			square([da, da]);
			circle(d=da);
		}

		translate([13-da/2, da/2])
		rotate([0, 0, 270])
		difference() {
			square([da, da]);
			circle(d=da);
		}
	}
	
	translate([24, 28, h/2])
	rotate([-90, 0, 0])
		cylinder(d=6, h=10);
}




