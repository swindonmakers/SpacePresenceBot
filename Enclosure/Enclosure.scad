$fn=50;

h=200;
w=287;
hole=3.3;

// false = draw everything to show layout
// true = draw just things that should be cuts(*)
justcuts=true;


// slight less than A4 border
*difference() {
	//TODO: (*)delete this square after exporting DXF
	square([297, 210], center=true);

	hull()
	for (i=[0,1], j=[0,1])
	mirror([i, 0, 0])
	mirror([0, j, 0])
	translate([w/2-5, h/2-5, 0])
		circle(d=10);
}

difference() {
square([w, h], center=true);


translate([w/4, 75, 0])
	lcd(justcuts);

translate([w/4, -72, 0])
rotate([0, 0, 90])
	rfidReader(justcuts);

translate([20, 20, 0])
rotate([0, 0, 90])
	nodeMcu(justcuts);


translate([115, 0, 0])
	buttonBoard(justcuts);


// info sheet mount holes
inset=15;
translate([-w/4, 0, 0])
for (i=[0,1], j=[0,1])
mirror([i, 0, 0])
mirror([0, j, 0])
translate([-(w/4-inset), -(h/2-inset), 0])
	circle(d=hole);

// Backplate mount holes
inset2=7;
for (i=[0,1], j=[0,1])
mirror([i, 0, 0])
mirror([0, j, 0])
translate([-(w/2-inset2), -(h/2-inset2), 0])
	circle(d=hole);
for (i=[0,1])
mirror([i, 0, 0])
translate([-(w/2-inset2), 0, 0])
	circle(d=hole);
for (i=[0,1])
mirror([0, i, 0])
translate([0, -(h/2-inset2), 0])
	circle(d=hole);

}

module button(justcuts=false)
{
	render()
	difference() {
		if (!justcuts)
			square([12, 12] ,center=true);
		
		circle(d=12);
	}
}

module buttonBoard(justcuts=false)
{
	for (i=[0,1], j=[0,1])
	mirror([i, 0, 0])
	mirror([0, j, 0])
	translate([-25/2, -86/2])
		circle(d=hole);

	translate([-25/2, 86/2, 0])
	translate([18.5, -10, 0])
	{
		for (i=[0, 15, 30, 45, 65])
		translate([0, -i, 0]) {
			button(justcuts);
		}

		translate([-60, -5, 0]) {
								   text("1 hour");
			translate([0, -15, 0]) text("2 hours");
			translate([0, -30, 0]) text("3 hours");
			translate([0, -45, 0]) text("4 hours");
			
			translate([-10, -65, 0]) text("checkout");
		}
	}
}

module nodeMcu(justcuts=false)
{
	render()
	difference() {
		if (!justcuts)
		hull()
		for (i=[0,1], j=[0,1])
		mirror([i, 0, 0])
		mirror([0, j, 0])
		translate([48.5/2-2, 25.75/2-2, 0])
			circle(d=4);
		
		for (i=[0,1], j=[0,1])
		mirror([i, 0, 0])
		mirror([0, j, 0])
		translate([43.5/2, 20.5/2, 0])
			circle(d=hole);
	}
}

module lcd()
{
	render()
	difference() {
		if (!justcuts)
			square([80, 36], center=true);

		union() {
			
			for (i=[0,1], j=[0,1])
			mirror([i, 0, 0])
			mirror([0, j, 0])
			translate([75/2, 31/2, 0])
				circle(d=hole);
			
			square([71.5, 24.5], center=true);
		}
	}
}


module rfidReader()
{
	render()
	difference() {
		if (!justcuts)
			square([40, 60], center=true);
	
		union() {
			translate([0, 60/2-15.5, 0])
			for (i=[0,1])
			mirror([i, 0, 0])
			translate([34/2, 0, 0])
				circle(d=hole);
		
		
			translate([0, -60/2+7, 0])
			for (i=[0,1])
			mirror([i, 0, 0])
			translate([25/2, 0, 0])
				circle(d=hole);
		}
		
		
		if (!justcuts)
		translate([0, -10, 0])
		difference() {
			for(i=[1, 5, 8, 13, 18, 22, 29])
			difference() {
				circle(d=i+0.5);
				circle(d=i);
			}
			
			for (i=[0,1])
			mirror([0, i, 0])
			translate([0, -22, 0])
			rotate([0, 0, 45])
				square([30, 30], center=true);
			
		}
	}
}