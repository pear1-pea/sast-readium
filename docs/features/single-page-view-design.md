# 单页视图：行为设计与验证对照

当前单页视图存在缩放几何、页面所有权、帧与交互状态不同步的问题。现有测试主要验证接口状态和信号，不能证明用户看到、选中、复制的是同一页的同一区域。本文将预期行为、基线现状和测试证据分开。设计契约已在本轮落地，验证结果与仍未覆盖的部分见第 7 节。

- 日期：2026-11-24；代码基线：`224e1fadeaa1f8d906413853a13455350cb3e1da`。
- 调查方式：TRACE + VERIFY，working 深度；范围为单页展示、导航、缩放、旋转、选择复制、搜索高亮、缓存和渲染生命周期。
- 正文的“应当”是目标契约；“现状”段落保留改动前基线。已落地项、差异和未接通部分见第 7 节。
- 证据标签：SOURCE＝源码直接支持；RUNTIME＝实际执行观察；INFERRED＝据源码推导；UNKNOWN＝缺少验证。
- 连续视图的组件与测试只作为复用参考，其结果不能替代单页验收。

## 1. 验收边界

用户可见的完整结果是：输入操作进入实际单页界面，经文档、几何、渲染、显示和交互处理，最终显示正确页面，或向系统剪贴板写入准确文字。仅检查 `getCurrentZoom()`、信号次数或窗口可见，均不足以验收。

以下策略属于本草案的明确选择：

1. 非法页码、非有限缩放、非 90° 倍数旋转均拒绝，保持原状态。
2. 渲染等待时允许保留旧图；目标与可见帧不一致时暂停选择和复制，并显示加载状态。
3. 同页缩放保留选区所代表的页面内容；跨页、换文档和旋转清除选区。
4. 手动缩放以鼠标位置为锚点；工具栏缩放以视口中心为锚点。Fit 模式持续跟随有效视口和页面变化。
5. 复制按与选区相交的文本框取完整文字；字形级部分选择、多栏阅读顺序和 OCR 不在本次验收范围。
6. 当前页优先于预取页；异步优化采用每个 worker 独立的文档上下文，禁止把现有页面裸指针直接交给后台任务。

## 2. 关键场景与 corner case

“判定依据”是验收时应观测的结果；第 6 节另列当前实际测试。S 编号跨章节保持一致。

| ID | 输入与边界 | 预期结果及判定依据 |
| --- | --- | --- |
| S01 | 打开有效文档；单页或多页 | 提交第一页图像后进入可交互状态；页面身份、页数、实际画面一致。单页文档前后翻页均不越界。 |
| S02 | 空指针、零页、无效文件、加载失败 | 空指针关闭文档；无效输入进入明确错误状态；旧图、高亮和选区不可继续作为当前文档交互，后续打开有效文件能恢复。 |
| S03 | 首页上一页、末页下一页、页码 -1/N | 保持有效页、画面和滚动位置；非法请求不发出成功导航事件。合法首末页各能准确到达。 |
| S04 | A→B→C 快速切页，完成顺序 C→A→B | 最后仅 C 可提交；晚到结果不得替换 C 的图像、尺寸、高亮或交互身份。 |
| S05 | 10%/100%/200%/300%/500%，DPR=1/2 | 逻辑页面尺寸始终等于旋转后页面 pt 尺寸乘缩放；栅格分辨率上限不得限制逻辑尺寸。像素取整误差每轴不超过 1 个逻辑像素。 |
| S06 | 缩放超限、NaN、±Inf；连续小幅输入 | 有限超限值限制在 [0.1,5.0]；非有限值拒绝。预览可即时改变，最终渲染合并到最后一次输入；检查实际渲染次数和最终帧。 |
| S07 | 滚轮 ±1/±120/±240、仅像素增量、触控板 | 缩放随输入量连续变化；不能把所有非零增量都当一次 15% 跳变；无 Ctrl 的滚动继续交给滚动区域。 |
| S08 | 0/90/180/270°，负角度、450°、45° | 合法角度按模 360 归一化；45° 拒绝。90/270° 后宽高交换，文字、选区及高亮仍对齐同一页面内容。 |
| S09 | FitPage/FitWidth/FitHeight 后 resize、侧栏变化、换不同尺寸页、旋转 | 按有效视口和旋转后尺寸重算倍率；不裁掉承诺适配的方向；固定倍率模式保留倍率及阅读锚点。零尺寸视口暂缓计算。 |
| S10 | 在文字区域放大、缩小，包含页面边缘 | 鼠标下或视口中心的页面点维持原视口位置；受滚动边界限制时允许裁剪到最近有效位置。 |
| S11 | 框选后缩放，再复制；旋转后重新选择 | 缩放前后复制同一段文字；旋转后新选区对应旋转后的可见内容。断言剪贴板精确文本与选框几何。 |
| S12 | 页面外拖选、空选区、空白页、扫描页 | 选区裁剪到页面边界；无可提取文字时不覆盖剪贴板，也不报告复制成功；不调用外部 OCR。 |
| S13 | A 图仍显示、目标已为 B 时框选/复制 | 暂停选择与复制，不得看 A 而复制 B；只有 B 帧完整提交后才恢复交互。 |
| S14 | 搜索结果跨页跳转，缩放、旋转和清空 | 只显示对应页的结果；原始矩形经同一页面变换对齐文字；清空后无残留，旧文档结果不可污染新文档。 |
| S15 | 同页同键命中；接近倍率；跨 DPR/文档 | 同一规范键命中且图像/尺寸正确；不同文档或 DPR 不得误命中。测试相等关系与哈希一致性，不能只看缓存条目数。 |
| S16 | 缓存预算变小、超大帧、清空与关闭 | 按预算淘汰；超预算帧可以显示但不强制缓存；空帧不缓存。清空后重新请求能恢复，文档隔离不依赖路径字符串相同与否。 |
| S17 | 当前页与相邻页竞争、重复请求、取消 | 当前页优先、同代同键去重、最多 2 个后台渲染；取消旧代的排队请求，运行中旧结果完成后丢弃。 |
| S18 | 渲染返回空图/失败；失败后重试 | 结束加载状态并显示错误，不能显示成成功的新页；禁止缓存失败帧；重试可成功，成功后交互恢复。 |
| S19 | 渲染途中关闭/换文档；反复翻页与旋转 | 关闭后任务不能访问已销毁页面或提交旧图；页对象分配/释放最终平衡，后台任务归零；不能仅以短时 RSS 判断无泄漏。 |
| S20 | 单页↔连续模式，返回原阅读位置 | 保留有效页、倍率、旋转；单页应能恢复页面内锚点，不能仅验证页码相同。 |
| S21 | 复制/导航快捷键，焦点在阅读区与输入框 | 阅读区操作生效；输入框保留自己的复制、方向键及空格编辑行为；核查实际事件路由和目标控件内容。 |

