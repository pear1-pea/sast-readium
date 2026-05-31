#pragma once

#include "AppComponents.h"

/**
 * Application-level factory.
 *
 * Creates the entire Model + Controller + Application layer,
 * wires Model↔Model connections, and returns the assembled
 * dependency graph as an AppComponents value struct.
 *
 * Example:
 *   auto deps = AppBootstrap::assemble();
 *   MainWindow w(deps);
 *
 * Layer: Application (L3).
 */
class AppBootstrap {
public:
    AppBootstrap() = delete;

    /** Assemble all application components.
     *  @param dpiX / dpiY  Screen DPI for RenderModel (defaults = 72). */
    static AppComponents assemble(double dpiX = 72.0, double dpiY = 72.0);
};
