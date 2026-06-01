#pragma once

#include <QString>

class DocumentModel;
class DocumentOrchestrator;
class PageModel;
class RecentFilesManager;
class RenderModel;
class ThemeManager;

/**
 * Value struct holding all assembled application-level dependencies.
 *
 * Created by AppBootstrap, consumed by MainWindow.
 * All pointers are owned by their respective QObject trees.
 */
struct AppComponents {
    DocumentOrchestrator* documentOrchestrator = nullptr;
    ThemeManager* themeManager = nullptr;

    DocumentModel* documentModel = nullptr;
    PageModel* pageModel = nullptr;
    RenderModel* renderModel = nullptr;

    RecentFilesManager* recentFilesManager = nullptr;
};
