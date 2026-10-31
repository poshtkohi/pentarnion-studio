#pragma once
#include <QIcon>

namespace penta {
// Explicit initialization also retains the resource object in static IDE builds.
void initializeStudioBranding();
QIcon studioApplicationIcon();
}
