#include "AnnotationModel.h"
#include <QColor>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFont>
#include <QHash>
#include <QJsonDocument>
#include <QPointF>
#include <QRandomGenerator>
#include <QtCore>
#include <algorithm>

// PDFAnnotation serialization implementation
QJsonObject PDFAnnotation::toJson() const {
    QJsonObject obj;
    obj["id"] = id;
    obj["type"] = static_cast<int>(type);
    obj["pageNumber"] = pageNumber;
    obj["content"] = content;
    obj["author"] = author;
    obj["createdTime"] = createdTime.toString(Qt::ISODate);
    obj["modifiedTime"] = modifiedTime.toString(Qt::ISODate);
    obj["color"] = color.name();
    obj["opacity"] = opacity;
    obj["isVisible"] = isVisible;
    obj["lineWidth"] = lineWidth;
    obj["fontFamily"] = fontFamily;
    obj["fontSize"] = fontSize;

    // Bounding rect
    QJsonObject rectObj;
    rectObj["x"] = boundingRect.x();
    rectObj["y"] = boundingRect.y();
    rectObj["width"] = boundingRect.width();
    rectObj["height"] = boundingRect.height();
    obj["boundingRect"] = rectObj;

    // Points for line/arrow annotations
    if (type == AnnotationType::Line || type == AnnotationType::Arrow) {
        QJsonObject startObj;
        startObj["x"] = startPoint.x();
        startObj["y"] = startPoint.y();
        obj["startPoint"] = startObj;

        QJsonObject endObj;
        endObj["x"] = endPoint.x();
        endObj["y"] = endPoint.y();
        obj["endPoint"] = endObj;
    }

    // Ink path for freehand drawing
    if (type == AnnotationType::Ink && !inkPath.isEmpty()) {
        QJsonArray pathArray;
        for (const QPointF& point : inkPath) {
            QJsonObject pointObj;
            pointObj["x"] = point.x();
            pointObj["y"] = point.y();
            pathArray.append(pointObj);
        }
        obj["inkPath"] = pathArray;
    }

    return obj;
}

PDFAnnotation PDFAnnotation::fromJson(const QJsonObject& json) {
    PDFAnnotation annotation;
    annotation.id = json["id"].toString();
    annotation.type = static_cast<AnnotationType>(json["type"].toInt());
    annotation.pageNumber = json["pageNumber"].toInt();
    annotation.content = json["content"].toString();
    annotation.author = json["author"].toString();
    annotation.createdTime =
        QDateTime::fromString(json["createdTime"].toString(), Qt::ISODate);
    annotation.modifiedTime =
        QDateTime::fromString(json["modifiedTime"].toString(), Qt::ISODate);
    annotation.color = QColor(json["color"].toString());
    annotation.opacity = json["opacity"].toDouble();
    annotation.isVisible = json["isVisible"].toBool();
    annotation.lineWidth = json["lineWidth"].toDouble();
    annotation.fontFamily = json["fontFamily"].toString();
    annotation.fontSize = json["fontSize"].toInt();

    // Bounding rect
    if (json.contains("boundingRect")) {
        QJsonObject rectObj = json["boundingRect"].toObject();
        annotation.boundingRect =
            QRectF(rectObj["x"].toDouble(), rectObj["y"].toDouble(),
                   rectObj["width"].toDouble(), rectObj["height"].toDouble());
    }

    // Points for line/arrow annotations
    if (json.contains("startPoint")) {
        QJsonObject startObj = json["startPoint"].toObject();
        annotation.startPoint =
            QPointF(startObj["x"].toDouble(), startObj["y"].toDouble());
    }
    if (json.contains("endPoint")) {
        QJsonObject endObj = json["endPoint"].toObject();
        annotation.endPoint =
            QPointF(endObj["x"].toDouble(), endObj["y"].toDouble());
    }

    // Ink path
    if (json.contains("inkPath")) {
        QJsonArray pathArray = json["inkPath"].toArray();
        for (const QJsonValue& value : pathArray) {
            QJsonObject pointObj = value.toObject();
            annotation.inkPath.append(
                QPointF(pointObj["x"].toDouble(), pointObj["y"].toDouble()));
        }
    }

    return annotation;
}

bool PDFAnnotation::containsPoint(const QPointF& point) const {
    return boundingRect.contains(point);
}

