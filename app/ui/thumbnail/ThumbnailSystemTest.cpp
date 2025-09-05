#include "ThumbnailWidget.h"
#include "ThumbnailModel.h"
#include "ThumbnailDelegate.h"
#include "ThumbnailListView.h"
#include "ThumbnailGenerator.h"
#include <QApplication>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QDebug>
#include <poppler-qt6.h>

/**
 * @brief 缩略图系统测试类
 * 
 * 用于测试和验证缩略图系统的各个组件功能
 */
class ThumbnailSystemTest : public QMainWindow
{
    Q_OBJECT

public:
    explicit ThumbnailSystemTest(QWidget* parent = nullptr)
        : QMainWindow(parent)
        , m_thumbnailModel(nullptr)
        , m_thumbnailDelegate(nullptr)
        , m_thumbnailView(nullptr)
    {
        setupUI();
        setupConnections();
    }

private slots:
    void openTestDocument()
    {
        QString filePath = QFileDialog::getOpenFileName(
            this, "选择PDF文件", "", "PDF文件 (*.pdf)");
        
        if (filePath.isEmpty()) {
            return;
        }
        
        // 加载PDF文档
        std::unique_ptr<Poppler::Document> doc(Poppler::Document::load(filePath));
        if (!doc) {
            QMessageBox::warning(this, "错误", "无法加载PDF文件");
            return;
        }
        
        // 配置文档
        doc->setRenderBackend(Poppler::Document::ArthurBackend);
        doc->setRenderHint(Poppler::Document::Antialiasing, true);
        doc->setRenderHint(Poppler::Document::TextAntialiasing, true);
        
        // 创建shared_ptr
        std::shared_ptr<Poppler::Document> sharedDoc(doc.release());
        
        // 设置到模型
        m_thumbnailModel->setDocument(sharedDoc);
        
        // 更新UI
        m_pageCountLabel->setText(QString("总页数: %1").arg(sharedDoc->numPages()));
        m_loadButton->setEnabled(true);
        m_sizeSpinBox->setEnabled(true);
        m_qualitySpinBox->setEnabled(true);
        
        qDebug() << "Loaded PDF with" << sharedDoc->numPages() << "pages";
    }
    
    void updateThumbnailSize()
    {
        int size = m_sizeSpinBox->value();
        QSize thumbnailSize(size, size * 4 / 3); // 4:3 比例
        
        m_thumbnailView->setThumbnailSize(thumbnailSize);
        m_thumbnailModel->setThumbnailSize(thumbnailSize);
        m_thumbnailDelegate->setThumbnailSize(thumbnailSize);
        
        qDebug() << "Updated thumbnail size to" << thumbnailSize;
    }
    
    void updateThumbnailQuality()
    {
        double quality = m_qualitySpinBox->value() / 100.0;
        m_thumbnailModel->setThumbnailQuality(quality);
        
        qDebug() << "Updated thumbnail quality to" << quality;
    }
    
    void refreshThumbnails()
    {
        if (m_thumbnailModel) {
            m_thumbnailModel->refreshAllThumbnails();
            qDebug() << "Refreshed all thumbnails";
        }
    }
    
    void onPageClicked(int pageNumber)
    {
        m_statusLabel->setText(QString("点击了第 %1 页").arg(pageNumber + 1));
        qDebug() << "Page clicked:" << pageNumber;
    }
    
