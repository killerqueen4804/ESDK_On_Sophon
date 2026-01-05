#pragma once
#include <QObject>
#include <QThread>

#include "MqttClient.h"
#include "Go2Control.h"

class LogicControl : public QObject
{
    Q_OBJECT
public:
    explicit LogicControl(QObject *parent = nullptr);
    ~LogicControl();

signals:
    void signalTestStand();

private:
    void init_connections();

private:
    QThread m_thread;
    Go2Control* m_go2_control;
    MqttClient* m_mqtt_client;
};