QString PDFAnnotation::getTypeString() const {
    switch (type) {
        case AnnotationType::Highlight:
            return "Highlight";
        case AnnotationType::Note:
            return "Note";
        case AnnotationType::FreeText:
            return "FreeText";
        case AnnotationType::Underline:
            return "Underline";
        case AnnotationType::StrikeOut:
            return "StrikeOut";
        case AnnotationType::Squiggly:
            return "Squiggly";
        case AnnotationType::Rectangle:
            return "Rectangle";
        case AnnotationType::Circle:
            return "Circle";
        case AnnotationType::Line:
            return "Line";
        case AnnotationType::Arrow:
            return "Arrow";
        case AnnotationType::Ink:
            return "Ink";
        default:
            return "Unknown";
    }
}

AnnotationType PDFAnnotation::typeFromString(const QString& typeStr) {
    if (typeStr == "Highlight")
        return AnnotationType::Highlight;
    if (typeStr == "Note")
        return AnnotationType::Note;
    if (typeStr == "FreeText")
        return AnnotationType::FreeText;
    if (typeStr == "Underline")
        return AnnotationType::Underline;
    if (typeStr == "StrikeOut")
        return AnnotationType::StrikeOut;
    if (typeStr == "Squiggly")
        return AnnotationType::Squiggly;
    if (typeStr == "Rectangle")
        return AnnotationType::Rectangle;
    if (typeStr == "Circle")
        return AnnotationType::Circle;
    if (typeStr == "Line")
        return AnnotationType::Line;
    if (typeStr == "Arrow")
        return AnnotationType::Arrow;
    if (typeStr == "Ink")
        return AnnotationType::Ink;
    return AnnotationType::Highlight;  // Default
}

AnnotationModel::AnnotationModel(QObject* parent)
    : QAbstractListModel(parent), m_document(nullptr) {}

int AnnotationModel::rowCount(const QModelIndex& parent) const {
    Q_UNUSED(parent)
    return m_annotations.size();
}

QVariant AnnotationModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_annotations.size()) {
        return QVariant();
    }

    const PDFAnnotation& annotation = m_annotations.at(index.row());

    switch (role) {
        case Qt::DisplayRole:
            return QString("%1 - Page %2")
                .arg(annotation.getTypeString())
                .arg(annotation.pageNumber + 1);
        case Qt::ToolTipRole:
            return QString(
                       "Type: %1\nPage: %2\nAuthor: %3\nCreated: %4\nContent: "
                       "%5")
                .arg(annotation.getTypeString())
                .arg(annotation.pageNumber + 1)
                .arg(annotation.author)
                .arg(annotation.createdTime.toString())
                .arg(annotation.content);
        case IdRole:
            return annotation.id;
        case TypeRole:
            return static_cast<int>(annotation.type);
        case PageNumberRole:
            return annotation.pageNumber;
        case BoundingRectRole:
            return annotation.boundingRect;
        case ContentRole:
            return annotation.content;
        case AuthorRole:
            return annotation.author;
        case CreatedTimeRole:
            return annotation.createdTime;
        case ModifiedTimeRole:
            return annotation.modifiedTime;
        case ColorRole:
            return annotation.color;
        case OpacityRole:
            return annotation.opacity;
        case VisibilityRole:
            return annotation.isVisible;
        default:
            return QVariant();
    }
}

bool AnnotationModel::setData(const QModelIndex& index, const QVariant& value,
                              int role) {
    if (!index.isValid() || index.row() >= m_annotations.size()) {
        return false;
    }

    PDFAnnotation& annotation = m_annotations[index.row()];
    bool changed = false;

    switch (role) {
        case ContentRole:
            if (annotation.content != value.toString()) {
                annotation.content = value.toString();
                annotation.modifiedTime = QDateTime::currentDateTime();
                changed = true;
            }
            break;
        case ColorRole:
            if (annotation.color != value.value<QColor>()) {
                annotation.color = value.value<QColor>();
                annotation.modifiedTime = QDateTime::currentDateTime();
                changed = true;
            }
            break;
        case OpacityRole:
            if (annotation.opacity != value.toDouble()) {
                annotation.opacity = value.toDouble();
                annotation.modifiedTime = QDateTime::currentDateTime();
                changed = true;
            }
            break;
        case VisibilityRole:
            if (annotation.isVisible != value.toBool()) {
                annotation.isVisible = value.toBool();
                annotation.modifiedTime = QDateTime::currentDateTime();
                changed = true;
            }
            break;
        default:
            return false;
    }

    if (changed) {
        emit dataChanged(index, index, {role});
        emit annotationUpdated(annotation);
        return true;
    }

    return false;
}