## 3. 核心链路：现状与目标

以下是改动前基线的 SOURCE 静态调用链，不能当作运行时已验证轨迹；实现后的行为与验证见第 7 节。

### F1 文档、导航与首帧（S01–S04、S18–S20）

现状：`PDFViewer::setDocument` → 清缓存并替换 document → 页码/旋转归零 → `updatePageDisplay` → `document->page()` → `release()` → `PDFPageWidget::setPage` → 更新 currentPage/scale/rotation 并清选区 → `renderPage`。

导航入口：`nextPage/previousPage/firstPage/lastPage` → `goToPageWithValidation` → 检查文档及页码 → 更新目标页 → `updatePageDisplay` → 更新搜索结果 → `pageChanged`。当前信号发生在新图真正提交之前，不能用它代表渲染完成。

问题边界：控件直接覆盖页面裸指针；旧图与新 currentPage 可同时存在；关闭分支不具有异步任务的统一失效协议。

目标：先验证输入，再建立目标状态和文档 token；旧页面按明确所有权释放。新帧成功后一次提交图像、几何、覆盖层与可交互身份；失败进入 Error；关闭递增有效性标识并禁止后续提交。导航目标改变与帧提交必须是两种可区分的事件。

### F2 缓存、等待与渲染（S04–S06、S13、S15–S19）

现状：`renderPage` → 查 `{pageIndex, scale, rotation}` → 命中则设置 pixmap/固定尺寸 → 未命中启动 100ms single-shot → GUI 线程 `onRenderTimeout` → `min(72×zoom×DPR,300)` → 同步 `renderToImage` → 设置 QPixmap 的 DPR → 根据位图反算控件尺寸 → 入缓存。

目标：规范化 FrameKey → 查缓存 → 命中仍经相同提交校验；未命中可用旧图预览，但保持加载标识并禁用不匹配交互 → 当前页高优先级排队 → worker 独立文档生成 QImage → 校验 token/generation/key/有效图像 → GUI 原子提交。只有成功帧可缓存；错误结果结束 loading，重试创建新请求。

状态约束：`Empty → Loading → Ready | Error`；`Ready → Loading` 可保留旧图；任意状态关闭均进入 Empty。`targetKey != committedKey` 时 `canSelect/canCopy=false`。

现有 `PDFPrerenderer` 管理队列未分发给 worker 本地队列，viewer 也未调用启动/预取。连续调度器具备 token、generation、去重和并发限制，可评估复用；它尚未服务单页。

### F3 缩放、旋转与适配（S05–S10）

工具栏：`PDFViewer::setZoom` → `ZoomController::setZoom` → `zoomChanged` → viewer 暂时阻断控件信号 → `PDFPageWidget::setScaleFactor` → `renderPage`。

Ctrl+滚轮：控件按符号计算倍率 → `setScaleFactor` → `scaleChanged` → viewer `onScaleChanged` → ZoomController。回传同倍率会被去重；没有证据支持“无限递归渲染”。

