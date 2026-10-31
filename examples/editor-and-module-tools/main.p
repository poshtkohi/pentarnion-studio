% Press Ctrl+Space, hover a name, Ctrl+Shift+I, or F12 after Check.
% Help > Feature Rules and Native Modules stays beside the editable source.
record available = module_info();
record linear_algebra = module_info("linalg");
display("Linear algebra module before first use:", linear_algebra);

matrix values = [1,2;3,4];
matrix squared = values*values;
display("Ordered matrix product:", squared);
display("Modules after numerical use:", module_info());

function real square(real value) {
    real result = value*value;
    return result;
}
real answer = square(7);
display("Inspect answer, square and its local result:", answer);