Qt::ItemFlags AnnotationModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }

    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

QHash<int, QByteArray> AnnotationModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[Qt::DisplayRole] = "display";
    roles[IdRole] = "id";
    roles[TypeRole] = "type";
    roles[PageNumberRole] = "pageNumber";
    roles[BoundingRectRole] = "boundingRect";
    roles[ContentRole] = "content";
    roles[AuthorRole] = "author";
    roles[CreatedTimeRole] = "createdTime";
    roles[ModifiedTimeRole] = "modifiedTime";
    roles[ColorRole] = "color";
    roles[OpacityRole] = "opacity";
    roles[VisibilityRole] = "isVisible";
    return roles;
}

bool AnnotationModel::addAnnotation(const PDFAnnotation& annotation) {
    beginInsertRows(QModelIndex(), m_annotations.size(), m_annotations.size());
    m_annotations.append(annotation);
    endInsertRows();

    beginResetModel();
    sortAnnotations();
    endResetModel();
    emit annotationAdded(annotation);

    return true;
}

bool AnnotationModel::removeAnnotation(const QString& annotationId) {
    int index = findAnnotationIndex(annotationId);
    if (index < 0) {
        return false;
    }

    beginRemoveRows(QModelIndex(), index, index);
    m_annotations.removeAt(index);
    endRemoveRows();

    emit annotationRemoved(annotationId);
    return true;
}

bool AnnotationModel::updateAnnotation(const QString& annotationId,
                                       const PDFAnnotation& updatedAnnotation) {
    int index = findAnnotationIndex(annotationId);
    if (index < 0) {
        return false;
    }

    m_annotations[index] = updatedAnnotation;
    m_annotations[index].modifiedTime = QDateTime::currentDateTime();

    QModelIndex modelIndex = this->index(index, 0);
    emit dataChanged(modelIndex, modelIndex);
    emit annotationUpdated(updatedAnnotation);

    return true;
}

PDFAnnotation AnnotationModel::getAnnotation(
    const QString& annotationId) const {
    int index = findAnnotationIndex(annotationId);
    if (index >= 0) {
        return m_annotations.at(index);
    }
    return PDFAnnotation();
}

QList<PDFAnnotation> AnnotationModel::getAllAnnotations() const {
    return m_annotations;
}

QList<PDFAnnotation> AnnotationModel::getAnnotationsForPage(
    int pageNumber) const {
    QList<PDFAnnotation> result;
    for (const PDFAnnotation& annotation : m_annotations) {
        if (annotation.pageNumber == pageNumber) {
            result.append(annotation);
        }
    }
    return result;
}

bool AnnotationModel::removeAnnotationsForPage(int pageNumber) {
    bool removed = false;
    for (int i = m_annotations.size() - 1; i >= 0; --i) {
        if (m_annotations.at(i).pageNumber == pageNumber) {
            beginRemoveRows(QModelIndex(), i, i);
            QString removedId = m_annotations.at(i).id;
            m_annotations.removeAt(i);
            endRemoveRows();
            emit annotationRemoved(removedId);
            removed = true;
        }
    }
    return removed;
}

int AnnotationModel::getAnnotationCountForPage(int pageNumber) const {
    int count = 0;
    for (const PDFAnnotation& annotation : m_annotations) {
        if (annotation.pageNumber == pageNumber) {
            count++;
        }
    }
    return count;
}

void AnnotationModel::setDocument(std::shared_ptr<Poppler::Document> document) {
    m_document = std::move(document);
    clearAnnotations();
    if (m_document) {
        loadAnnotationsFromDocument();
    }
    if (!m_storagePath.isEmpty()) {
        loadAnnotationsFromStorage();
    }
}

void AnnotationModel::setStoragePath(const QString& storagePath) {
    m_storagePath = storagePath;
    if (!m_storagePath.isEmpty()) {
        loadAnnotationsFromStorage();
    }
}

void AnnotationModel::clearAnnotations() {
    beginResetModel();
    m_annotations.clear();
    endResetModel();
    emit annotationsCleared();
}