目标：验证/规范化输入 → 保存页面锚点 → 更新逻辑几何与低成本预览 → 合并最终渲染请求 → 新帧提交 → 恢复锚点。DPR 变化同样重新请求图像。Fit 模式的有效视口变化、换页和旋转都应触发重算。

### F4 选择与复制（S11–S13、S21）

现状：鼠标 press/move/release 保存控件坐标 → `PDFViewer::copySelectedText` 按模式路由 → 控件坐标除 currentScaleFactor → `currentPage->textList(rotation)` → 取相交文本框并拼接 → `QClipboard::setText`。这条链依赖请求倍率、可见图像和 currentPage 一致，目前没有完整状态约束。

目标：先检查 committed frame 可交互 → 鼠标经逆变换得到原始页面坐标并裁剪 → 持久化选区 → 绘制时正变换 → 按同一页面身份提取文字 → 有非空结果才写剪贴板。等待新帧时不允许绕过检查。

### F5 高亮与模式/位置恢复（S14、S20）

现状：搜索结果按页筛选 → `setSearchResults` → `transformToWidgetCoordinates` → 绘制；搜索原始矩形为左下原点，证据矩形使用左上原点，二者不能直接互换。`onNavigateToSearchResult(page, rect)` 当前只按页导航，未使用 rect 定位页内位置。

`captureViewport/restoreViewport` 当前仅连续模式处理 offsetY，单页没有保存横纵滚动锚点；结构中也没有 rotation、文档身份或页内坐标。

目标：所有覆盖层先转换到约定的原始页面坐标，再通过 committed frame 的同一变换绘制；高亮与帧一起提交。模式切换保存页面内点和视口相对位置，目标模式几何就绪后恢复，不能只比较页码。

## 4. 数据结构与不可破坏的约束

本节的 FrameKey、FrameSnapshot、Selection、ViewportAnchor 是建议的概念模型，尚未新增 C++ 类型或改变公共接口。现有类型名称在“现状”列明确注明。

### D1 文档与页面所有权（S01、S02、S19）

| 项目 | 现状 | 目标约束 |
| --- | --- | --- |
| 文档 | viewer 持有 `shared_ptr<Poppler::Document>` | 每次打开有唯一 token；相同路径重新打开也换 token；依赖页面销毁完毕前文档保持存活。 |
| 页面 | `PDFPageWidget::currentPage` 为裸指针；上层 release 后交付 | GUI 页面由唯一 owner 管理；替换、失败、关闭和析构都可回收。worker 使用自己的文档与页面，不借用 GUI 页对象。 |
| 销毁顺序 | 无单页后台任务协议 | 先失效结果并停止接收，再释放 GUI 页面及文档；运行中 worker 的独立上下文维持到任务结束。 |

### D2 FrameKey 与缓存（S05、S15–S17）

建议字段：`documentToken, pageIndex, zoomKey, rotation, dprKey, renderProfile`。

- `pageIndex` 是零基页码，必须满足 `0 <= pageIndex < pageCount`。
- zoom 和 DPR 必须有限且为正；zoom 限制在 [0.1,5.0]。统一量化成整数，例如 `round(value×10000)`，渲染使用量化后的规范值；同一规则同时用于比较、哈希和请求去重。
- `rotation ∈ {0,90,180,270}`；拒绝非 90° 倍数后再模 360。
- `renderProfile` 标识会改变图像结果的质量策略；质量上限变化不可复用旧质量的同名键。
- 相等关系必须自反、对称、传递；`a == b ⇒ qHash(a,seed) == qHash(b,seed)`。禁止用浮点差阈值定义相等。
- 缓存存成功图像；像素/字节成本使用可容纳乘积的整数类型，检查尺寸合法性和溢出；预算不能为负。缓存预算约束仅涵盖缓存持有成本，不等于进程总内存。
- 单页现有 CacheKey 仅含页码、浮点倍率、旋转；DPR/token/profile 缺失。viewer 独立缓存和换文档清空是已有保护，但不能替代异步结果身份校验。
- 连续缓存键虽已有 token/DPR，其浮点相等与整数哈希也不能不经审查直接当作最终模型。

### D3 几何与坐标（S05、S08–S14）

统一原始页面空间：左上原点、未旋转、单位 pt。显示空间为逻辑像素；栅格空间为物理像素。页面宽高、DPR、变换元素必须有限且有效；零尺寸视口暂缓适配与求逆。

```text
rotatedSize = rotation in {90,270} ? (heightPt,widthPt) : (widthPt,heightPt)
logicalSize = rotatedSize × canonicalZoom
renderDpi = min(72 × canonicalZoom × canonicalDpr, 300)
pixelSize ≈ rotatedSize × renderDpi / 72
widgetPoint = pageToWidget(pagePoint)
pagePoint = widgetToPage(widgetPoint)
```

