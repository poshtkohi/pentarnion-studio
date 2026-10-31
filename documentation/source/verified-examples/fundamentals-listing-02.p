function real square(real x) { return x*x; }
integer total = 0;
for(n=1:5) { total = total+n; }
if(total == 15) {
    display("sum:", total, "square:", square(3));
} else {
    error("Unexpected sum");
}
