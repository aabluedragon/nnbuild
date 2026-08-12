// Shared Mapster32 unknown-instruction VM default handler.

default:
            VM_ScriptInfo();

            OSD_Printf("\nAn error has occurred in the Mapster32 virtual machine.\n\n"
                       "Please e-mail the file mapster32.log along with every M32 file\n"
                       "you're using and instructions how to reproduce this error to\n"
                       "development@voidpoint.com.\n\n"
                       "Thank you!\n");
            vm.flags |= VMFLAG_ERROR;
            Bfflush(NULL);
            return 1;