int AnnotationModel::findAnnotationIndex(const QString& annotationId) const {
    for (int i = 0; i < m_annotations.size(); ++i) {
        if (m_annotations.at(i).id == annotationId) {
            return i;
        }
    }
    return -1;
}

void AnnotationModel::sortAnnotations() {
    std::sort(m_annotations.begin(), m_annotations.end(),
              [](const PDFAnnotation& a, const PDFAnnotation& b) {
                  if (a.pageNumber != b.pageNumber) {
                      return a.pageNumber < b.pageNumber;
                  }
                  return a.createdTime > b.createdTime;
              });
}

QString AnnotationModel::generateUniqueId() const {
    return QString("ann_%1_%2")
        .arg(QDateTime::currentMSecsSinceEpoch())
        .arg(QRandomGenerator::global()->bounded(10000));
}

bool AnnotationModel::loadAnnotationsFromDocument() {
    if (!m_document) {
        return false;
    }

    beginResetModel();
    m_annotations.clear();

    int loadedCount = 0;
    for (int pageNum = 0; pageNum < m_document->numPages(); ++pageNum) {
        std::unique_ptr<Poppler::Page> page(m_document->page(pageNum));
        if (!page) {
            continue;
        }

        std::vector<std::unique_ptr<Poppler::Annotation>> popplerAnnotations =
            page->annotations();
        for (auto& popplerAnnot : popplerAnnotations) {
            try {
                PDFAnnotation annotation = PDFAnnotation::fromPopplerAnnotation(
                    popplerAnnot.get(), pageNum);
                if (!annotation.id.isEmpty()) {
                    m_annotations.append(annotation);
                    loadedCount++;
                }
            } catch (const std::exception& e) {
                qWarning() << "Failed to load annotation from page" << pageNum
                           << ":" << e.what();
            }
        }
    }

    sortAnnotations();
    endResetModel();

    emit annotationsLoaded(loadedCount);
    qDebug() << "Loaded" << loadedCount << "annotations from document";

    return true;
}

bool AnnotationModel::saveAnnotationsToDocument() {
    if (!m_document) {
        return false;
    }

    int savedCount = 0;

    // Group annotations by page for efficient processing
    QMap<int, QList<PDFAnnotation>> annotationsByPage;
    for (const PDFAnnotation& annotation : m_annotations) {
        annotationsByPage[annotation.pageNumber].append(annotation);
    }

    // Process each page
    for (auto it = annotationsByPage.begin(); it != annotationsByPage.end();
         ++it) {
        int pageNum = it.key();
        const QList<PDFAnnotation>& pageAnnotations = it.value();

        std::unique_ptr<Poppler::Page> page(m_document->page(pageNum));
        if (!page) {
            continue;
        }

        // Add new annotations to the page
        for (const PDFAnnotation& annotation : pageAnnotations) {
            try {
                Poppler::Annotation* popplerAnnot =
                    annotation.toPopplerAnnotation();
                if (popplerAnnot) {
                    page->addAnnotation(popplerAnnot);
                    delete popplerAnnot;
                    savedCount++;
                }
            } catch (const std::exception& e) {
                qWarning() << "Failed to save annotation to page" << pageNum
                           << ":" << e.what();
            }
        }
    }

    emit annotationsSaved(savedCount);
    qDebug() << "Saved" << savedCount << "annotations to document";

    return savedCount > 0;
}

bool AnnotationModel::loadAnnotationsFromStorage() {
    if (m_storagePath.isEmpty()) {
        return false;
    }

    QFile file(m_storagePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return false;
    }

    const QJsonArray annotations =
        document.object().value(QStringLiteral("annotations")).toArray();
    int loadedCount = 0;
    beginResetModel();
    for (const QJsonValue& value : annotations) {
        if (!value.isObject()) {
            continue;
        }
        const PDFAnnotation annotation =
            PDFAnnotation::fromJson(value.toObject());
        if (annotation.id.isEmpty() ||
            findAnnotationIndex(annotation.id) >= 0) {
            continue;
        }
        m_annotations.append(annotation);
        ++loadedCount;
    }
    sortAnnotations();
    endResetModel();

    if (loadedCount > 0) {
        emit annotationsLoaded(loadedCount);
    }
    qDebug() << "Loaded" << loadedCount << "annotations from storage"
             << m_storagePath;
    return true;
}

