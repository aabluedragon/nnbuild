// Shared Mapster32 editor mode, aiming, and interaction conditional VM cases.

case CON_IFIN3DMODE:
            VM_DoConditional(in3dmode());
            continue;

        // ifaimingsprite and -wall also work in 2d mode, but you must "and" with 16383 yourself
        case CON_IFAIMINGSPRITE:
            VM_DoConditional(AIMING_AT_SPRITE || (!in3dmode() && pointhighlight>=16384));
            continue;
        case CON_IFAIMINGWALL:
            VM_DoConditional(AIMING_AT_WALL_OR_MASK || (!in3dmode() && linehighlight>=0));
            continue;
        case CON_IFAIMINGSECTOR:
            VM_DoConditional(AIMING_AT_CEILING_OR_FLOOR);
            continue;
        case CON_IFINTERACTIVE:
            VM_DoConditional(vm.miscflags&VMFLAG_MISC_INTERACTIVE);
            continue;
