#include "NDSSystem.h"
#include "emufile.h"
#include "MMU.h"
#include "path.h"
#include "rasterize.h"
#include "saves.h"
#include "SPU.h"

volatile bool execute = false;
SoundInterface_struct* SNDCoreList[] = {&SNDDummy, nullptr};
GPU3DInterface* core3DList[] = {&gpu3DNull, &gpu3DRasterize, nullptr};

// The browser writes the selected files into this module's in-memory filesystem.
extern "C" int make_template()
{
    CommonSettings.num_cores = 1;
    if (NDS_Init() != 0) return 1;
    int status = 0;
    if (NDS_LoadROM("/game.nds") <= 0) {
        status = 2;
    } else {
        // importData resets the console. Use an absolute MEMFS path so that
        // the backup device can reopen its new .dsv after that reset.
        path.setpath(path.BATTERY, std::string("/tmp"));
        NDS_Reset();
        if (!MMU_new.backupDevice.importData("/import.sav")) {
            status = 3;
        } else {
            EMUFILE_FILE output("/template.dst", "wb");
            if (output.fail() || !savestate_save(output, Z_NO_COMPRESSION)) status = 4;
        }
    }
    NDS_DeInit();
    return status;
}