bool AnnotationModel::saveAnnotationsToStorage() const {
    if (m_storagePath.isEmpty()) {
        return false;
    }

    QJsonArray annotations;
    for (const PDFAnnotation& annotation : m_annotations) {
        if (annotation.id.startsWith(QStringLiteral("ann_"))) {
            annotations.append(annotation.toJson());
        }
    }

    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("annotations")] = annotations;

    QFile file(m_storagePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Failed to open annotation storage for writing:"
                   << m_storagePath;
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    qDebug() << "Saved" << annotations.size() << "annotations to storage"
             << m_storagePath;
    return true;
}

QList<PDFAnnotation> AnnotationModel::searchAnnotations(
    const QString& query) const {
    QList<PDFAnnotation> result;
    QString lowerQuery = query.toLower();

    for (const PDFAnnotation& annotation : m_annotations) {
        if (annotation.content.toLower().contains(lowerQuery) ||
            annotation.author.toLower().contains(lowerQuery) ||
            annotation.getTypeString().toLower().contains(lowerQuery)) {
            result.append(annotation);
        }
    }

    return result;
}

QList<PDFAnnotation> AnnotationModel::getAnnotationsByType(
    AnnotationType type) const {
    QList<PDFAnnotation> result;
    for (const PDFAnnotation& annotation : m_annotations) {
        if (annotation.type == type) {
            result.append(annotation);
        }
    }
    return result;
}

QList<PDFAnnotation> AnnotationModel::getAnnotationsByAuthor(
    const QString& author) const {
    QList<PDFAnnotation> result;
    for (const PDFAnnotation& annotation : m_annotations) {
        if (annotation.author == author) {
            result.append(annotation);
        }
    }
    return result;
}

QList<PDFAnnotation> AnnotationModel::getRecentAnnotations(int count) const {
    QList<PDFAnnotation> sorted = m_annotations;
    std::sort(sorted.begin(), sorted.end(),
              [](const PDFAnnotation& a, const PDFAnnotation& b) {
                  return a.modifiedTime > b.modifiedTime;
              });

    if (count > 0 && sorted.size() > count) {
        return sorted.mid(0, count);
    }

    return sorted;
}

QMap<AnnotationType, int> AnnotationModel::getAnnotationCountByType() const {
    QMap<AnnotationType, int> counts;

    for (const PDFAnnotation& annotation : m_annotations) {
        counts[annotation.type]++;
    }

    return counts;
}

QStringList AnnotationModel::getAuthors() const {
    QStringList authors;
    for (const PDFAnnotation& annotation : m_annotations) {
        if (!annotation.author.isEmpty() &&
            !authors.contains(annotation.author)) {
            authors.append(annotation.author);
        }
    }
    authors.sort();
    return authors;
}

