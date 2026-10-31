// Adapted from GTG Cell Studio desktop components; scientific engine remains separate.
#pragma once
#include <QList>
#include <QString>
#include <QStringList>

namespace penta::assistant {
// Locally indexed reference metadata. IDs/locations are made by the application;
// document contents remain data, not provider instructions. Retrieval itself
// performs no file, network or embedding reads.
struct CopilotReference {
    QString id;
    QString title;
    QString location;
    QString text;
};
namespace copilot_knowledge {
QString version();
QString coreReference();
QList<CopilotReference> allReferences();
CopilotReference reference(const QString& id);
QList<CopilotReference> retrieve(const QString& question,
    const QString& modelContext = {}, int maxSections = 5,
    int maxCharacters = 18000);
QString formatReferences(const QList<CopilotReference>&);
QStringList suggestedFollowups(const QString& question,
    const QList<CopilotReference>& references = {});
}
}
