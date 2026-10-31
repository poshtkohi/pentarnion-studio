#include "math_text_renderer.hpp"
#include <jkqtmathtext/jkqtmathtext.h>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace penta {
QImage renderMathText(const QString& text, const QFont& requestedFont,
                      const QColor& color, bool latex, QString* error) {
    if (error) error->clear();
    const auto reject=[error](const QString& message) {
        if(error) *error=message;
        return QImage{};
    };
    if(text.toUtf8().size()>4096) return reject(QStringLiteral("A figure label is limited to 4096 UTF-8 bytes."));
    if(text.isEmpty()) {QImage empty(1,1,QImage::Format_ARGB32_Premultiplied);empty.fill(Qt::transparent);return empty;}
    QFont font=requestedFont;
    if(font.pointSizeF()<=0) font.setPointSizeF(14);
    if(!std::isfinite(font.pointSizeF())||font.pointSizeF()>96)
        return reject(QStringLiteral("Figure-label font size is outside the supported range."));
    QImage measuring(1,1,QImage::Format_ARGB32_Premultiplied);
    QPainter probe(&measuring);
    QSize size;
    JKQTMathText renderer;
    if(latex) {
        // This is a mathematical-label subset, not a TeX document interpreter.
        // In particular source cannot change numeric font sizes/dimensions.
        static const QSet<QString> commands=[] {
            const auto names=QStringLiteral(
                "alpha beta gamma delta epsilon varepsilon zeta eta theta vartheta iota kappa lambda mu nu xi omicron pi varpi rho varrho sigma varsigma tau upsilon phi varphi chi psi omega "
                "Gamma Delta Theta Lambda Xi Pi Sigma Upsilon Phi Psi Omega "
                "frac dfrac tfrac sqrt sum prod coprod int iint iiint oint partial nabla infty ell "
                "sin cos tan cot sec csc sinh cosh tanh log ln exp lim min max sup inf det dim gcd "
                "left right lceil rceil lfloor rfloor langle rangle lbrace rbrace vert Vert "
                "mathrm mathbf mathit mathsf mathtt mathbb mathcal mathfrak boldsymbol text textit textbf textrm operatorname "
                "overline underline hat bar vec tilde dot ddot widehat widetilde overbrace underbrace overset underset "
                "dots cdots ldots vdots ddots le leq ge geq neq ne approx sim simeq equiv propto in notin subset subseteq supset supseteq cup cap land lor neg forall exists "
                "pm mp times cdot div star ast circ bullet otimes oplus to rightarrow leftarrow leftrightarrow Rightarrow Leftarrow Leftrightarrow mapsto uparrow downarrow "
                "quad qquad color textcolor boxed phantom begin end displaystyle textstyle").split(QLatin1Char(' '),Qt::SkipEmptyParts);
            return QSet<QString>(names.cbegin(),names.cend());
        }();
        static const QRegularExpression command(QStringLiteral("\\\\([A-Za-z]+)"));
        auto matches=command.globalMatch(text);
        while(matches.hasNext()) {
            const auto name=matches.next().captured(1);
            if(!commands.contains(name))
                return reject(QStringLiteral("Unsupported LaTeX label command: \\%1").arg(name));
        }
        static const QSet<QString> environments{QStringLiteral("matrix"),QStringLiteral("pmatrix"),QStringLiteral("bmatrix"),QStringLiteral("Bmatrix"),QStringLiteral("vmatrix"),QStringLiteral("Vmatrix"),QStringLiteral("array"),QStringLiteral("aligned"),QStringLiteral("align"),QStringLiteral("gathered"),QStringLiteral("cases"),QStringLiteral("smallmatrix")};
        static const QRegularExpression environment(QStringLiteral("\\\\(?:begin|end)\\s*\\{([^}]+)\\}"));
        auto environmentMatches=environment.globalMatch(text);
        while(environmentMatches.hasNext()) {
            const auto name=environmentMatches.next().captured(1);
            if(!environments.contains(name))return reject(QStringLiteral("Unsupported LaTeX label environment: %1").arg(name));
        }
        if(text.count(QLatin1Char('\\'))>256 ||
           text.count(QLatin1Char('^'))+text.count(QLatin1Char('_'))>128)
            return reject(QStringLiteral("LaTeX label complexity exceeds the rendering limit."));
        int depth=0;
        bool escaped=false;
        for(const QChar c:text) {
            if(!escaped&&c==QLatin1Char('{')&&++depth>64)
                return reject(QStringLiteral("LaTeX label nesting exceeds 64 levels."));
            if(!escaped&&c==QLatin1Char('}')&&--depth<0)
                return reject(QStringLiteral("Unmatched closing brace in LaTeX label."));
            if(c==QLatin1Char('\\')) escaped=!escaped;else escaped=false;
        }
        if(depth!=0) return reject(QStringLiteral("Unmatched opening brace in LaTeX label."));
        if(!renderer.useXITS()) return reject(QStringLiteral("The bundled XITS math fonts could not be loaded."));
        renderer.setFontSize(font.pointSizeF());
        renderer.setFontColor(color);
        JKQTMathText::BaseFontOptions options=JKQTMathText::BaseFontDefault;
        if(font.bold()) options|=JKQTMathText::BaseFontBold;
        if(font.italic()) options|=JKQTMathText::BaseFontItalic;
        renderer.setFontOptions(options);
        if(!renderer.parse(text)||renderer.hadErrors())
            return reject(QStringLiteral("Invalid or unsupported LaTeX label: %1").arg(renderer.getErrorList().join(QStringLiteral("; "))));
        const QSizeF measured=renderer.getSize(probe);
        if(!std::isfinite(measured.width())||!std::isfinite(measured.height())||
           measured.width()<0||measured.height()<0||measured.width()>2040||measured.height()>1016)
            return reject(QStringLiteral("Figure label is too large to render (maximum 2048 by 1024 pixels)."));
        size=QSize(static_cast<int>(std::ceil(measured.width()))+8,static_cast<int>(std::ceil(measured.height()))+8);
    } else {
        probe.setFont(font);
        const auto bounds=probe.boundingRect(QRectF(0,0,40000,40000),Qt::AlignLeft|Qt::AlignTop,text);
        size=QSize(static_cast<int>(std::ceil(bounds.width()))+8,static_cast<int>(std::ceil(bounds.height()))+8);
    }
    probe.end();
    if(size.width()>2048||size.height()>1024||size.width()<1||size.height()<1)
        return reject(QStringLiteral("Figure label is too large to render (maximum 2048 by 1024 pixels)."));
    QImage image(std::max(1,size.width()),std::max(1,size.height()),QImage::Format_ARGB32_Premultiplied);
    if(image.isNull()) return reject(QStringLiteral("Cannot allocate a figure-label image."));
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    if(latex) renderer.draw(painter,Qt::AlignCenter,QRectF(image.rect()),false);
    else {painter.setFont(font);painter.setPen(color);painter.drawText(image.rect().adjusted(4,4,-4,-4),Qt::AlignLeft|Qt::AlignTop,text);}
    return image;
}

