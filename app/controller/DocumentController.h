#pragma once

#include <QStandardPaths>
#include <QWidget>
#include "model/DocumentModel.h"
#include "tool.hpp"

class DocumentController : public QObject {
    Q_OBJECT

private:
    DocumentModel* pdfDocument;
    QHash<ActionMap, std::function<void(QWidget*)>> commandMap;
    void initializeCommandMap();

public:
    DocumentController(DocumentModel* pdf);
    ~DocumentController() {};
    void execute(ActionMap actionID, QWidget* context);
};
