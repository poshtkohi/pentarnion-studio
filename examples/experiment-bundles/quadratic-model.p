% A saved import: its exact analyzed bytes are captured with the main program.
native function real energy(real displacement,real stiffness){
    return stiffness*displacement*displacement/2;
}
