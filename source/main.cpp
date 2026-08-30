#include <3ds.h>

#include "draw.h"
#include "app.h"

// Referenced by wifi.cpp for all SD-card I/O.
FS_Archive sdmcArchive;

int main(int, char**)
{
    cfguInit();

    FSUSER_OpenArchive(&sdmcArchive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""));

    if (draw::init())
        app::run();

    draw::exit();

    FSUSER_CloseArchive(sdmcArchive);
    cfguExit();
    return 0;
}
