#pragma once

#include "types.h"

#include <QObject>

class QNetworkAccessManager;

class LLMRefiner : public QObject {
    Q_OBJECT

public:
    explicit LLMRefiner(QObject *parent = nullptr);
    void refine(const QString &text, const AppConfig &config);

signals:
    void refined(const QString &text);
    void errorOccurred(const QString &message);

private:
    QNetworkAccessManager *m_networkManager = nullptr;
};
