// goalstate.cpp —— /goal 断点续跑持久化实现
#include "goalstate.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace codearts {

int GoalState::doneSteps() const {
  int n = 0;
  for (const auto &s : steps)
    if (s.done)
      ++n;
  return n;
}

bool GoalState::load(const QString &path, QString *error) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error)
      *error = QStringLiteral("无法打开 %1").arg(path);
    return false;
  }
  QJsonParseError perr{};
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
  if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
    if (error)
      *error = QStringLiteral("%1 JSON 损坏: %2").arg(path, perr.errorString());
    return false;
  }
  const QJsonObject o = doc.object();
  goal = o.value(QStringLiteral("goal")).toString();
  acceptance = o.value(QStringLiteral("acceptance")).toString();
  status = o.value(QStringLiteral("status")).toString(QStringLiteral("idle"));
  totalSteps = o.value(QStringLiteral("totalSteps")).toInt();
  lastStepTitle = o.value(QStringLiteral("lastStepTitle")).toString();
  steps.clear();
  const QJsonArray arr = o.value(QStringLiteral("steps")).toArray();
  for (const auto &v : arr) {
    const QJsonObject so = v.toObject();
    GoalStep st;
    st.index = so.value(QStringLiteral("index")).toInt();
    st.title = so.value(QStringLiteral("title")).toString();
    st.done = so.value(QStringLiteral("done")).toBool();
    st.recommendation = so.value(QStringLiteral("recommendation")).toString();
    steps.append(st);
  }
  return true;
}

bool GoalState::save(const QString &path, QString *error) const {
  QJsonObject o;
  o.insert(QStringLiteral("goal"), goal);
  o.insert(QStringLiteral("acceptance"), acceptance);
  o.insert(QStringLiteral("status"), status);
  o.insert(QStringLiteral("totalSteps"), totalSteps);
  o.insert(QStringLiteral("lastStepTitle"), lastStepTitle);
  QJsonArray arr;
  for (const auto &s : steps) {
    QJsonObject so;
    so.insert(QStringLiteral("index"), s.index);
    so.insert(QStringLiteral("title"), s.title);
    so.insert(QStringLiteral("done"), s.done);
    so.insert(QStringLiteral("recommendation"), s.recommendation);
    arr.append(so);
  }
  o.insert(QStringLiteral("steps"), arr);

  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (error)
      *error = QStringLiteral("无法写入 %1").arg(path);
    return false;
  }
  f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
  return true;
}

} // namespace codearts
