#include <QDir>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "../../app/model/AnnotationModel.h"

class AnnotationModelTest : public QObject {
    Q_OBJECT

private slots:
    void testJsonRoundTrip();
    void testStorageRoundTrip();
};

void AnnotationModelTest::testJsonRoundTrip() {
    PDFAnnotation source;
    source.type = AnnotationType::Underline;
    source.pageNumber = 3;
    source.boundingRect = QRectF(0.1, 0.2, 0.3, 0.04);
    source.content = QStringLiteral("selected text");
    source.color = QColor(QStringLiteral("#336699"));
    source.opacity = 0.55;

    const PDFAnnotation restored = PDFAnnotation::fromJson(source.toJson());
    QCOMPARE(restored.type, AnnotationType::Underline);
    QCOMPARE(restored.pageNumber, 3);
    QCOMPARE(restored.boundingRect, source.boundingRect);
    QCOMPARE(restored.content, source.content);
    QCOMPARE(restored.color, source.color);
    QCOMPARE(restored.opacity, source.opacity);
}

void AnnotationModelTest::testStorageRoundTrip() {
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());
    const QString storagePath =
        QDir(temporaryDir.path()).filePath(QStringLiteral("annotations.json"));

    PDFAnnotation annotation;
    annotation.type = AnnotationType::Note;
    annotation.pageNumber = 4;
    annotation.boundingRect = QRectF(0.5, 0.5, 0.05, 0.05);
    annotation.content = QStringLiteral("remember this");

    AnnotationModel writer;
    writer.setStoragePath(storagePath);
    QVERIFY(writer.addAnnotation(annotation));
    QVERIFY(writer.saveAnnotationsToStorage());

    AnnotationModel reader;
    reader.setStoragePath(storagePath);
    QCOMPARE(reader.getAllAnnotations().size(), 1);
    QCOMPARE(reader.getAllAnnotations().first().pageNumber, 4);
    QCOMPARE(reader.getAllAnnotations().first().content,
             QStringLiteral("remember this"));
}

QTEST_MAIN(AnnotationModelTest)
#include "test_annotation_model.moc"