- `logicalSize` 独立于 pixelSize；DPI 限制只能降低清晰度，不能改变布局倍率。
- pageToWidget 包含旋转、缩放和实际内容偏移；选择、搜索和证据高亮共用它，不能分别除倍率估算。
- 可逆变换往返误差每轴不超过 0.0001pt；最终布局每轴允许 1 个逻辑像素的取整误差。锚点恢复每轴允许 1 个逻辑像素误差，滚动范围裁剪除外。
- Fit 倍率若受 [0.1,5.0] 限制而无法完整适配，应使用受限倍率并显示必要滚动条，不承诺该尺寸视口内完整容纳。
- 量化前检查整数范围及溢出，量化后的 DPR 必须大于 0；不能因极小正值或超大有限值产生无效键。
- 搜索 `SearchResult::boundingRect` 当前为左下原点，证据框为左上原点；进入统一空间时各转换一次，禁止重复翻转 Y。

### D4 已提交帧、请求与选区（S04、S11–S19）

| 概念 | 约束 |
| --- | --- |
| FrameSnapshot | 将 key、图像、逻辑页面矩形、正逆变换和页面交互身份绑定；不可出现新页身份配旧页图片。GUI 线程一次提交。 |
| RenderRequest | 带 FrameKey、generation、priority；当前页优先于预取；同代同键去重。generation 是单调更新的有效性版本，不能只比较页码。 |
| RenderResult | 原样带回请求身份；成功要求图像有效；失败带错误并清理 loading/pending。提交时同时核对文档 token、generation 和目标键。 |
| Selection | 绑定文档 token/页码，在原始页面坐标保存；绘制与复制使用同一 committed frame。跨页、旋转、换文档时清除；空选区不写剪贴板。 |
| ViewportAnchor | 绑定文档身份和页码，保存页面内点、视口相对点、zoom/type、rotation；几何就绪后恢复并裁剪滚动范围。 |

现有 `PDFViewerPosition{page,offsetY,zoom,mode}` 不足以满足上述恢复；现有 single-page timer 没有 RenderRequest/Result 版本字段；连续调度器中的同名能力只能作为迁移参考。

## 5. 怎样才算真实端到端验证

本节是验收设计；E01–E08 已部分落地为自动化用例，逐项覆盖层级见 7.4，未覆盖部分不得按“已通过”计入。

### 5.1 独立判定依据与夹具

- 固定两份真实 PDF，A/B 文档每页有不同的文字和定位标记；人工约定精确期望文本，如 `PAGE_A_ALPHA`、`PAGE_B_BETA`，禁止从被测提取函数计算期望值。
- 包含 600×800pt 非正方页面、另一种尺寸页面、空白页、仅图片页和损坏文件；损坏文件/零页的行为分别检验。
- 坐标期望由已知页面尺寸和固定矩形独立计算。例：600×800pt、DPR=2、300% 时应显示 1800×2400 逻辑像素，旋转 90° 后为 2400×1800；不能根据被测 pixmap 尺寸反算“正确答案”。
- 图像内容必须能识别具体文档/页码的定位标记；非空图、窗口可见或任意 render 信号都不足以证明内容正确。

### 5.2 最小验收集合

| 验收 ID | 输入、链路与约束 | 必须断言的终点 |
| --- | --- | --- |
| E01 | 从实际应用打开 A，UI 翻页并操作首末页；S01–S03/F1/D1 | 可见图像对应正确文档和页；边界不变；页对象回收；失败后打开有效文件恢复。 |
| E02 | 真实滚轮/工具栏缩放，矩阵含 DPR1/2、10/100/200/300/500%；S05–S10/F2–F3/D2–D3 | 最终逻辑尺寸、页面标记位置、锚点误差和最终倍率；连续输入后最终帧正确，昂贵渲染被合并。 |
| E03 | 实际鼠标框选→缩放→复制，再旋转后重选；S11–S13/F4/D3–D4 | 剪贴板精确等于预先给定文字；缩放不改变选区所代表内容；空选区/空白页保留原剪贴板。测试结束恢复剪贴板。 |
| E04 | 缓存命中/淘汰、A→B→重新加载同路径、DPR 改变；S15–S16/F2/D2 | 像素标记、帧身份和逻辑几何正确；缓存预算有界；不同来源无污染；清空后能重新渲染。 |
| E05 | 人为控制任务完成顺序，A→B→C 并关闭/换文档；S04、S13、S17、S19/F1–F2/D1、D4 | 只有最新合法帧提交；每次提交的图像/身份/交互一致；无关闭后的提交；去重和并发上限成立。 |
| E06 | 空图/异常/重试，GUI 心跳同时运行；S18/F2/D4 | 错误结束 loading 且不缓存；重试成功；慢任务期间事件循环仍能响应。不能用单个固定等待时间证明响应性。 |
| E07 | 搜索跨页、清空、切模式再返回；S14、S20/F5/D3–D4 | 高亮落在固定文本矩形，旧高亮消失；单页内部的页面锚点和旋转恢复。 |
| E08 | 焦点在阅读区、搜索输入框、其他编辑区时发送复制/方向键/空格；S07、S21/F3–F4 | 实际目标控件接到正确事件；复制文本、输入框内容和当前页分别符合预期。 |

