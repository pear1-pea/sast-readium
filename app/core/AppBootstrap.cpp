#include "AppBootstrap.h"
#include "controller/ActionDispatcher.h"
#include "controller/DocumentOrchestrator.h"
#include "managers/RecentFilesManager.h"
#include "managers/ThemeManager.h"
#include "model/DocumentModel.h"
#include "model/PageModel.h"
#include "model/RenderModel.h"

AppComponents AppBootstrap::assemble(double dpiX, double dpiY) {
    AppComponents deps;

    // ── Layer 1: Model ──
    deps.renderModel = new RenderModel(dpiX, dpiY);
    deps.documentModel = new DocumentModel(deps.renderModel);
    deps.pageModel = new PageModel(deps.renderModel);
    deps.recentFilesManager = new RecentFilesManager();

    // ── Layer 3: Application (Controllers + Orchestrators) ──
    deps.themeManager = new ThemeManager();
    deps.actionDispatcher =
        new ActionDispatcher(deps.documentModel, deps.recentFilesManager);
    deps.documentOrchestrator =
        new DocumentOrchestrator(deps.documentModel, deps.pageModel,
                                 deps.renderModel, deps.recentFilesManager);

    return deps;
}