    void onPageDoubleClicked(int pageNumber)
    {
        m_statusLabel->setText(QString("双击了第 %1 页").arg(pageNumber + 1));
        qDebug() << "Page double-clicked:" << pageNumber;
    }

private:
    void setupUI()
    {
        setWindowTitle("缩略图系统测试");
        setMinimumSize(800, 600);
        
        QWidget* centralWidget = new QWidget(this);
        setCentralWidget(centralWidget);
        
        QHBoxLayout* mainLayout = new QHBoxLayout(centralWidget);
        
        // 左侧控制面板
        QWidget* controlPanel = new QWidget();
        controlPanel->setFixedWidth(200);
        QVBoxLayout* controlLayout = new QVBoxLayout(controlPanel);
        
        // 文件加载
        QPushButton* openButton = new QPushButton("打开PDF文件");
        connect(openButton, &QPushButton::clicked, this, &ThumbnailSystemTest::openTestDocument);
        controlLayout->addWidget(openButton);
        
        // 页数显示
        m_pageCountLabel = new QLabel("总页数: 0");
        controlLayout->addWidget(m_pageCountLabel);
        
        // 缩略图尺寸控制
        controlLayout->addWidget(new QLabel("缩略图尺寸:"));
        m_sizeSpinBox = new QSpinBox();
        m_sizeSpinBox->setRange(50, 300);
        m_sizeSpinBox->setValue(120);
        m_sizeSpinBox->setSuffix(" px");
        m_sizeSpinBox->setEnabled(false);
        connect(m_sizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &ThumbnailSystemTest::updateThumbnailSize);
        controlLayout->addWidget(m_sizeSpinBox);
        
        // 质量控制
        controlLayout->addWidget(new QLabel("渲染质量:"));
        m_qualitySpinBox = new QSpinBox();
        m_qualitySpinBox->setRange(50, 300);
        m_qualitySpinBox->setValue(100);
        m_qualitySpinBox->setSuffix(" %");
        m_qualitySpinBox->setEnabled(false);
        connect(m_qualitySpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &ThumbnailSystemTest::updateThumbnailQuality);
        controlLayout->addWidget(m_qualitySpinBox);
        
        // 刷新按钮
        m_loadButton = new QPushButton("刷新缩略图");
        m_loadButton->setEnabled(false);
        connect(m_loadButton, &QPushButton::clicked, this, &ThumbnailSystemTest::refreshThumbnails);
        controlLayout->addWidget(m_loadButton);
        
        // 状态显示
        m_statusLabel = new QLabel("请打开PDF文件");
        m_statusLabel->setWordWrap(true);
        controlLayout->addWidget(m_statusLabel);
        
        controlLayout->addStretch();
        
        // 右侧缩略图视图
        m_thumbnailModel = new ThumbnailModel(this);
        m_thumbnailDelegate = new ThumbnailDelegate(this);
        m_thumbnailView = new ThumbnailListView(this);
        
        m_thumbnailView->setThumbnailModel(m_thumbnailModel);
        m_thumbnailView->setThumbnailDelegate(m_thumbnailDelegate);
        
        // 布局
        mainLayout->addWidget(controlPanel);
        mainLayout->addWidget(m_thumbnailView, 1);
    }
    
    void setupConnections()
    {
        // 连接缩略图点击信号
        connect(m_thumbnailView, &ThumbnailListView::pageClicked,
                this, &ThumbnailSystemTest::onPageClicked);
        connect(m_thumbnailView, &ThumbnailListView::pageDoubleClicked,
                this, &ThumbnailSystemTest::onPageDoubleClicked);
        
        // 连接模型信号
        connect(m_thumbnailModel, &ThumbnailModel::thumbnailLoaded,
                this, [this](int pageNumber) {
                    m_statusLabel->setText(QString("第 %1 页缩略图已加载").arg(pageNumber + 1));
                });
        
        connect(m_thumbnailModel, &ThumbnailModel::thumbnailError,
                this, [this](int pageNumber, const QString& error) {
                    m_statusLabel->setText(QString("第 %1 页加载错误: %2").arg(pageNumber + 1).arg(error));
                });
    }

private:
    ThumbnailModel* m_thumbnailModel;
    ThumbnailDelegate* m_thumbnailDelegate;
    ThumbnailListView* m_thumbnailView;
    
    QLabel* m_pageCountLabel;
    QSpinBox* m_sizeSpinBox;
    QSpinBox* m_qualitySpinBox;
    QPushButton* m_loadButton;
    QLabel* m_statusLabel;
};

// 测试主函数
int runThumbnailSystemTest(int argc, char* argv[])
{
    QApplication app(argc, argv);
    
    ThumbnailSystemTest testWindow;
    testWindow.show();
    
    return app.exec();
}

#include "ThumbnailSystemTest.moc"
