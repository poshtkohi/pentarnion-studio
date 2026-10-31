#pragma once
#include <QDialog>
#include <QStringList>

namespace penta::desktop {
struct AboutOptions {
    QString version;
    QStringList licenseRoots;
};
QString studioCopyrightText(int year = 0);

// Compact GTG-style information pages; no scientific work or network access.
class AboutDialog final : public QDialog {
public:
    explicit AboutDialog(QWidget* parent = nullptr, const AboutOptions& options = {});
};
}
