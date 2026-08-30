#pragma once
#include <citro2d.h>
#include <string>

// Outer-camera QR scanner. Feeds frames to quirc and exposes a live preview
// image for the top screen.
namespace qrscan
{
    bool init();
    void exit();

    // Pump the camera once. Returns true (and fills `payload`) when a QR code
    // was successfully decoded on this call.
    bool update(std::string& payload);

    // Live preview (valid between init() and exit()).
    C2D_Image preview();
    bool      hasPreview();
    bool      failed();      // camera could not be started
}
