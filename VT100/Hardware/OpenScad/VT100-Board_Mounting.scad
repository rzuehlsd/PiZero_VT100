/*
Step 1
- basic parameters
- $fn - 200 fragments results in a smooth shape
- minkowski is creating a rounded shape, don't forget to exclude the double radius
*/

$fn=200;
eps = 0.01;

bp_width = 149;
bp_length = 62;
bp_delta = 5;
bp_h1 = 2;
bp_h2 = 3;
board_length = 100;
board_height = 1.6;

dxPCB = (bp_width - board_length) / 2;
echo ("dxPCB = ", dxPCB);
dyPCB = 18;
echo ("dxyCB = ", dyPCB);

module rounded_box(width, length, height, radius)
{
    minkowski() {
        cube(size=[width-radius,length-radius, height]);
        cylinder(r=radius, h=0.01);
    }
}

module backplate(width, length, delta, h1, h2)
{
    translate([delta,delta,0])
    union()
    {
    rounded_box(width, length, h1, 5);
    translate([delta,delta,h1-eps])
        {
        rounded_box(width-2*delta, length-2*delta, h2, 2);
        }
    }
}


// Referenz ist die linke untere Ecke des PCBs
// Umrechnung von PCB Koordinaten in 

function pcb2x(x) = x + dxPCB ;
echo("pcb2x(0) = ", pcb2x(0));

function pcb2y(y) = y + dyPCB + board_height;
echo("pcb2y(0) = ", pcb2y(0));



module rect_hole(x, y, w, h)
{
  translate([pcb2x(x), pcb2y(y), -5*eps]) 
    rounded_box(w,h,bp_h1 + bp_h2 + 10*eps,1);   
}


module round_hole(x,y,rh)
{
    translate([pcb2x(x), pcb2y(y), -5*eps])
        cylinder(h = bp_h1 + bp_h2 + 10*eps, r= rh, center = false);
}


module lasche(x, y, b, t, d)
{
    translate([pcb2x(x), pcb2y(y), bp_h1 + bp_h2 -10*eps]) 
      rounded_box(b, t, d, 1); 
}


//round_hole(10,10,5)


mirror ([1,0,0])
    difference()
    {   // Ausschneiden der Löcher
            union()
            {
            // hintere Trägerplatte für PCB
            backplate(bp_width,bp_length, bp_delta, bp_h1, bp_h2);
            
            // Laschen für board links
            lasche(-5, -6, 15, 3, 10);
                
            // Laschen für board rechts
            lasche(95, -6, 15, 3, 10);
                
            // Lasche für SD Card
            lasche(72, 15, 36, 2, 10);
            }
            
            
            // Power connector x = 8, y=0, w = 9, h = 9
            rect_hole(8,0,9,9);
            
            // DB9 Stecker x = 20, y = 0, w = 30, h = 17
            rect_hole(20,0,31,17);

            // Mini DIN6 x = 56.5, y = 0, w = 12.5, h =14
            rect_hole(56.5, 0, 12.5, 14);
            
            // USB A  x = 74, x = -2, w = 14.5, h = 7
            rect_hole(74, -2, 15, 7);
            
            // SD Card Slot  x = 80, y = 13.5, w = 20, h = 3.5
            rect_hole(80, 17.5, 20, 3.5);
            
            // LED x = 94, y = 5, w = 2, h = 2
            round_hole(94, 5, 1,5);
            
            // ON/OFF Switch x = 5, y = 45, r = 3
            round_hole(5,30,3.15);
        
    }

