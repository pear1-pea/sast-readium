#include "DocumentController.h"

void DocumentController::initializeCommandMap() {
    commandMap = {
        {ActionMap::openFile,
         [this](QWidget* ctx) {
             QString filePath = QFileDialog::getOpenFileName(
                 ctx, tr("Open PDF"),
                 QStandardPaths::writableLocation(
                     QStandardPaths::DocumentsLocation),
                 tr("PDF Files (*.pdf)"));
             pdfDocument->openFromFile(filePath);
         }},
        {ActionMap::saveFile, [this](QWidget* ctx) { /*....save()....*/ }}};
}

DocumentController::DocumentController(DocumentModel* pdf) : pdfDocument(pdf) {
    initializeCommandMap();
}

void DocumentController::execute(ActionMap actionID, QWidget* context) {
    qDebug() << "EventID:" << actionID << "context" << context;

    auto it = commandMap.find(actionID);
    if (it != commandMap.end()) {
        (*it)(context);
    } else {
        qWarning() << "Unknown action ID:" << static_cast<int>(actionID);
    }
}