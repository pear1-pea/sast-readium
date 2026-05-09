# 主题适配修复工作日志

**日期**：2026-05-07 ~ 2026-05-08
**任务**：修复 ToolBar 和 StatusBar 控件主题适配问题

---

## 背景

**起始任务**：移除 PDFViewer 内部工具栏，将缩放控件移到 StatusBar
**已完成**：ToolBar 和 StatusBar 重构完成，编译通过

---

## 用户提出的问题（按时间顺序）

### 第一轮问题（2026-05-07）

#### 问题 1：主题适配
**描述**：视图框（viewModeCombo）和页数框（pageSpinBox）不随日间模式而变白

**定位过程**：
1. 检查 `ToolBar::applyToolBarStyle()` 的实现
2. 发现遍历 `actions()` 时，`widgetForAction()` 返回的是容器 QWidget，不是内部的 QSpinBox/QComboBox
3. 探查验证：
   ```
   Action: "" Widget: QWidget(0x60000132f150) Type: QWidget  ← 容器
     └─ pageSpinBox: QSpinBox(0x60000132f1b0) parent: QWidget(0x60000132f150)
   ```
4. 确认：样式只应用到容器，不影响内部控件

**根本原因**：
- pageSpinBox 和 viewModeCombo 是嵌套在容器里的
- 通过 `addWidget(容器)` 添加到 toolbar
- `applyToolBarStyle()` 对容器应用样式，但不影响内部控件

#### 问题 2：缩略图加载速度
**描述**：用户感觉缩略图加载慢

