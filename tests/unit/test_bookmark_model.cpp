#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "../../app/model/BookmarkModel.h"

class BookmarkModelTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void testAddAndRejectDuplicatePage();
    void testPersistsAcrossInstances();
    void testUpdateAndRemove();

private:
    QTemporaryDir m_storageDir;
};

void BookmarkModelTest::initTestCase() {
    QCoreApplication::setOrganizationName("SASTReadiumTest");
    QCoreApplication::setApplicationName("BookmarkModelTest");
    QVERIFY(m_storageDir.isValid());
}

void BookmarkModelTest::cleanup() {
    if (m_storageDir.isValid()) {
        QFile::remove(QDir(m_storageDir.path()).filePath("bookmarks.json"));
    }
}

void BookmarkModelTest::testAddAndRejectDuplicatePage() {
    BookmarkModel model(nullptr,
                        QDir(m_storageDir.path()).filePath("bookmarks.json"));
    const QString path = QDir::temp().filePath("bookmark-model-a.pdf");

    QVERIFY(model.addBookmark(Bookmark(path, 2, "Chapter 3")));
    QVERIFY(!model.addBookmark(Bookmark(path, 2, "Duplicate")));
    QVERIFY(model.addBookmark(Bookmark(path, 3, "Chapter 4")));
    QCOMPARE(model.getBookmarksForDocument(path).size(), 2);
}

void BookmarkModelTest::testPersistsAcrossInstances() {
    const QString path = QDir::temp().filePath("bookmark-model-b.pdf");
    const QString storagePath =
        QDir(m_storageDir.path()).filePath("bookmarks.json");
    {
        BookmarkModel writer(nullptr, storagePath);
        QVERIFY(writer.addBookmark(Bookmark(path, 7, "Saved page")));
    }

    BookmarkModel reader(nullptr, storagePath);
    const QList<Bookmark> bookmarks = reader.getBookmarksForDocument(path);
    QCOMPARE(bookmarks.size(), 1);
    QCOMPARE(bookmarks.first().pageNumber, 7);
    QCOMPARE(bookmarks.first().title, QStringLiteral("Saved page"));
}

void BookmarkModelTest::testUpdateAndRemove() {
    const QString path = QDir::temp().filePath("bookmark-model-c.pdf");
    BookmarkModel model(nullptr,
                        QDir(m_storageDir.path()).filePath("bookmarks.json"));
    Bookmark bookmark(path, 1, "Original");
    QVERIFY(model.addBookmark(bookmark));

    bookmark.title = "Renamed";
    QVERIFY(model.updateBookmark(bookmark.id, bookmark));
    QCOMPARE(model.getBookmark(bookmark.id).title, QStringLiteral("Renamed"));

    QVERIFY(model.removeBookmark(bookmark.id));
    QVERIFY(model.getBookmark(bookmark.id).id.isEmpty());
}

QTEST_MAIN(BookmarkModelTest)
#include "test_bookmark_model.moc"
