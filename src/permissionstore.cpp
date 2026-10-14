// permissionstore.cpp —— 权限规则库读写实现
#include "permissionstore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace codearts {

PermissionStore::PermissionStore(QString dataDir)
    : m_dataDir(std::move(dataDir)) {}

QString PermissionStore::rulesPath() const {
  return m_dataDir + QStringLiteral("/storage/permission/global.json");
}

QString PermissionStore::bashModePath() const {
  return m_dataDir + QStringLiteral("/storage/permission/config.json");
}

QString PermissionStore::lastError() const { return m_lastError; }

static bool readJsonFile(const QString &path, QJsonDocument &doc,
                         QString &err) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    err = QStringLiteral("无法打开 %1").arg(path);
    return false;
  }
  QJsonParseError perr{};
  doc = QJsonDocument::fromJson(f.readAll(), &perr);
  if (perr.error != QJsonParseError::NoError) {
    err = QStringLiteral("%1 JSON 损坏: %2").arg(path, perr.errorString());
    return false;
  }
  return true;
}

QList<PermissionRule> PermissionStore::rules() {
  m_lastError.clear();
  const QString path = rulesPath();
  QJsonDocument doc;
  QString err;
  if (!readJsonFile(path, doc, err)) {
    // 文件不存在：无规则（安全语义，非兜底——规则表本就可能未初始化）。
    // JSON 损坏：显式记录错误，返回空表（调用方据 lastError 提示用户）。
    if (QFile::exists(path))
      m_lastError = err;
    return {};
  }
  if (!doc.isArray()) {
    m_lastError = QStringLiteral("%1 期望 JSON 数组").arg(path);
    return {};
  }
  QList<PermissionRule> out;
  const QJsonArray arr = doc.array();
  for (const auto &v : arr) {
    const QJsonObject o = v.toObject();
    PermissionRule r;
    r.permission = o.value(QStringLiteral("permission")).toString();
    r.pattern = o.value(QStringLiteral("pattern")).toString();
    r.action = o.value(QStringLiteral("action")).toString();
    if (!r.permission.isEmpty())
      out.append(r);
  }
  return out;
}

bool PermissionStore::setRule(const QString &permission, const QString &pattern,
                              const QString &action) {
  const QString path = rulesPath();
  QJsonDocument doc;
  QString err;
  if (!readJsonFile(path, doc, err)) {
    if (QFile::exists(path)) {
      m_lastError = err;
      return false;
    }
    doc = QJsonDocument(QJsonArray{}); // 首次初始化为空规则表
  }
  if (!doc.isArray()) {
    m_lastError = QStringLiteral("%1 期望 JSON 数组").arg(path);
    return false;
  }

  QJsonArray arr = doc.array();
  bool replaced = false;
  for (int i = 0; i < arr.size(); ++i) {
    QJsonObject o = arr.at(i).toObject();
    if (o.value(QStringLiteral("permission")).toString() == permission &&
        o.value(QStringLiteral("pattern")).toString() == pattern) {
      o.insert(QStringLiteral("action"), action); // 仅改 action，保留其他字段
      arr.replace(i, o);
      replaced = true;
      break;
    }
  }
  if (!replaced) {
    QJsonObject o;
    o.insert(QStringLiteral("permission"), permission);
    o.insert(QStringLiteral("pattern"), pattern);
    o.insert(QStringLiteral("action"), action);
    arr.append(o);
  }

  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    m_lastError = QStringLiteral("无法写入 %1").arg(path);
    return false;
  }
  f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
  return true;
}

bool PermissionStore::removeRule(const QString &permission,
                                 const QString &pattern) {
  const QString path = rulesPath();
  QJsonDocument doc;
  QString err;
  if (!readJsonFile(path, doc, err)) {
    m_lastError = err;
    return false;
  }
  if (!doc.isArray()) {
    m_lastError = QStringLiteral("%1 期望 JSON 数组").arg(path);
    return false;
  }
  QJsonArray arr = doc.array();
  for (int i = 0; i < arr.size(); ++i) {
    const QJsonObject o = arr.at(i).toObject();
    if (o.value(QStringLiteral("permission")).toString() == permission &&
        o.value(QStringLiteral("pattern")).toString() == pattern) {
      arr.removeAt(i);
      QFile f(path);
      if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_lastError = QStringLiteral("无法写入 %1").arg(path);
        return false;
      }
      f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
      return true;
    }
  }
  return true; // 不存在即已满足
}

QString PermissionStore::bashMode() {
  m_lastError.clear();
  const QString path = bashModePath();
  QJsonDocument doc;
  QString err;
  if (!readJsonFile(path, doc, err)) {
    m_lastError = err;
    return {};
  }
  if (!doc.isObject()) {
    m_lastError = QStringLiteral("%1 期望 JSON 对象").arg(path);
    return {};
  }
  return doc.object().value(QStringLiteral("bash_mode")).toString();
}

bool PermissionStore::setBashMode(const QString &mode) {
  const QString path = bashModePath();
  QJsonDocument doc;
  QString err;
  if (!readJsonFile(path, doc, err)) {
    if (QFile::exists(path)) {
      m_lastError = err;
      return false;
    }
    doc = QJsonDocument(QJsonObject{});
  }
  if (!doc.isObject()) {
    m_lastError = QStringLiteral("%1 期望 JSON 对象").arg(path);
    return false;
  }
  QJsonObject o = doc.object();
  o.insert(QStringLiteral("bash_mode"), mode);
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    m_lastError = QStringLiteral("无法写入 %1").arg(path);
    return false;
  }
  f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
  return true;
}

} // namespace codearts