E05/E06 的可控完成顺序、失败注入及生命周期计数适合组件/集成测试，不能单独标为完整应用 E2E。还必须有实际应用入口、真实 Poppler 渲染和真实交互结果的 E01–E04/E07–E08。

headless Qt 集成测试只能证明其平台插件下的结果；macOS 原生焦点、触控板、跨屏 DPR 变化需在对应环境补充运行。不能把设置一个 DPR 数值的组件测试等同于物理跨屏验证。

### 5.3 测试必须能失败

为关键断言分别设计负对照：错误页图、错误文字、DPI 限幅后错误逻辑尺寸、过期结果提交、空图被当成功、旧页面不释放。每个负对照应让对应验收失败。等待期复制被拒绝等少数负路径已有直接用例；“错误页图、过期提交、空图当成功”的 mutation 仍未运行，是否被拦截仍是 UNKNOWN。

## 6. 交叉检验：现有测试究竟断言了什么

本节是改动前基线的交叉检验记录。结论：基线没有证据支持“上面的一切已被真实端到端断言”。当前用例与结果见第 7 节。

测试简称与定位：

- V：[test_pdf_viewer.cpp](../../tests/integration/test_pdf_viewer.cpp)，直接创建 PDFViewer，以 API 驱动，未经过 MainWindow/应用文件打开入口。
- Z：[test_zoom_controller.cpp](../../tests/unit/test_zoom_controller.cpp)，控制器组件测试。
- S：[test_pdf_continuous_render_scheduler.cpp](../../tests/unit/test_pdf_continuous_render_scheduler.cpp)，连续调度器组件测试。
- C：[test_pdf_continuous_image_cache.cpp](../../tests/unit/test_pdf_continuous_image_cache.cpp)，连续图像缓存组件测试。
- B/M：[blueprint](../../tests/unit/test_pdf_continuous_blueprint.cpp) / [coordinate mapper](../../tests/unit/test_pdf_continuous_coordinate_mapper.cpp)，连续布局与映射组件测试。
- [测试注册](../../tests/CMakeLists.txt)给这些用例设置 `QT_QPA_PLATFORM=offscreen`。链接了 viewer 源码不等于通过了应用入口或验证了原生交互。

| 场景/数据约束 | 已有测试及实际断言 | 覆盖判断与缺口 |
| --- | --- | --- |
| S01–S02 / D1 | V `testDocumentLoad`：hasDocument、页数=3、页码=0；init 直接注入已由 Poppler 解析的文档 | 部分元数据；无应用打开、关闭、坏文件、零页、加载错误与恢复断言。 |
| S03–S04 / D4 | V `testNavigation`：常规 API 后页码；`testPageBounds`：100/-5 后页码不变 | 部分页码；无边界继续翻页、最终图像、快速切页或乱序提交断言。 |
| S05 / D3 | V `testSinglePagePaintDoesNotCrash` 等待后仅断言 isVisible；`testZoom` 只检查倍率值或 >0 | 无逻辑尺寸、DPR、像素内容与分辨率断言；窗口可见不证明 PDF 画面正确。 |
| S06 / D2–D3 | Z `testZoomClamping`：0.01→0.1、10→5；`testZoomSignal`：2.0 信号和 2.0005 去重；`testPersistence`：保存恢复倍率和类型 | 部分数值契约；无非有限值、防抖最终帧/渲染次数或实际显示断言。 |
| S07、S10 / D3 | V/Z 通过 setZoom/zoomIn/zoomOut 调用；无单页 QWheelEvent 输入用例 | 无触控板增量、普通滚动路由及鼠标/视口锚点断言。 |
| S08–S09 / D3 | V `testRotation` 检查信号次数，三个 Fit 调用仅检查 zoom>0；B 检查90°尺寸交换，M检查90°点/180°矩形往返 | 单页旋转画面、Fit 后 resize/换页/旋转不受保护；B/M 走另一套几何。 |
| S11–S13 / D3–D4 | V `testCopyShortcutScope` 只检查有 Copy 快捷键且 context=WidgetWithChildrenShortcut | 无 PDF 文字拖选、剪贴板、缩放后选区、等待期可见页/复制页一致性断言。 |
| S14 / D3–D4 | 已核查单页用例无实际文字区域的高亮断言 | 无跨页高亮、坐标、清空和页内定位的单页验收。 |
| S15–S16 / D2 | C `testCostBoundedEviction`：插图后淘汰及成本<=预算；`testHitMissStatistics`：计数；S `testDocumentTokenChangesAcrossDocuments`：token递增且键不等 | 连续组件部分覆盖；单页 PDFRenderCache 无直接用例；无不同DPR/文档图像内容、近倍率哈希、超预算项与关闭后隔离断言。 |
| S04、S17 / D4 | S `testStaleGenerationRequestIsDropped`：先增代，再提交旧代请求，结果数/pending=0；`testDocumentChangeAllowsSameRenderKeyAgain` 等A完成后再换B，检查B成功和generation | 仅验证入队时已经过期的请求被拒绝；没有在途任务失效、优先级竞争、去重或并发上限的完整断言。不能外推单页。 |
| S18–S19 / D1、D4 | S 成功路径检查 success、非空图；V cleanup 删除 viewer | 无失败注入/重试、页对象平衡、渲染中关闭、旧帧提交或GUI心跳断言。 |
| S20 / D4 | V `testViewModeSwitch`：模式/信号/页码；`testViewportCaptureRestore` 仅连续模式页码、zoom和offsetY误差<=5 | 部分模式状态；无单页内锚点、旋转和实际显示内容恢复断言。 |
| S21 / D4 | V Copy scope 检查；ShortcutManager 测试动作注册及快捷键元数据 | 只验证声明；无单页真实按键路由、输入框编辑、同窗口焦点竞争断言。 |

