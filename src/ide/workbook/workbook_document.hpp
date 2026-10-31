#pragma once

#include <QWidget>
#include <QStringList>
#include <memory>

class QTableView;

namespace penta::workbook {

// Native CSV/TSV/XLSX editor. Save never runs .p source or spreadsheet formulas.
class WorkbookDocument final:public QWidget {
    Q_OBJECT
public:
    explicit WorkbookDocument(QWidget* parent=nullptr);
    ~WorkbookDocument()override;
    static bool supportsPath(const QString& path);
    bool openFile(const QString& path,QString* error=nullptr);
    void newWorkbook();
    QString path()const;
    bool isLoading()const;
    bool isModified()const;
    QString statusText()const;
    bool save(QString* error=nullptr);
    bool saveAs(const QString& path,QString* error=nullptr,bool overwrite=false);
    // Explicit non-destructive export; does not replace the workbook document.
    bool exportActiveSheet(const QString& path,QString* error=nullptr,bool overwrite=false)const;
    QTableView* table()const;
    QStringList sheetNames()const;
    int activeSheet()const;
    bool selectSheet(int index);
    bool setCell(int row,int column,const QString& text,QString* error=nullptr);
    QString cellText(int row,int column)const;
    bool pasteText(const QString& text,int row,int column,QString* error=nullptr);
    bool canUndo()const;
    bool canRedo()const;
public slots:
    void undo();
    void redo();
    void cut();
    void copy();
    void paste();
    void clearSelection();
    void selectAll();
signals:
    void modifiedChanged(bool modified);
    void pathChanged(const QString& path);
    void loadingFinished(bool success);
    void undoStateChanged(bool canUndo,bool canRedo);
    void error(const QString& message);
private:
    bool copySelectedCells();
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace penta::workbook
