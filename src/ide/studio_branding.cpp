#include "studio_branding.hpp"
#include <QResource>

static void registerStudioResources() { Q_INIT_RESOURCE(pentarnion_branding); }

namespace penta {
void initializeStudioBranding() {
    static const bool registered=[] { registerStudioResources();return true; }();
    Q_UNUSED(registered);
}
QIcon studioApplicationIcon() {
    initializeStudioBranding();
    QIcon icon;
    for(int size:{16,24,32,48,64,128,256,512})
        icon.addFile(QStringLiteral(":/branding/pentarnion-studio-%1.png").arg(size),QSize(size,size));
    icon.addFile(QStringLiteral(":/branding/pentarnion-studio.svg"));
    return icon;
}
}
