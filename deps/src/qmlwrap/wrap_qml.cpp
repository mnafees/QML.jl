#include <QApplication>
#include <QLibraryInfo>
#include <QPainter>
#include <QPaintDevice>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QSurfaceFormat>
#include <QTimer>
#include <QtQml>

#include "application_manager.hpp"
#include "julia_api.hpp"
#include "julia_display.hpp"
#include "julia_object.hpp"
#include "julia_painteditem.hpp"
#include "julia_signals.hpp"
#include "listmodel.hpp"
#include "opengl_viewport.hpp"
#include "glvisualize_viewport.hpp"
#include "type_conversion.hpp"

namespace qmlwrap
{

void load_qml_app(const QString& path, cxx_wrap::ArrayRef<jl_value_t*> property_names, cxx_wrap::ArrayRef<jl_value_t*> context_properties)
{
  auto e = ApplicationManager::instance().init_qmlapplicationengine();
  ApplicationManager::instance().add_context_properties(property_names, context_properties);
  e->load(path);
}


} // namespace qmlwrap

JULIA_CPP_MODULE_BEGIN(registry)
  using namespace cxx_wrap;

  Module& qml_module = registry.create_module("QML");

  qmlRegisterSingletonType("org.julialang", 1, 0, "Julia", qmlwrap::julia_js_singletontype_provider);
  qmlRegisterType<qmlwrap::JuliaSignals>("org.julialang", 1, 0, "JuliaSignals");
  qmlRegisterType<qmlwrap::JuliaDisplay>("org.julialang", 1, 0, "JuliaDisplay");
  qmlRegisterType<qmlwrap::JuliaPaintedItem>("org.julialang", 1, 1, "JuliaPaintedItem");
  qmlRegisterType<qmlwrap::OpenGLViewport>("org.julialang", 1, 0, "OpenGLViewport");
  qmlRegisterType<qmlwrap::GLVisualizeViewport>("org.julialang", 1, 0, "GLVisualizeViewport");

  qml_module.add_abstract<QObject>("QObject");

  qml_module.add_type<QQmlContext>("QQmlContext", julia_type<QObject>())
    .method("context_property", &QQmlContext::contextProperty);
  qml_module.method("set_context_property", qmlwrap::set_context_property);

  qml_module.add_type<QQmlEngine>("QQmlEngine", julia_type<QObject>())
    .method("root_context", &QQmlEngine::rootContext);

  qml_module.add_type<QQmlApplicationEngine>("QQmlApplicationEngine", julia_type<QQmlEngine>())
    .constructor<QString>() // Construct with path to QML
    .method("load", static_cast<void (QQmlApplicationEngine::*)(const QString&)>(&QQmlApplicationEngine::load)); // cast needed because load is overloaded

  qml_module.method("qt_prefix_path", []() { return QLibraryInfo::location(QLibraryInfo::PrefixPath); });

  qml_module.add_abstract<QQuickWindow>("QQuickWindow")
    .method("content_item", &QQuickWindow::contentItem);

  qml_module.method("effectiveDevicePixelRatio", [] (QQuickWindow& w)
  {
#if (QT_VERSION >= QT_VERSION_CHECK(5, 4, 0))
      return w.effectiveDevicePixelRatio();
#else
      return 1.0;
#endif
  });

  qml_module.add_type<QQuickView>("QQuickView", julia_type<QQuickWindow>())
    .method("set_source", &QQuickView::setSource)
    .method("show", &QQuickView::show) // not exported: conflicts with Base.show
    .method("engine", &QQuickView::engine)
    .method("root_object", &QQuickView::rootObject);

  qml_module.add_abstract<QQuickItem>("QQuickItem")
    .method("window", &QQuickItem::window);

  qml_module.add_type<qmlwrap::JuliaPaintedItem>("JuliaPaintedItem", julia_type<QQuickItem>());

  qml_module.add_type<QByteArray>("QByteArray").constructor<const char*>();
  qml_module.add_type<QQmlComponent>("QQmlComponent", julia_type<QObject>())
    .constructor<QQmlEngine*>()
    .method("set_data", &QQmlComponent::setData);
  qml_module.method("create", [](QQmlComponent& comp, QQmlContext* context)
  {
    if(!comp.isReady())
    {
      qWarning() << "QQmlComponent is not ready, aborting create. Errors were: " << comp.errors();
      return;
    }

    QObject* obj = comp.create(context);
    if(context != nullptr)
    {
      obj->setParent(context); // setting this makes sure the new object gets deleted
    }
  });

  // App manager functions
  qml_module.add_type<qmlwrap::ApplicationManager>("ApplicationManager");
  qml_module.method("init_application", []() { qmlwrap::ApplicationManager::instance().init_application(); });
  qml_module.method("init_qmlapplicationengine", []() { return qmlwrap::ApplicationManager::instance().init_qmlapplicationengine(); });
  qml_module.method("init_qmlengine", []() { return qmlwrap::ApplicationManager::instance().init_qmlengine(); });
  qml_module.method("init_qquickview", []() { return qmlwrap::ApplicationManager::instance().init_qquickview(); });
  qml_module.method("qmlcontext", []() { return qmlwrap::ApplicationManager::instance().root_context(); });
  qml_module.method("load_qml_app", qmlwrap::load_qml_app);
  qml_module.method("exec", []() { qmlwrap::ApplicationManager::instance().exec(); });
  qml_module.method("exec_async", []() { qmlwrap::ApplicationManager::instance().exec_async(); });

  qml_module.add_type<QTimer>("QTimer", julia_type<QObject>());

  qml_module.add_type<qmlwrap::JuliaObject>("JuliaObject", julia_type<QObject>())
    .method("set", &qmlwrap::JuliaObject::set) // Not exported, use @qmlset
    .method("julia_object_value", &qmlwrap::JuliaObject::value); // Not exported, use @qmlget

  // Emit signals helper
  qml_module.method("emit", [](const char* signal_name, cxx_wrap::ArrayRef<jl_value_t*> args)
  {
    using namespace qmlwrap;
    JuliaSignals* julia_signals = JuliaAPI::instance()->juliaSignals();
    if(julia_signals == nullptr)
    {
      throw std::runtime_error("No signals available");
    }
    julia_signals->emit_signal(signal_name, args);
  });

  // Function to register a function
  qml_module.method("register_function", [](cxx_wrap::ArrayRef<jl_value_t*> args)
  {
    for(jl_value_t* arg : args)
    {
      qmlwrap::JuliaAPI::instance()->register_function(convert_to_cpp<QString>(arg));
    }
  });

  qml_module.add_type<qmlwrap::JuliaDisplay>("JuliaDisplay", julia_type("CppDisplay"))
    .method("load_png", &qmlwrap::JuliaDisplay::load_png);

  qml_module.add_type<QPaintDevice>("QPaintDevice")
    .method("width", &QPaintDevice::width)
    .method("height", &QPaintDevice::height)
    .method("logicalDpiX", &QPaintDevice::logicalDpiX)
    .method("logicalDpiY", &QPaintDevice::logicalDpiY);
  qml_module.add_type<QPainter>("QPainter")
    .method("device", &QPainter::device);

  qml_module.add_type<qmlwrap::ListModel>("ListModel", julia_type<QObject>())
    .constructor<const cxx_wrap::ArrayRef<jl_value_t*>&>()
    .constructor<const cxx_wrap::ArrayRef<jl_value_t*>&, jl_function_t*>()
    .method("setconstructor", &qmlwrap::ListModel::setconstructor)
    .method("removerole", static_cast<void (qmlwrap::ListModel::*)(const int)>(&qmlwrap::ListModel::removerole))
    .method("removerole", static_cast<void (qmlwrap::ListModel::*)(const std::string&)>(&qmlwrap::ListModel::removerole));
  qml_module.method("addrole", [] (qmlwrap::ListModel& m, const std::string& role, jl_function_t* getter) { m.addrole(role, getter); });
  qml_module.method("addrole", [] (qmlwrap::ListModel& m, const std::string& role, jl_function_t* getter, jl_function_t* setter) { m.addrole(role, getter, setter); });
  qml_module.method("setrole", [] (qmlwrap::ListModel& m, const int idx, const std::string& role, jl_function_t* getter) { m.setrole(idx, role, getter); });
  qml_module.method("setrole", [] (qmlwrap::ListModel& m, const int idx, const std::string& role, jl_function_t* getter, jl_function_t* setter) { m.setrole(idx, role, getter, setter); });

  qml_module.add_type<QVariantMap>("QVariantMap");
  qml_module.method("getindex", [](const QVariantMap& m, const QString& key) { return m[key]; });

  // Exports:
  qml_module.export_symbols("QQmlContext", "set_context_property", "root_context", "load", "qt_prefix_path", "set_source", "engine", "QByteArray", "QQmlComponent", "set_data", "create", "QQuickItem", "content_item", "JuliaObject", "QTimer", "context_property", "emit", "JuliaDisplay", "init_application", "qmlcontext", "init_qmlapplicationengine", "init_qmlengine", "init_qquickview", "exec", "exec_async", "ListModel", "addrole", "setconstructor", "removerole", "setrole", "QVariantMap");
  qml_module.export_symbols("QPainter", "device", "width", "height", "logicalDpiX", "logicalDpiY", "QQuickWindow", "effectiveDevicePixelRatio", "window", "JuliaPaintedItem");
JULIA_CPP_MODULE_END