### 6.1 夹具为什么不足以发现错误

基线阶段 V 的 `buildPdf` 生成 3 页相同的 612×792pt 空白页，内容流只有 `q Q`。本轮已替换为 3 页 600×800pt、每页含 `PAGE_<i>_ALPHA` 文字标记的夹具。S 的测试文档同样为空白，跨文档用例主要区别是路径。它们可以触发真实 PDF 解析和渲染，但不能作为“哪一页/哪个文档的文字与画面”的判定依据。

仓库有其他子系统的真实文字提取及剪贴板测试；它们不经过 PDFPageWidget 的鼠标选择、坐标换算和复制链路，不能填补单页验证缺口。

V `testZoom` 中的 `getCurrentZoom() < getCurrentZoom() + 0.2` 对任意有限倍率均成立，不能证明 zoomOut 生效。V 的 shared viewer 在 initTestCase 创建，各 slot 之间也不是独立初始状态。

### 6.2 反例检查，而非以测试名称推断能力

以下是据断言源码推导的负对照（INFERRED），尚未实际修改代码运行，不能写为“mutation 已通过”：

| 假设注入的错误 | 用户结果 | 现有断言为什么可能仍通过 |
| --- | --- | --- |
| 单页始终获取 page(0)，页码状态照常更新 | 翻页数字改变，画面仍是第一页 | V只比较页码，且夹具各页同为空白。 |
| 单页 renderToImage 固定72 DPI | 缩放数字正确，页面尺寸/清晰度错误 | Z检查数值，V不检查单页图像和几何。 |
| 单页 copySelectedText 直接返回false | 选中文字无法复制 | V只检查快捷键context，不检查剪贴板。 |
| 删除连续调度器完成回调中的旧代丢弃，保留入队检查 | 旧在途任务可发出结果 | S过期测试在请求前已增代；跨文档测试等待A结束后才换B。 |

特别核对：换文档有 viewer 清缓存和 prerenderer::setDocument 清缓存两条路径；只删除前一条不能单独证明串图。负对照必须实际覆盖所有保护路径，不能依据单行修改猜测结论。

## 7. 实施结果与本轮验证

### 7.1 基线缺陷探针（改动前）

前一轮在临时目录编译了实际 `PDFRenderCache.cpp` 和 `ZoomController.cpp`，未把探针加入仓库测试。环境为 macOS、本机 Qt 6.11.1；项目声明版本为 6.8.2，不能把它当作目标版本回归。

缓存探针：插入 `{0,1.009,0}`，先按原键查询，再按 `{0,1.011,0}` 查询；二者被当时的 `operator==` 判等。实际输出：

```text
Qt=6.11.1
equal_keys=1
same_hash=0
lookup_original=1
lookup_equal_key=0
```

缩放探针：连续调用 `setZoom(1.0+i*0.01)`，i=1…10，连接 zoomChanged 计数，并检查子 QTimer。实际输出：

```text
Qt=6.11.1
rapid_zoom_requests=10
immediate_zoom_signals=10
debounce_timer_active=0
final_zoom=1.1
```

证据强度：这些探针直接复现组件缺陷（RUNTIME），不经过文件打开、页面渲染、输入、显示或剪贴板。

### 7.2 已落地改动