**调研结果**：
- 代码已经很优化（异步、优先级队列、DPI缓存、批处理）
- 参考资料：
  - [PDF to Thumbnail in Milliseconds](https://www.yasindu.me/blog/render-once-cache-forever)
  - [kb47/pdf-poppler](https://github.com/kb47/pdf-poppler)
  - [Qt QPixmapCache](https://doc.qt.io/qt-6.5/qpixmapcache.html)

**可优化方向**：
1. 磁盘缓存（避免重复渲染）
2. 可见区域优先渲染
3. 微调参数（并发数、DPI、尺寸）

#### 问题 3：右侧栏功能
**描述**：
- "属性" 和 "工具" 标签页是空的（占位符）
- "调试" 是什么？
- 拖到边缘消失后如何恢复

**定位结果**：
- 属性/工具：占位符，功能未实现
- 调试：`DebugLogPanel`，显示日志输出
- RightSideBar 是普通 QWidget，通过动画控制显示/隐藏

---

### 第二轮问题（深入探查）

#### 问题 4：控件创建方式差异
**用户质疑**：为什么 pageSpinBox 和 viewModeCombo 的创建方式与其他控件不同？

**回答**：
- **其他控件（按钮）**：通过 `addAction(QAction*)` 添加，toolbar 自动创建 QToolButton
- **pageSpinBox/viewModeCombo**：手动创建容器 → 嵌套控件 → `addWidget(容器)`

**历史原因**：
- 项目曾有 `WidgetFactory`，但只能创建 QPushButton（Command 模式）
- WidgetFactory 已被删除（在 refactor/log 分支）
- 从未有统一的 QSpinBox/QComboBox 创建机制

#### 问题 5：是否应该统一？
**用户选择**：方案 2 - 重构成 QWidgetAction（更优雅）

---

### 第三轮问题（实现后发现的问题）

#### 问题 6：视图下拉框还是黑色
**原因**：`applyToolBarStyle()` 对所有 widget 应用 `getButtonStyleSheet()`，覆盖了 ComboBox 样式

**修复**：只对 QToolButton 应用按钮样式，跳过其他控件
```cpp
if (qobject_cast<QToolButton*>(widget)) {
    widget->setStyleSheet(STYLE.getButtonStyleSheet());
}
```

#### 问题 7：底部状态栏的缩放控件还是黑色
**原因**：StatusBar 的 `zoomPercentSpinBox` 根本没有应用样式

**发现**：ToolBar 和 StatusBar 不是统一创建控件的

#### 问题 8：顶部控件在夜间模式下是白色（最新）
**原因**：样式在创建时固定（`setStyleSheet(STYLE.getSpinBoxStyleSheet())`），切换主题时不更新

**需要**：监听 `StyleManager::themeChanged` 信号，重新应用样式

#### 问题 9：能否统一创建控件？（最新）
**用户要求**：ToolBar 和 StatusBar 应该统一创建控件

**方案**：
- 方案 A：创建 ControlFactory（轻量）
- 方案 B：在 StyleManager 中统一管理（推荐）

---

## 已完成的工作

### 1. StyleManager 扩展（Phase 1）
- ✅ 添加 `getSpinBoxStyleSheet()` / `getComboBoxStyleSheet()` 样式表方法
- ✅ 支持 Light/Dark 主题
- ✅ 参考资料：
  - [Qt Style Sheets Examples](https://doc.qt.io/qt-6/stylesheet-examples.html)
  - [QDarkStyleSheet](https://github.com/ColinDuquesnoy/QDarkStyleSheet)
  - [QSpinBox Stylesheet Gist](https://gist.github.com/ShenTengTu/1c0ec10ca969aefd19e1d9502c076870)

### 2. ToolBar 重构
- ✅ pageSpinBox 改用 QWidgetAction 包装
- ✅ viewModeCombo 改用 QWidgetAction 包装
- ✅ 创建时应用样式表
- ✅ `applyToolBarStyle()` 只对 QToolButton 应用按钮样式

### 3. 头文件更新
- ✅ ToolBar.h 添加 `#include <QWidgetAction>`
- ✅ StyleManager.h 添加样式表方法声明

### 4. P0 - 主题切换不更新样式
- ✅ ToolBar 监听 `themeChanged` → `applyToolBarStyle()`
- ✅ StatusBar 监听 `themeChanged` → lambda 刷新所有控件
- ✅ `applyToolBarStyle()` 内重新应用 viewModeCombo 样式
- ✅ StatusBar 内重新应用 pageSpinBox/zoomPercentSpinBox/zoomSlider 样式

### 5. P1 - StatusBar 缩放控件没有样式
- ✅ `zoomPercentSpinBox` 创建时应用样式表
- ✅ `zoomSlider` 创建时应用样式表

### 6. P2 - 控件创建不统一（方案 B：StyleManager 管理）
- ✅ StyleManager 添加 `createSpinBox()` / `createComboBox()` / `createSlider()` 工厂方法
- ✅ 内部控件注册表：`registerWidget()` + `reThemeAll()` + `QPointer` 安全删除
- ✅ ToolBar: viewModeCombo 改用工厂创建
- ✅ StatusBar: pageSpinBox/zoomPercentSpinBox/zoomSlider 改用工厂创建
- ✅ 手动 `connect(themeChanged)` 移除，工厂自动管理主题切换

---

## 待解决的问题

### P0 - 主题切换不更新样式 ✅ 已完成
**问题**：
- pageSpinBox, viewModeCombo, zoomPercentSpinBox 在切换主题时不更新

**需要做**：
1. 监听 `StyleManager::themeChanged` 信号
2. 重新应用样式到这些控件

**实现方案**：
```cpp
// ToolBar 构造函数
connect(&STYLE, &StyleManager::themeChanged, this, &ToolBar::applyToolBarStyle);

// StatusBar 构造函数
connect(&STYLE, &StyleManager::themeChanged, this, &StatusBar::applyStatusBarStyle);
```

### P1 - StatusBar 缩放控件没有样式 ✅ 已完成
**问题**：
- `zoomPercentSpinBox` 没有应用样式表

**需要做**：
1. 在 `setupZoomControls()` 中应用样式：
   ```cpp
   zoomPercentSpinBox->setStyleSheet(STYLE.getSpinBoxStyleSheet());
   ```
2. 监听主题变化

### P2 - 控件创建不统一 ✅ 已完成
**问题**：
- ToolBar 和 StatusBar 各自创建控件
- 没有统一的工厂或管理机制

**需要做**：
1. 设计统一的控件创建方案
2. 自动管理主题切换

**方案选项**：
- **方案 A**：创建 ControlFactory
  ```cpp
  class ControlFactory {
  public:
      static QSpinBox* createThemedSpinBox(QWidget* parent);
      static QComboBox* createThemedComboBox(QWidget* parent);
  };
  ```
- **方案 B**：在 StyleManager 中统一管理（推荐）
  ```cpp
  QSpinBox* spinBox = STYLE.createSpinBox(parent);
  // 自动注册到主题管理器，切换主题时自动更新
  ```

### P3 - 缩略图性能优化（可选）
**可选优化**：
- 磁盘缓存
- 可见区域优先
- 参数微调

### P4 - 右侧栏功能完善（可选）
**可选功能**：
- 实现 "属性" 标签页
- 实现 "工具" 标签页
- 添加恢复机制

---

## 技术细节记录

### Qt QToolBar 控件添加方式

#### 方式 1：addAction(QAction*)
```cpp
QAction* action = new QAction("Button", this);
addAction(action);
```
- toolbar 自动创建 QToolButton
- `widgetForAction()` 返回 QToolButton
- 出现在 `actions()` 列表中

#### 方式 2：addWidget(QWidget*)
```cpp
QWidget* widget = new QWidget(this);
addWidget(widget);
```
- 返回一个 QAction
- `widgetForAction()` 返回该 widget
- 出现在 `actions()` 列表中

#### 方式 3：QWidgetAction（推荐）
```cpp
QWidgetAction* action = new QWidgetAction(this);
action->setDefaultWidget(customWidget);
addAction(action);
```
- 统一使用 QAction 接口
- `widgetForAction()` 返回自定义控件
- 出现在 `actions()` 列表中

### StyleManager 样式表参数映射
```cpp
.arg(surfaceColor().name())      // %1 - background
.arg(borderColor().name())       // %2 - border
.arg(textColor().name())         // %3 - text
.arg(accentColor().name())       // %4 - accent/selection
.arg(primaryColor().name())      // %5 - hover border
.arg(backgroundColor().name())   // %6 - disabled bg
.arg(textSecondaryColor().name()) // %7 - disabled text
.arg(hoverColor().name())        // %8 - button hover
```

### 探查结果数据
```
[ToolBar] Total actions: 17
[ToolBar] Action: "📁" Widget: QToolButton(0x60000132eb50) Type: QToolButton
[ToolBar] Action: "" Widget: QWidget(0x60000132f150) Type: QWidget
[ToolBar] pageSpinBox: QSpinBox(0x60000132f1b0) parent: QWidget(0x60000132f150)
[ToolBar] viewModeCombo: QComboBox(0x600001326580) parent: QWidget(0x600001326280)
```

---

## 相关文件清单

### 已修改文件
- `app/managers/StyleManager.h` - 添加 getSpinBoxStyleSheet/getComboBoxStyleSheet 声明 + 工厂方法 + 注册表
- `app/managers/StyleManager.cpp` - 实现样式表方法 + 工厂方法 + registerWidget/reThemeAll
- `app/ui/core/ToolBar.h` - 添加 QWidgetAction 头文件
- `app/ui/core/ToolBar.cpp` - 重构控件创建，改用工厂，修改 applyToolBarStyle
- `app/ui/core/StatusBar.cpp` - 改用工厂创建控件，移除手动 connect

### 待研究文件（P3）
- `app/ui/thumbnail/` - 缩略图系统相关文件

---

## 下一步行动

1. **已完成**：
   - [x] ToolBar 监听主题变化
   - [x] StatusBar 应用样式表并监听主题变化
   - [x] 测试主题切换
   - [x] 设计统一的控件创建机制（StyleManager 工厂）

2. **后续优化**：
   - [ ] P3 - 缩略图性能优化
   - [ ] P4 - 右侧栏功能完善（如需要）

---

**最后更新**：2026-05-08
**状态**：P0-P2 已完成，当前进行 P3 缩略图性能优化
