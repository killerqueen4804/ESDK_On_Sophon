#include "LogicControl.h"

LogicControl::LogicControl(QObject *parent) : QObject(parent)
{

    m_mqtt_client = new MqttClient();
    m_go2_control = new Go2Control();

    init_connections();

    this->moveToThread(&m_thread);
    m_thread.start();

    emit signalTestStand();
}

void LogicControl::init_connections()
{
    connect(this, &LogicControl::signalTestStand, m_go2_control, &Go2Control::slotsTestStand);
}

LogicControl::~LogicControl()
{
    delete m_mqtt_client;
    delete m_go2_control;
    m_thread.quit();
    m_thread.wait();
}