- D1：`PDFPageWidget` 以 `std::unique_ptr<Poppler::Page>` 独占页对象；`setPage` 接收所有权，关闭文档时页对象与帧同时清除。
- D2：`PDFRenderCache::CacheKey{documentToken, pageNumber, zoomKey, rotation, dprKey}` 全部整数量化，`qHashMulti` 覆盖全部字段；空图与超过预算的单帧不入缓存。
- D3：逻辑尺寸只由旋转后页面 pt × 规范倍率决定；300 DPI 上限只降低栅格分辨率；选区保存为未旋转页面坐标，绘制与复制经同一正反旋转映射。
- D4：`FrameKey` + 已提交帧标志；仅当生成序号与目标键一致时提交。等待期 `isFrameReady()` 为 false，复制被拒绝。
- 缩放：非有限值拒绝；每次输入立即生效；滚轮按 `1.15^(angleDelta/120)`；持久化改到 `zoomSettled`。
- 渲染：QtConcurrent 后台栅格化，每个工作线程持有 thread-local `Poppler::Document`；同一 `Poppler::Document` 不跨线程共享。
- 适配：FitPage/Width/Height 在视口 resize、旋转、换页后按旋转后页面尺寸和 0.98 边距重算。
- 视图恢复：`PDFViewerPosition` 增加页面坐标锚点和旋转；单页保存/恢复页内锚点，模式切换与换页后重新应用。

实施中与手工启动后发现并修复的五处问题：

1. FitWidth 震荡。0.98 边距小于滚动条宽度，竖条出现使视口变窄、重算后横条出现、再重算又消失，适配值在两个倍率间反复切换。现在预测需要另一轴滚动条时先扣除 `PM_ScrollBarExtent`；`testFitWidthFollowsViewportResize` 额外断言 300ms 内倍率不变。
2. 每次 `zoomChanged` 都调用 `saveSettings()`。在受限环境下每次都会同步到 cfprefsd，与上面的震荡叠加导致测试 300s 超时。持久化现只在 `zoomSettled` 触发。
3. 滚轮缩放把控件坐标当成了视口坐标。`QScrollArea::setWidget` 会把页面重新挂到 viewport 之下，`qobject_cast<QScrollArea*>(parentWidget())` 恒为空，滚动偏移被忽略。现在沿父级找到滚动区域再换算全局坐标；`testWheelZoomKeepsPointUnderCursor` 断言光标下的页面点在缩放前后不变。
4. 带样式的真实应用在构造期崩溃（SIGSEGV，`QAbstractScrollArea::viewport()` 空指针）。`setupViewModes` 先给单页区域安装事件过滤器、再应用样式表，repolish 事件进入 `eventFilter` 时连续区域仍为空；集成测试用 `PDFViewer(nullptr, false)` 构造，绕过了这条路径。现在事件过滤器先判空；新增 `testStylesheetPolishBeforeContinuousAreaDoesNotCrash` 构造带样式 viewer 复现该路径。
5. 真实应用打开文档后仍停在欢迎页：`MainWindow` 初始化时直接切换了 `QStackedWidget`，没有同步 `WelcomeScreenManager` 的可见标志，首次打开文档时因此不会隐藏欢迎页；状态栏已显示文件名，画面仍在选择页。现在初始显示欢迎页时调用 `showWelcomeScreen()` 同步状态。此路径没有现成测试夹具，由手工启动发现。

### 7.3 本轮命令与实际结果

| 命令/检查 | 实际结果 | 说明 |
| --- | --- | --- |
| `cmake --build build --config Release` | 退出 0，链接 `app/app` 与全部测试目标 | 编译器报 `xcrun_db` 缓存写失败，为 sandbox 限制，不影响产物 |
| `QT_QPA_PLATFORM=offscreen test_pdf_viewer` | 32 passed / 0 failed，约 5.7s | 含新增的选择、缓存、缩放、旋转、失败与锚点用例 |
| `test_pdf_render_cache` | 9 passed / 0 failed | 键相等与哈希一致、量化、token/DPR/旋转区分、空图与超预算、淘汰 |
| `test_zoom_controller` | 13 passed / 0 failed | 含非有限值拒绝、连续输入逐个生效、settled 只发一次 |
| `ctest --test-dir build --output-on-failure` | 26/29 通过 | 3 个失败均为 `server.listen(QHostAddress::LocalHost)` 返回 false；sandbox 禁止本地端口绑定，非本轮代码回归 |
| `pre-commit run --files <改动文件>` | 首轮 clang-format 改写后复检全部 Passed | 未运行全量：`.gitmodules` 写保护会使 end-of-file-fixer 报 PermissionError |

### 7.4 E01–E08 对照

