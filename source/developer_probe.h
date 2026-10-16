// Development-only host automation. Never included in release builds.
#pragma once

QWidget* findGraphView();

void writeWidgetInventory() {
    const QString path = qEnvironmentVariable(
        "WRAP_CHINESE_WIDGET_INVENTORY_PATH");
    if (path.isEmpty()) return;
    QJsonArray widgets;
    for (QWidget* widget : QApplication::allWidgets()) {
        if (!widget) continue;
        const QRect geometry = widget->geometry();
        QJsonObject item{
            {QStringLiteral("class"),
             QString::fromLatin1(widget->metaObject()->className())},
            {QStringLiteral("object_name"), widget->objectName()},
            {QStringLiteral("title"), widget->windowTitle()},
            {QStringLiteral("visible"), widget->isVisible()},
            {QStringLiteral("enabled"), widget->isEnabled()},
            {QStringLiteral("x"), geometry.x()},
            {QStringLiteral("y"), geometry.y()},
            {QStringLiteral("width"), geometry.width()},
            {QStringLiteral("height"), geometry.height()},
        };
        if (QWidget* parent = widget->parentWidget()) {
            item.insert(QStringLiteral("parent_class"),
                        QString::fromLatin1(parent->metaObject()->className()));
            item.insert(QStringLiteral("parent_object_name"),
                        parent->objectName());
        }
        widgets.append(item);
    }
    QJsonObject root{
        {QStringLiteral("schema"),
         QStringLiteral("wrap-widget-inventory-v1")},
        {QStringLiteral("widgets"), widgets},
    };
    if (QWidget* graph = findGraphView()) {
        QJsonArray metaObjects;
        for (QObject* object = graph; object; object = object->parent()) {
            const QMetaObject* meta = object->metaObject();
            QJsonArray methods;
            for (int index = meta->methodOffset(); index < meta->methodCount(); ++index)
                methods.append(QString::fromLatin1(
                    meta->method(index).methodSignature()));
            metaObjects.append(QJsonObject{
                {QStringLiteral("class"),
                 QString::fromLatin1(meta->className())},
                {QStringLiteral("methods"), methods},
            });
        }
        root.insert(QStringLiteral("graph_meta_objects"), metaObjects);

        QJsonArray shortcuts;
        for (QShortcut* shortcut : graph->window()->findChildren<QShortcut*>()) {
            shortcuts.append(QJsonObject{
                {QStringLiteral("key"), shortcut->key().toString()},
                {QStringLiteral("enabled"), shortcut->isEnabled()},
                {QStringLiteral("context"), static_cast<int>(shortcut->context())},
            });
        }
        root.insert(QStringLiteral("shortcuts"), shortcuts);

        QSet<QAction*> seenActions;
        QJsonArray actions;
        for (QWidget* widget : QApplication::allWidgets()) {
            if (!widget || widget->window() != graph->window()) continue;
            for (QAction* action : widget->actions()) {
                if (!action || seenActions.contains(action)) continue;
                seenActions.insert(action);
                QJsonArray menuActions;
                if (QMenu* menu = action->menu()) {
                    for (QAction* child : menu->actions())
                        menuActions.append(child ? child->text() : QString());
                }
                actions.append(QJsonObject{
                    {QStringLiteral("text"), action->text()},
                    {QStringLiteral("object_name"), action->objectName()},
                    {QStringLiteral("shortcut"), action->shortcut().toString()},
                    {QStringLiteral("enabled"), action->isEnabled()},
                    {QStringLiteral("owner_class"),
                     QString::fromLatin1(widget->metaObject()->className())},
                    {QStringLiteral("menu_actions"), menuActions},
                });
            }
        }
        root.insert(QStringLiteral("actions"), actions);
    }
    QString error;
    writeCaptureFile(path,
        QJsonDocument(root).toJson(QJsonDocument::Indented), error);
}

QWidget* findGraphView() {
    constexpr char kGraphViewClass[] =
        "Wrap::WGraphViewportItems::GraphicsView";
    for (QWidget* widget : QApplication::allWidgets()) {
        if (widget && widget->isVisible() &&
            std::strcmp(widget->metaObject()->className(), kGraphViewClass) == 0)
            return widget;
    }
    return nullptr;
}

