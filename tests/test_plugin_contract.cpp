#include "plugin_contract.hpp"
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
class ContractTest:public QObject{Q_OBJECT private slots:
void version(){QJsonObject o{{"api_version",2},{"type","gift"}};QVERIFY(!PluginContract::validateEvent(QJsonDocument(o).toJson()));}
void event(){QJsonObject o{{"api_version",1},{"type","gift"}};QVERIFY(PluginContract::validateEvent(QJsonDocument(o).toJson()));}
void size(){QVERIFY(!PluginContract::validateEvent(QByteArray(PluginContract::MaxEventBytes+1,'x')));}
void action(){QJsonObject o{{"api_version",1},{"actions",QJsonArray{QJsonObject{{"type","exec"},{"text","x"}}}}};QVERIFY(!PluginContract::validateActions(QJsonDocument(o).toJson()));}
};QTEST_GUILESS_MAIN(ContractTest)
#include "test_plugin_contract.moc"