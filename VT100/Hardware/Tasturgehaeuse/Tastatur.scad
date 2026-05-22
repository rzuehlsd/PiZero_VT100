// VT100 Terminal Replica 66% Tastatur

$fn = 200;     // Fix 100 Facetten pro Kreis (langsam bei großen Objekten)
$fa = 1;       // Minimaler Winkel 1° pro Facette (empfohlen)
$fs = 0.2;     // Minimale Facettengröße 0.2mm (3D-Druck-optimiert)


// Masse der Tastatur Replika
function scale(x) = 0.66 * x;
fuss = 5;

kb_x = scale(457);      //302;         // Breite
kb_y = scale(203.2);    //134;         // Tiefe
kb_z_vorn = scale(48 - fuss);  // 30          //Hoehe vorne
kb_z_hinten = scale(89 - fuss);// 50;         // Hoehe hinten

echo("kb_x =", kb_x);
echo("kb_y =", kb_y);
echo("kb_z_vorn =", kb_z_vorn);
echo("kb_z_hinten =", kb_z_hinten);

// Aussenmasse der Tastatur
t_x = 293;          // Breite;
t_y = 103;          // Tiefe
t_z_vorn = 24;      // Hoehe vorn
t_z_hinten = 32;    // Hoehe hinten





// VT100 Tastatur Gehäuse


module ausschnitt(){
  dx = (kb_x - t_x)/2;
  dy = (kb_y - t_y)/2;
  dz_vorn = kb_z_vorn - t_z_vorn;
  dz_hinten = kb_z_hinten - t_z_hinten; 

  color("red")
    hull(){
        translate([dx, dy, dz_vorn])
            cube([t_x, 1, t_z_vorn + 10]);
        
        translate([dx, kb_y - dy, dz_hinten])
        rotate([7.5,0,0])
            cube([t_x, 1, t_z_hinten + 10]);
    }

}

module usb_stecker(){
    color("green")
        translate([kb_x/2, kb_y -10, kb_z_hinten - t_z_hinten + 10])
            cube([40, 25, 20], center = true);
}

module nut(d){
    color("Blue")
        translate([0,0, kb_z_vorn/2])
        difference(){
            cube([kb_x + d, kb_y + d, d]);
            translate([d/2, d/2, 0])
            cube([kb_x - d, kb_y - d , d]);
    }
}

module tastatur(breite, tiefe, hoehe_vorne, hoehe_hinten) {
    r_vorn = hoehe_vorne/2;
    r_hinten = 10;

   hull() {
    // Vorne
    translate([0, r_vorn, r_vorn])
            rotate([0,90,0])
                cylinder(h=breite, r=r_vorn, center= false);

    union(){
        translate([0, tiefe-r_hinten, r_hinten])
            rotate([0,90,0])
                cylinder(h=breite, r=r_hinten, center= false);
        translate([0, tiefe-r_hinten, hoehe_hinten-r_hinten])
            rotate([0,90,0])
                cylinder(h=breite, r=r_hinten, center= false);
        }
    }
}



// VT100 Tastatur Gehäuse in 66%
// tastatur(302, 134, 32, 59);

render() // wird sonst nicht korrekt angezeigt in preview
{
    difference()
    {
    tastatur(kb_x, kb_y, kb_z_vorn, kb_z_hinten);
    ausschnitt();
    usb_stecker();
    nut(1);
    }
}