bool openNodeMenu(QWidget* graphWidget) {
    if (!graphWidget) return false;
    auto* graph = qobject_cast<QGraphicsView*>(graphWidget);
    QWidget* target = graph ? graph->viewport() : graphWidget;
    QPoint local = target->rect().center();
    if (graph) {
        bool foundBlank = false;
        for (int y = 40; y < target->height() - 40 && !foundBlank; y += 55) {
            for (int x = 40; x < target->width() - 40; x += 70) {
                const QPoint candidate(x, y);
                if (graph->items(candidate).isEmpty()) {
                    local = candidate;
                    foundBlank = true;
                    break;
                }
            }
        }
        if (!foundBlank) {
            const QRectF occupied = graph->scene()->itemsBoundingRect();
            const QPointF blankScene(occupied.right() + 300.0,
                                     occupied.center().y());
            QRectF sceneRect = graph->sceneRect();
            sceneRect |= QRectF(blankScene.x() - target->width() / 2.0,
                                blankScene.y() - target->height() / 2.0,
                                target->width(), target->height());
            graph->setSceneRect(sceneRect);
            graph->centerOn(blankScene);
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            local = target->rect().center();
        }
    }
    const QPoint global = target->mapToGlobal(local);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(local), QPointF(global),
                      Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(target, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(local),
                        QPointF(global), Qt::RightButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(target, &release);
    const QPoint graphLocal = graphWidget->mapFromGlobal(global);
    QContextMenuEvent context(QContextMenuEvent::Mouse, graphLocal, global);
    return QApplication::sendEvent(graphWidget, &context);
}

QAction* findNodeMenuAction(QMenu* menu, const QString& nodeType) {
    if (!menu) return nullptr;
    for (QAction* action : menu->actions()) {
        if (!action) continue;
        QString text = action->text();
        text.remove(QLatin1Char('&'));
        if (!action->menu() &&
            text.trimmed().compare(nodeType, Qt::CaseInsensitive) == 0)
            return action;
        if (QAction* match = findNodeMenuAction(action->menu(), nodeType))
            return match;
    }
    return nullptr;
}

void writeProbeStatus(const QString& nodeType, bool success,
                      int beforeItems, int afterItems,
                      const QString& menuClass, const QString& error) {
    const QString path =
        qEnvironmentVariable("WRAP_CHINESE_PROBE_STATUS_PATH");
    if (path.isEmpty()) return;
    const QJsonObject root{
        {QStringLiteral("schema"), QStringLiteral("wrap-node-probe-status-v1")},
        {QStringLiteral("node_type"), nodeType},
        {QStringLiteral("success"), success},
        {QStringLiteral("scene_items_before"), beforeItems},
        {QStringLiteral("scene_items_after"), afterItems},
        {QStringLiteral("menu_class"), menuClass},
        {QStringLiteral("error"), error},
    };
    QString writeError;
    writeCaptureFile(path,
        QJsonDocument(root).toJson(QJsonDocument::Indented), writeError);
}

void startNodeProbe(QApplication* application, const QString& nodeType,
                    const std::function<void(bool)>& completion) {
    QTimer::singleShot(1000, application, [application, nodeType, completion] {
        QWidget* graphWidget = findGraphView();
        auto* graph = qobject_cast<QGraphicsView*>(graphWidget);
        if (!graphWidget || !graph || !graph->scene()) {
            writeProbeStatus(nodeType, false, -1, -1, QString(),
                             QStringLiteral("graph view was not found"));
            completion(false);
            return;
        }
        const int beforeItems = graph->scene()->items().size();
        graphWidget->setFocus(Qt::OtherFocusReason);
        // Keep the temporary node menu in Wrap's canonical English so its
        // leaf actions can be selected deterministically by node type.
        g_enabled.store(false, std::memory_order_relaxed);

        QTimer::singleShot(200, application,
                           [application, nodeType, graph = QPointer<QGraphicsView>(graph), beforeItems,
                            completion] {
            auto* nodeMenu =
                qobject_cast<QMenu*>(QApplication::activePopupWidget());
            const QString menuClass = nodeMenu
                ? QString::fromLatin1(nodeMenu->metaObject()->className())
                : QString();
            QAction* nodeAction = findNodeMenuAction(nodeMenu, nodeType);
            if (!nodeAction) {
                g_enabled.store(true, std::memory_order_relaxed);
                writeWidgetInventory();
                writeProbeStatus(nodeType, false, beforeItems, beforeItems,
                                 menuClass,
                                 QStringLiteral("node action was not found in the expanded menu"));
                if (nodeMenu) nodeMenu->close();
                completion(false);
                return;
            }
            nodeAction->trigger();
            // Programmatic QAction activation does not automatically unwind
            // every parent popup the way a physical click does.
            for (QWidget* widget : QApplication::allWidgets()) {
                if (auto* menu = qobject_cast<QMenu*>(widget)) {
                    if (menu->isVisible()) menu->close();
                }
            }
            g_enabled.store(true, std::memory_order_relaxed);
            QTimer::singleShot(900, application,
                               [application, nodeType, graph = QPointer<QGraphicsView>(graph), beforeItems,
                                menuClass, completion] {
                    const int afterItems = graph && graph->scene()
                        ? graph->scene()->items().size() : -1;
                    const bool success = afterItems > beforeItems;
                    writeProbeStatus(
                        nodeType, success, beforeItems, afterItems, menuClass,
                        success ? QString()
                                : QStringLiteral("node menu selection did not add a scene item"));
                    if (!success) {
                        completion(false);
                        return;
                    }
                    captureUntranslated();
                    QTimer::singleShot(700, application, [] {
                        captureUntranslated();
                    });
                    QTimer::singleShot(1000, application,
                                       [completion] { completion(true); });
            });
        });
        // The node menu runs a nested event loop.  Register selection first
        // so it executes while that menu is open.
        openNodeMenu(graphWidget);
    });
}

struct ProbePlanJob {
    QString nodeType;
    QString capturePath;
    QString statusPath;
};

void startNodeProbePlan(QApplication* application, const QString& planPath) {
    QFile input(planPath);
    if (!input.open(QIODevice::ReadOnly)) {
        application->quit();
        return;
    }
    QJsonParseError parseError{};
    const QJsonDocument document =
        QJsonDocument::fromJson(input.readAll(), &parseError);
    const QJsonObject root = document.object();
    const QJsonArray nodeValues = root.value(QStringLiteral("nodes")).toArray();
    if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
        nodeValues.isEmpty()) {
        application->quit();
        return;
    }
    auto jobs = std::make_shared<std::vector<ProbePlanJob>>();
    jobs->reserve(static_cast<std::size_t>(nodeValues.size()));
    for (const QJsonValue& value : nodeValues) {
        const QJsonObject item = value.toObject();
        ProbePlanJob job{
            item.value(QStringLiteral("node_type")).toString(),
            item.value(QStringLiteral("capture")).toString(),
            item.value(QStringLiteral("status")).toString(),
        };
        if (job.nodeType.isEmpty() || job.capturePath.isEmpty() ||
            job.statusPath.isEmpty()) {
            application->quit();
            return;
        }
        jobs->push_back(std::move(job));
    }

    auto next = std::make_shared<std::function<void(std::size_t)>>();
    std::weak_ptr<std::function<void(std::size_t)>> weakNext(next);
    *next = [application, jobs, weakNext](std::size_t index) {
        const auto next = weakNext.lock();
        if (!next) return;
        if (index >= jobs->size()) {
            application->quit();
            return;
        }
        const ProbePlanJob& job = (*jobs)[index];
        qputenv("WRAP_CHINESE_CAPTURE_PATH", job.capturePath.toUtf8());
        qputenv("WRAP_CHINESE_PROBE_STATUS_PATH", job.statusPath.toUtf8());
        startNodeProbe(application, job.nodeType,
                       [application, next, index](bool success) {
            if (!success) {
                application->quit();
                return;
            }
            (*next)(index + 1);
        });
    };

    const QString baseline =
        root.value(QStringLiteral("baseline_capture")).toString();
    QTimer::singleShot(1200, application, [baseline, next] {
        if (!baseline.isEmpty()) {
            qputenv("WRAP_CHINESE_CAPTURE_PATH", baseline.toUtf8());
            captureUntranslated();
        }
        (*next)(0);
    });
}