// Poppler integration methods
Poppler::Annotation* PDFAnnotation::toPopplerAnnotation() const {
    if (pageNumber < 0) {
        return nullptr;
    }

    auto configure = [this](Poppler::Annotation* annotation) {
        if (!annotation) {
            return annotation;
        }
        annotation->setBoundary(boundingRect);
        annotation->setUniqueName(id);
        annotation->setAuthor(author);
        annotation->setContents(content);
        annotation->setCreationDate(createdTime);
        annotation->setModificationDate(modifiedTime);
        Poppler::Annotation::Style style = annotation->style();
        style.setColor(color);
        style.setOpacity(opacity);
        style.setWidth(lineWidth);
        annotation->setStyle(style);
        return annotation;
    };

    switch (type) {
        case AnnotationType::Highlight:
        case AnnotationType::Underline:
        case AnnotationType::StrikeOut:
        case AnnotationType::Squiggly: {
            auto* annotation = new Poppler::HighlightAnnotation();
            Poppler::HighlightAnnotation::Quad quad{};
            quad.points[0] = boundingRect.topLeft();
            quad.points[1] = boundingRect.topRight();
            quad.points[2] = boundingRect.bottomRight();
            quad.points[3] = boundingRect.bottomLeft();
            quad.capStart = false;
            quad.capEnd = false;
            quad.feather = 0.0;
            annotation->setHighlightQuads({quad});
            const auto highlightType =
                type == AnnotationType::Underline
                    ? Poppler::HighlightAnnotation::Underline
                : type == AnnotationType::StrikeOut
                    ? Poppler::HighlightAnnotation::StrikeOut
                : type == AnnotationType::Squiggly
                    ? Poppler::HighlightAnnotation::Squiggly
                    : Poppler::HighlightAnnotation::Highlight;
            annotation->setHighlightType(highlightType);
            return configure(annotation);
        }
        case AnnotationType::Note: {
            auto* annotation =
                new Poppler::TextAnnotation(Poppler::TextAnnotation::Linked);
            annotation->setTextIcon(QStringLiteral("Note"));
            return configure(annotation);
        }
        case AnnotationType::FreeText: {
            auto* annotation =
                new Poppler::TextAnnotation(Poppler::TextAnnotation::InPlace);
            annotation->setTextFont(QFont(fontFamily, fontSize));
            annotation->setTextColor(color);
            return configure(annotation);
        }
        case AnnotationType::Rectangle:
        case AnnotationType::Circle: {
            auto* annotation = new Poppler::GeomAnnotation();
            annotation->setGeomType(
                type == AnnotationType::Circle
                    ? Poppler::GeomAnnotation::InscribedCircle
                    : Poppler::GeomAnnotation::InscribedSquare);
            return configure(annotation);
        }
        case AnnotationType::Line:
        case AnnotationType::Arrow: {
            auto* annotation = new Poppler::LineAnnotation(
                Poppler::LineAnnotation::StraightLine);
            annotation->setLinePoints({startPoint, endPoint});
            if (type == AnnotationType::Arrow) {
                annotation->setLineEndStyle(
                    Poppler::LineAnnotation::ClosedArrow);
                annotation->setLineIntent(Poppler::LineAnnotation::Arrow);
            }
            return configure(annotation);
        }
        case AnnotationType::Ink: {
            auto* annotation = new Poppler::InkAnnotation();
            annotation->setInkPaths({inkPath.toVector()});
            return configure(annotation);
        }
    }
    return nullptr;
}

PDFAnnotation PDFAnnotation::fromPopplerAnnotation(
    Poppler::Annotation* annotation, int pageNum) {
    PDFAnnotation result;

    if (!annotation) {
        return result;
    }

    // Basic properties
    result.pageNumber = pageNum;
    result.boundingRect = annotation->boundary();
    result.content = annotation->contents();
    result.author = annotation->author();
    result.createdTime = annotation->creationDate();
    result.modifiedTime = annotation->modificationDate();

    // Extract style properties - Poppler doesn't expose these directly
    // Use default values for now
    result.color = Qt::yellow;
    result.opacity = 1.0;

    // Convert Poppler annotation type to our enum and extract type-specific
    // data
    switch (annotation->subType()) {
        case Poppler::Annotation::AHighlight:
            result.type = AnnotationType::Highlight;
            break;

        case Poppler::Annotation::AText: {
            // Simplified text annotation handling
            result.type = AnnotationType::Note;
            break;
        }

        case Poppler::Annotation::ALine: {
            result.type = AnnotationType::Line;
            // Note: Line point extraction would require proper Poppler API
            // usage For now, we'll use the bounding rect to approximate line
            // endpoints
            result.startPoint = result.boundingRect.topLeft();
            result.endPoint = result.boundingRect.bottomRight();
            break;
        }

        case Poppler::Annotation::AInk: {
            result.type = AnnotationType::Ink;
            // Note: Ink path extraction would require proper Poppler API usage
            // For now, we'll create a simple path from the bounding rect
            result.inkPath.clear();
            result.inkPath.append(result.boundingRect.topLeft());
            result.inkPath.append(result.boundingRect.topRight());
            result.inkPath.append(result.boundingRect.bottomRight());
            result.inkPath.append(result.boundingRect.bottomLeft());
            break;
        }

        case Poppler::Annotation::AGeom: {
            // Geometric annotation (rectangle, circle, etc.)
            result.type = AnnotationType::Rectangle;  // Default to rectangle
            break;
        }

        default:
            result.type = AnnotationType::Highlight;
            qWarning() << "Unknown annotation type:"
                       << static_cast<int>(annotation->subType());
            break;
    }

    // Generate unique ID for imported annotation
    result.id = QString("imported_%1_%2_%3")
                    .arg(pageNum)
                    .arg(QDateTime::currentMSecsSinceEpoch())
                    .arg(qHash(result.content));

    return result;
}