| 验收 ID | 实际用例 | 覆盖层级与缺口 |
| --- | --- | --- |
| E01 | `testDocumentLoad`、`testDocumentSwitchDoesNotReusePreviousFrame`、`testClearDocumentResetsSinglePageFrame`、`testRenderFailureEmitsError` | viewer API 直连；坏文件后重新打开有效文件恢复未断言；未经过 MainWindow 打开入口 |
| E02 | `testSinglePageLogicalSizeAtHighZoom`、`testZoomKeepsPagePointUnderViewportCenter`、`testWheelZoomKeepsPointUnderCursor`、`testWheelZoomUsesInputMagnitude`、`testFitWidthFollowsViewportResize` | 300%/500% 逻辑尺寸与锚点、滚轮倍率、适配随视口重算；DPR 固定为 1，物理 DPR2/跨屏未验证；未统计渲染次数 |
| E03 | `testSelectionCopySurvivesZoom`、`testRotationUpdatesLogicalSizeAndSelection`、`testEmptySelectionKeepsClipboard`、`testCopyIsBlockedWhileFrameIsPending` | 剪贴板精确文本；缩放后选区内容不变；旋转后重新选择；空选区保留剪贴板；触控板手势未验证 |
| E04 | `test_pdf_render_cache` 全部用例、`testDocumentSwitchDoesNotReusePreviousFrame` | 键身份与预算有界；未逐像素比对缓存图像内容 |
| E05 | `testRapidPageSwitchCommitsCurrentPage`、`testRepeatedPageReplacementRemainsReady` | 只证明最终页帧可交互、重复替换不卡死；完成顺序不可控、无失败注入、无并发计数 |
| E06 | `testRenderFailureEmitsError` | 失败发出信号且不进入 ready；无重试成功用例，无 GUI 心跳响应性测量 |
| E07 | `testSearchResultCoordinateTransform`、`testSinglePageAnchorSurvivesModeRoundTrip` | 0°/90° 坐标变换与模式往返锚点；真实高亮绘制、跨页搜索未验证 |
| E08 | `testCopyShortcutScope` | 仅验证快捷键声明与 context；未验证真实按键在阅读区与输入框之间的路由 |

### 7.5 未接通与未验证

- `PDFPrerenderer` 的请求结构已修正（token、DPR、优先级、worker 本地队列），但未接入 viewer。单页渲染由 `PDFPageWidget` 的每线程 thread-local 文档完成。因此 F2 中“当前页优先预取、worker 队列、最多 2 个后台渲染”未在应用链路验证。
- 2026-11-24 按 12 项清单在真实应用完成手工验证，用户确认结果正常，覆盖：应用入口打开文档、翻页、缩放与光标锚点、Fit 跟随 resize、选择复制、旋转、模式往返、搜索高亮、跨文档切换、物理 DPR 切换、焦点与快捷键。手工验证未逐项保留截图或日志证据，不能替代自动化断言。
- 手工启动发现并修复了欢迎页不隐藏、带样式构造崩溃两个真实应用缺陷，见 7.2；两者原先都没有自动化覆盖，其中构造崩溃已补回归测试。
- 项目目标 Qt 为 6.8.2；本轮运行环境为本机 Qt 6.11.1。
- 应用入口（MainWindow 打开文件）未纳入自动化用例。
- 6.2 的 mutation 负对照未执行。

### 7.6 完成门槛

- 每个 S 场景必须关联实际用例、运行环境、精确断言和结果；覆盖状态按子断言记录，不能因为一个场景有相关测试名称就整行标“已覆盖”。
- D1–D4 的约束既要有组件性质测试，也要在实际单页链路检查最终结果；连续模式用例不能替代。
- 至少有带独立文本和标记的真实夹具；空白页只用于空内容和错误边界。
- 在途失效、失败恢复、页面资源平衡使用可确定重现的方法；负对照必须证明关键断言能发现错误。
- 核对实现全部差异后执行项目构建、ctest、pre-commit，并另列原生 macOS/跨屏/触控板未验证项。未完成前不得宣称“全场景 E2E 通过”。

## 8. 实现证据阅读入口

以下定位对应基线版本；函数名已随本轮实现变化，主要锚点仍可用于追踪。

| 文件/符号 | 对应问题 |
| --- | --- |
| [PDFViewer::setDocument / updatePageDisplay](../../app/ui/viewer/PDFViewer.cpp) | F1、页面交付、关闭、模式切换；导航目标与实际帧时序。 |
| [PDFPageWidget::setPage / renderPage / onRenderTimeout](../../app/ui/page/PDFPageWidget.cpp) | F2、GUI同步栅格化、DPI上限、缓存和控件尺寸。 |
| [PDFPageWidget::selectionRectInPageCoordinates / selectedText](../../app/ui/page/PDFPageWidget.cpp) | F4、控件坐标存储、文本框选择和剪贴板。 |
| [ZoomController::setZoom](../../app/controller/ZoomController.cpp) | F3、防抖条件与倍率状态。 |
| [PDFRenderCache::CacheKey / qHash](../../app/ui/viewer/PDFRenderCache.cpp) | D2、模糊相等与哈希不一致。 |
| [PDFPrerenderer::requestPrerender / PDFRenderWorker::processRenderQueue](../../app/ui/viewer/PDFPrerenderer.cpp) | 管理队列与worker本地队列、未接通的预取。 |
| [SearchResult::transformToWidgetCoordinates](../../app/model/SearchModel.cpp) | F5、左下原点转左上原点、旋转与显示矩形。 |
| [PDFContinuousRenderScheduler](../../app/ui/continuous/PDFContinuousRenderScheduler.cpp) | 可复用的token/generation/并发限制；尚非单页链路。 |
| [PDFViewerPosition](../../app/ui/viewer/PDFViewer.h) | 当前恢复结构缺少单页锚点与旋转。 |
