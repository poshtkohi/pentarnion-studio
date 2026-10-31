// Adapted from GTG Cell Studio desktop components; scientific engine remains separate.
#pragma once
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace penta::assistant {
struct CopilotSharedContext {QString id,title,text;};
struct CopilotPreparedTurn {
    QString text;
    QHash<QString,QByteArray> identities;
    int added=0,reused=0,removed=0;
};
// A request-local ledger. Context is committed only after a successful reply,
// so a failed/cancelled upload cannot make a later request omit required data.
class CopilotContextLedger {
public:
    CopilotPreparedTurn prepare(const QString& question,const QList<CopilotSharedContext>&)const;
    void commit(const CopilotPreparedTurn& turn){identities_=turn.identities;}
    void clear(){identities_.clear();}
private:
    QHash<QString,QByteArray> identities_;
};
QString copilotResponsePolicy();
QStringList copilotFollowups(const QString& question);
}
