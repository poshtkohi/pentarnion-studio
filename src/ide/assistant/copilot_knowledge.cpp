#include "ide/assistant/copilot_knowledge.hpp"
#include "ide/assistant/offline_help.hpp"
namespace penta::assistant::copilot_knowledge {
QString version(){return QStringLiteral("pentarnion-local-language-0.2");}
QString coreReference(){return QStringLiteral("Pentarnion Studio uses checked .p source, explicitly ordered nonassociative algebra operations, cached native scientific callbacks and explicit execution. Use the supplied versioned local sections for exact syntax; do not invent MATLAB syntax merely because the UI resembles MATLAB.");}
QList<CopilotReference> allReferences(){return HelpDialog::allReferences();}
CopilotReference reference(const QString& id){for(const auto& ref:allReferences())if(ref.id==id)return ref;return {};}
QList<CopilotReference> retrieve(const QString& question,const QString&,int maximum,int characters){
    QList<CopilotReference> result;int retained=0;for(const auto& match:HelpDialog::searchReferences(question,maximum)){auto ref=match.reference;const int available=characters-retained;if(available<128)break;if(ref.text.size()>available)ref.text=ref.text.left(available)+QStringLiteral("\n[Reference excerpt]");retained+=ref.text.size();result.append(std::move(ref));}return result;
}
QString formatReferences(const QList<CopilotReference>& references){QString result;for(const auto& ref:references)result+=QStringLiteral("\n\n[")+ref.id+QStringLiteral("] ")+ref.title+QStringLiteral(" — ")+ref.location+QStringLiteral("\n")+ref.text;return result;}
QStringList suggestedFollowups(const QString& question,const QList<CopilotReference>&){const auto lower=question.toLower();if(lower.contains(QStringLiteral("ode")))return {QStringLiteral("Explain the documented solver options for this equation."),QStringLiteral("Which native callback changes require recompilation?")};if(lower.contains(QStringLiteral("plot"))||lower.contains(QStringLiteral("figure")))return {QStringLiteral("Show a minimal documented plotting example."),QStringLiteral("Explain the difference between figure and subplot layouts.")};return {QStringLiteral("Which local specification supports this syntax?"),QStringLiteral("Explain the multiplication grouping in the supplied expression.")};}
}