bool paintMathText(QPainter& painter,const QRectF& target,const QString& text,
                   const QFont& requestedFont,const QColor& color,bool latex,QString* error) {
    // Validate on the same bounded path, then measure on the actual output
    // device: PDF/SVG DPI can differ from the image/screen measurement device.
    const QImage validation=renderMathText(text,requestedFont,color,latex,error);
    if(validation.isNull())return false;
    if(text.isEmpty())return true;
    QFont font=requestedFont;if(font.pointSizeF()<=0&&font.pixelSize()<=0)font.setPointSizeF(14);
    painter.save();painter.setFont(font);
    JKQTMathText renderer;QSizeF measured;
    if(latex){
        if(!renderer.useXITS()){painter.restore();if(error)*error=QStringLiteral("The bundled XITS math fonts could not be loaded.");return false;}
        renderer.setFontSize(font.pointSizeF()>0?font.pointSizeF():font.pixelSize()*72.0/painter.device()->logicalDpiY());renderer.setFontColor(color);
        JKQTMathText::BaseFontOptions options=JKQTMathText::BaseFontDefault;
        if(font.bold())options|=JKQTMathText::BaseFontBold;
        if(font.italic())options|=JKQTMathText::BaseFontItalic;
        renderer.setFontOptions(options);
        if(!renderer.parse(text)||renderer.hadErrors()){painter.restore();if(error)*error=QStringLiteral("The mathematical label could not be drawn.");return false;}
        measured=renderer.getSize(painter);
    }else measured=painter.boundingRect(QRectF(0,0,40000,40000),Qt::AlignLeft|Qt::AlignTop,text).size();
    measured+=QSizeF(8,8);if(!std::isfinite(measured.width())||!std::isfinite(measured.height())||measured.width()<=0||measured.height()<=0){painter.restore();if(error)*error=QStringLiteral("Invalid mathematical label dimensions.");return false;}
    painter.translate(target.topLeft());painter.scale(target.width()/measured.width(),target.height()/measured.height());
    const QRectF bounds(QPointF(0,0),measured);
    if(latex)renderer.draw(painter,Qt::AlignCenter,bounds,false);
    else{painter.setFont(font);painter.setPen(color);painter.drawText(bounds.adjusted(4,4,-4,-4),Qt::AlignLeft|Qt::AlignTop,text);}
    painter.restore();return true;
}
}
